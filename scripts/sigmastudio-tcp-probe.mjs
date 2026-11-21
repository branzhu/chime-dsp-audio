import fs from 'node:fs';
import net from 'node:net';
import path from 'node:path';
import readline from 'node:readline';
import { pathToFileURL } from 'node:url';

const MAX_PAYLOAD_BYTES = 65536;
const MAX_CAPTURE_BYTES = 8 * 1024 * 1024;

export function decodeHex(value) {
  const compact = value.replace(/\s/g, '');
  if (!compact || compact.length % 2 !== 0 || !/^[0-9a-f]+$/i.test(compact)) {
    throw new Error('Expected a nonempty, even number of hexadecimal digits.');
  }
  if (compact.length / 2 > MAX_PAYLOAD_BYTES + 14) throw new Error('Response exceeds the frame budget.');
  return Buffer.from(compact, 'hex');
}

export function inspectStream(bytes) {
  const frames = [];
  let offset = 0;
  while (offset < bytes.length) {
    const control = bytes[offset];
    if (![0x09, 0x0a, 0x0b].includes(control)) {
      return { frames, remainingHex: bytes.subarray(offset).toString('hex'), issue: control === 0x1b
        ? `Undocumented native 3.4.0 control 0x1b at byte ${offset}; raw bytes retained, NOT supported by strict R/W profiles (bench image has an explicit exact-capture-only quirk).`
        : `Unknown control at byte ${offset}.` };
    }
    if (bytes.length - offset < 14) {
      return { frames, remainingHex: bytes.subarray(offset).toString('hex'), issue: `Incomplete header at byte ${offset}.` };
    }
    const writeRequest = control === 0x09;
    const totalLength = bytes.readUInt32BE(offset + (writeRequest ? 3 : 1));
    const dataLength = bytes.readUInt32BE(offset + (writeRequest ? 8 : 6));
    if (totalLength < 14 || totalLength > MAX_PAYLOAD_BYTES + 14 || dataLength > MAX_PAYLOAD_BYTES) {
      return { frames, remainingHex: bytes.subarray(offset).toString('hex'), issue: `Invalid length budget at byte ${offset}.` };
    }
    const expectedLength = control === 0x0a ? 14 : 14 + dataLength;
    if (totalLength !== expectedLength) {
      return { frames, remainingHex: bytes.subarray(offset).toString('hex'), issue: `Length differs from the ADI diagram hypothesis at byte ${offset}.` };
    }
    if (bytes.length - offset < totalLength) {
      return { frames, remainingHex: bytes.subarray(offset).toString('hex'), issue: `Incomplete payload at byte ${offset}.` };
    }
    const addressOffset = offset + (writeRequest ? 12 : 10);
    const frame = {
      offset, control, totalLength, dataLength,
      chip: bytes[offset + (writeRequest ? 7 : 5)],
      addressBE: bytes.readUInt16BE(addressOffset),
      hex: bytes.subarray(offset, offset + totalLength).toString('hex'),
    };
    if (writeRequest) {
      frame.flag = bytes[offset + 1];
      frame.channel = bytes[offset + 2];
    } else if (control === 0x0a) {
      frame.reservedHex = bytes.subarray(offset + 12, offset + 14).toString('hex');
    } else {
      frame.addressLE = bytes.readUInt16LE(addressOffset);
      frame.status = bytes[offset + 12];
      frame.reserved = bytes[offset + 13];
    }
    frames.push(frame);
    offset += totalLength;
  }
  return { frames, remainingHex: '', issue: null };
}

export function analyzeCapture(filename) {
  const streams = new Map();
  const lines = fs.readFileSync(filename, 'utf8').trim().split(/\r?\n/);
  const events = lines.map(line => JSON.parse(line));
  if (events[0]?.schema !== 'audio-dsp.sigma-tcp-capture.v1') throw new Error('Unknown capture schema.');
  let totalBytes = 0;
  for (const event of events) {
    if (!['rx', 'tx'].includes(event.event)) continue;
    const bytes = decodeHex(event.hex);
    totalBytes += bytes.length;
    if (totalBytes > MAX_CAPTURE_BYTES) throw new Error('Capture exceeds the analysis budget.');
    const key = `${event.connection}:${event.event}`;
    if (!streams.has(key)) streams.set(key, []);
    streams.get(key).push(bytes);
  }
  return {
    decoderProfile: 'sigmastudio-plus-3.4.0-documented-rw',
    evidenceScope: 'Documented and pinned native 3.4.0 R/W layout only, NOT full connection/download/Safeload/error compatibility; capture client identity and response acceptance remain unverified.',
    captureMetadata: events[0],
    streams: [...streams].map(([stream, chunks]) => ({ stream, ...inspectStream(Buffer.concat(chunks)) })),
    completed: events.at(-1)?.event === 'end',
  };
}

export async function startCapture({ output, port = 8086, durationMs = 120000, target = 'UNVERIFIED client' }) {
  if (typeof target !== 'string' || !target.trim() || target.length > 256) throw new Error('Invalid target label.');
  if (!Number.isInteger(port) || port < 0 || port > 65535) throw new Error('Invalid port.');
  if (!Number.isInteger(durationMs) || durationMs < 1 || durationMs > 3600000) throw new Error('Invalid duration.');
  fs.mkdirSync(path.dirname(path.resolve(output)), { recursive: true });
  const descriptor = fs.openSync(output, 'wx');
  const started = process.hrtime.bigint();
  const sockets = new Set();
  let activeSocket;
  let connection = 0;
  let activeConnection = 0;
  let capturedBytes = 0;
  let closed = false;
  let timer;
  let resolveDone;
  const done = new Promise(resolve => { resolveDone = resolve; });
  const record = (event, fields = {}) => fs.writeSync(descriptor, JSON.stringify({
    event, elapsedMs: Number(process.hrtime.bigint() - started) / 1e6, ...fields,
  }) + '\n');
  const server = net.createServer(socket => {
    if (activeSocket && !activeSocket.destroyed) {
      socket.destroy();
      record('rejected', { reason: 'single-client limit' });
      return;
    }
    connection += 1;
    const identifier = connection;
    activeConnection = identifier;
    activeSocket = socket;
    sockets.add(socket);
    record('connected', { connection: identifier, remoteAddress: socket.remoteAddress });
    socket.setNoDelay(true);
    socket.on('data', bytes => {
      if (closed) return;
      if (capturedBytes + bytes.length > MAX_CAPTURE_BYTES) {
        close('capture byte limit');
        return;
      }
      capturedBytes += bytes.length;
      record('rx', { connection: identifier, hex: bytes.toString('hex') });
    });
    socket.on('error', error => { if (!closed) record('socket-error', { connection: identifier, message: error.message }); });
    socket.on('close', () => {
      sockets.delete(socket);
      if (activeSocket === socket) activeSocket = undefined;
      if (!closed) record('disconnected', { connection: identifier });
    });
  });
  function close(reason = 'requested') {
    if (closed) return done;
    closed = true;
    clearTimeout(timer);
    for (const socket of sockets) socket.destroy();
    record('end', { reason, capturedBytes });
    fs.closeSync(descriptor);
    server.close(() => resolveDone());
    return done;
  }
  record('metadata', {
    schema: 'audio-dsp.sigma-tcp-capture.v1', target, targetSource: 'operator-label-not-client-verification',
    collectedAt: new Date().toISOString(), evidence: 'Client identity unverified; no automatic replies.',
  });
  server.on('error', error => { if (!closed) { record('server-error', { message: error.message }); close('server error'); } });
  await new Promise((resolve, reject) => {
    server.once('error', reject);
    server.listen(port, '127.0.0.1', () => { server.off('error', reject); resolve(); });
  });
  record('listening', { address: server.address() });
  timer = setTimeout(() => close('duration elapsed'), durationMs);
  return {
    port: server.address().port, done, close,
    mark(text) { if (closed) throw new Error('Capture is closed.'); record('mark', { text: text.slice(0, 1024) }); },
    disconnect() { if (activeSocket) activeSocket.destroy(); },
    send(hex) {
      if (closed || !activeSocket || activeSocket.destroyed) throw new Error('No active client.');
      const bytes = decodeHex(hex);
      if (capturedBytes + bytes.length > MAX_CAPTURE_BYTES) throw new Error('Capture byte limit reached.');
      capturedBytes += bytes.length;
      record('tx', { connection: activeConnection, hex: bytes.toString('hex'), evidence: 'Operator-specified test response; socket delivery is not client acceptance.' });
      return activeSocket.write(bytes);
    },
  };
}

async function main(args) {
  const [command, ...rest] = args;
  if (command === 'analyze' && rest.length === 1) {
    console.log(JSON.stringify(analyzeCapture(rest[0]), null, 2));
    return;
  }
  if (command !== 'capture' || rest.length % 2 !== 0) throw new Error('Usage: capture --out <new.jsonl> [--port 8086] [--duration-ms 120000] [--target <label>], or analyze <capture.jsonl>');
  const options = new Map();
  for (let index = 0; index < rest.length; index += 2) {
    if (!['--out', '--port', '--duration-ms', '--target'].includes(rest[index]) || options.has(rest[index])) throw new Error('Unknown or duplicate option.');
    options.set(rest[index], rest[index + 1]);
  }
  if (!options.get('--out')) throw new Error('--out is required; existing files are never overwritten.');
  const capture = await startCapture({
    output: options.get('--out'), port: Number(options.get('--port') ?? 8086),
    durationMs: Number(options.get('--duration-ms') ?? 120000), target: options.get('--target') ?? 'UNVERIFIED client',
  });
  console.log(`Listening on 127.0.0.1:${capture.port}; commands: mark <action>, send <hex>, disconnect, quit`);
  const input = readline.createInterface({ input: process.stdin, output: process.stdout });
  input.on('line', line => {
    try {
      if (line === 'quit') capture.close('operator quit');
      else if (line === 'disconnect') capture.disconnect();
      else if (line.startsWith('mark ')) capture.mark(line.slice(5));
      else if (line.startsWith('send ')) capture.send(line.slice(5));
      else throw new Error('Use mark, send, disconnect or quit.');
    } catch (error) { console.error(error.message); }
  });
  const interrupt = () => capture.close('SIGINT');
  process.once('SIGINT', interrupt);
  await capture.done;
  input.close();
  process.off('SIGINT', interrupt);
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  main(process.argv.slice(2)).catch(error => { console.error(error.message); process.exitCode = 1; });
}

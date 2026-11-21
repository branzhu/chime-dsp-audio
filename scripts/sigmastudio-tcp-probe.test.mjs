import assert from 'node:assert/strict';
import fs from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { once } from 'node:events';
import test from 'node:test';
import { analyzeCapture, decodeHex, inspectStream, startCapture } from './sigmastudio-tcp-probe.mjs';

const writeFrame = Buffer.from('090001000000127000000004123401020304', 'hex');
const readFrame = Buffer.from('0a0000000e700000000412340000', 'hex');

test('read requests have no requested-length payload and follow a write in one stream', () => {
  const result = inspectStream(Buffer.concat([writeFrame, readFrame]));
  assert.equal(result.issue, null);
  assert.equal(result.frames.length, 2);
  assert.equal(result.frames[0].addressBE, 0x1234);
  assert.equal(result.frames[0].channel, 1);
  assert.equal(result.frames[1].totalLength, 14);
  assert.equal(result.frames[1].dataLength, 4);
});

test('all truncated prefixes are incomplete rather than successful frames', () => {
  for (let length = 1; length < writeFrame.length; length += 1) {
    const result = inspectStream(writeFrame.subarray(0, length));
    assert.equal(result.frames.length, 0);
    assert.match(result.issue, /Incomplete/);
  }
});

test('invalid length and unknown opcode stop parsing without stream resynchronization', () => {
  for (const total of [0, 13, 0xffffffff]) {
    const bytes = Buffer.from(writeFrame);
    bytes.writeUInt32BE(total, 3);
    assert.match(inspectStream(bytes).issue, /Invalid/);
  }
  const bytes = Buffer.from(writeFrame);
  bytes.writeUInt32BE(17, 3);
  assert.match(inspectStream(bytes).issue, /hypothesis/);
  assert.equal(inspectStream(Buffer.concat([Buffer.from([255]), readFrame])).frames.length, 0);
});

test('read responses report both address interpretations without guessing compatibility', () => {
  const result = inspectStream(Buffer.from('0b0000001270000000043412000001020304', 'hex'));
  assert.equal(result.frames[0].addressBE, 0x3412);
  assert.equal(result.frames[0].addressLE, 0x1234);
  assert.equal(result.frames[0].status, 0);
});

test('manual hex rejects malformed and oversized input', () => {
  assert.deepEqual(decodeHex('01 02\nA0'), Buffer.from([1, 2, 160]));
  for (const value of ['', '0', '0g', '00'.repeat(65551)]) assert.throws(() => decodeHex(value));
});

test('capture records exact RX/TX and reassembles separated receive events without automatic replies', async context => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'sigma-probe-test-'));
  const output = path.join(directory, 'capture.jsonl');
  const capture = await startCapture({ output, port: 0, durationMs: 5000 });
  const client = net.createConnection({ host: '127.0.0.1', port: capture.port });
  context.after(async () => { client.destroy(); await capture.close(); fs.rmSync(directory, { recursive: true, force: true }); });
  await once(client, 'connect');
  capture.mark('synthetic test client; not SigmaStudio evidence');
  const received = once(client, 'data');
  client.write(writeFrame.subarray(0, 5));
  await new Promise(resolve => setImmediate(resolve));
  client.write(Buffer.concat([writeFrame.subarray(5), readFrame]));
  const deadline = Date.now() + 2000;
  while (!fs.readFileSync(output, 'utf8').includes(readFrame.toString('hex'))) {
    if (Date.now() > deadline) throw new Error('Capture did not receive test data.');
    await new Promise(resolve => setTimeout(resolve, 10));
  }
  const before = fs.readFileSync(output, 'utf8');
  assert.equal(before.includes('"event":"tx"'), false);
  capture.send('0b0000001270000000043412000001020304');
  const [bytes] = await received;
  assert.equal(bytes.toString('hex'), '0b0000001270000000043412000001020304');
  await capture.close('test complete');
  const report = analyzeCapture(output);
  assert.equal(report.completed, true);
  assert.equal(report.captureMetadata.target, 'UNVERIFIED client');
  assert.equal(report.decoderProfile, 'sigmastudio-plus-3.4.0-documented-rw');
  assert.match(report.evidenceScope, /NOT full connection/);
  assert.equal(report.streams.find(stream => stream.stream === '1:rx').frames.length, 2);
  assert.equal(report.streams.find(stream => stream.stream === '1:tx').frames.length, 1);
  await assert.rejects(startCapture({ output, port: 0 }), /EEXIST/);
});

test('capture closes on its explicit duration without waiting for a client', async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'sigma-probe-timeout-'));
  try {
    const output = path.join(directory, 'capture.jsonl');
    const capture = await startCapture({ output, port: 0, durationMs: 25 });
    await capture.done;
    assert.equal(JSON.parse(fs.readFileSync(output, 'utf8').trim().split('\n').at(-1)).reason, 'duration elapsed');
  } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});

test('analysis reassembles every single-byte RX event and keeps connections separate', () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'sigma-probe-reassembly-'));
  try {
    const output = path.join(directory, 'capture.jsonl');
    const events = [{ schema: 'audio-dsp.sigma-tcp-capture.v1', event: 'metadata' }];
    for (const byte of writeFrame) events.push({ event: 'rx', connection: 1, hex: byte.toString(16).padStart(2, '0') });
    events.push({ event: 'rx', connection: 2, hex: readFrame.toString('hex') });
    events.push({ event: 'end' });
    fs.writeFileSync(output, events.map(event => JSON.stringify(event)).join('\n') + '\n');
    const report = analyzeCapture(output);
    assert.equal(report.streams.length, 2);
    assert.equal(report.streams[0].frames[0].hex, writeFrame.toString('hex'));
    assert.equal(report.streams[1].frames[0].hex, readFrame.toString('hex'));
  } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});

test('capture rejects a second client without dropping the active client', async context => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'sigma-probe-client-'));
  const output = path.join(directory, 'capture.jsonl');
  const capture = await startCapture({ output, port: 0, durationMs: 5000 });
  const client = net.createConnection({ host: '127.0.0.1', port: capture.port });
  let second;
  context.after(async () => { client.destroy(); second?.destroy(); await capture.close(); fs.rmSync(directory, { recursive: true, force: true }); });
  await once(client, 'connect');
  second = net.createConnection({ host: '127.0.0.1', port: capture.port });
  const secondClosed = once(second, 'close');
  await secondClosed;
  const received = once(client, 'data');
  capture.send('0102');
  const [bytes] = await received;
  assert.equal(bytes.toString('hex'), '0102');
  assert.match(fs.readFileSync(output, 'utf8'), /single-client limit/);
});


test('SigmaStudio+ target label is recorded without claiming identity or protocol verification', async () => {
  const directory = fs.mkdtempSync(path.join(os.tmpdir(), 'sigma-probe-plus-'));
  try {
    const output = path.join(directory, 'capture.jsonl');
    const capture = await startCapture({ output, port: 0, target: 'SigmaStudio+ 3.4.0' });
    await capture.close('metadata test only');
    const report = analyzeCapture(output);
    assert.equal(report.captureMetadata.target, 'SigmaStudio+ 3.4.0');
    assert.equal(report.captureMetadata.targetSource, 'operator-label-not-client-verification');
    assert.equal(report.decoderProfile, 'sigmastudio-plus-3.4.0-documented-rw');
    assert.match(report.captureMetadata.evidence, /identity unverified/);
    assert.deepEqual(report.streams, []);
  } finally { fs.rmSync(directory, { recursive: true, force: true }); }
});

test('capture rejects invalid target labels before creating evidence files', async () => {
  for (const target of ['', '  ', 340, 'x'.repeat(257)]) {
    await assert.rejects(startCapture({ output: 'unused.jsonl', target }), /Invalid target label/);
  }
});


test('undocumented 3.4.0 native prelude is retained without guessing its length or resynchronizing', () => {
  const prefix = Buffer.from('1b00400600010101000001', 'hex');
  for (const bytes of [prefix.subarray(0, 1), prefix, Buffer.concat([prefix, writeFrame])]) {
    const report = inspectStream(bytes);
    assert.deepEqual(report.frames, []);
    assert.equal(report.remainingHex, bytes.toString('hex'));
    assert.match(report.issue, /0x1b.*NOT supported/);
  }
});

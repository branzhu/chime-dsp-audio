// Passive offline integration tests against real TShark, not a mocked Lua API.
// Generates Ethernet/IPv4/TCP PCAPs locally; never captures or contacts a device.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const output = path.join(root, 'tmp/sigma-wireshark-tests');
fs.mkdirSync(output, { recursive: true });
const lua = path.join(root, 'tools/wireshark/sigmadsp.lua');
// Isolate user preferences/plugins; safe even after the production plugin is installed.
const config = path.join(output, 'isolated-config');
fs.mkdirSync(config, { recursive: true });
const tsharkEnv = { ...process.env, WIRESHARK_CONFIG_DIR: config };
const tshark = process.env.TSHARK ?? (process.platform === 'win32' ? 'C:/Program Files/Wireshark/tshark.exe' : 'tshark');
function run(args) {
  const r = spawnSync(tshark, args, { encoding: 'utf8', env: tsharkEnv, windowsHide: true, maxBuffer: 32 * 1024 * 1024, timeout: 60000 });
  if (r.error) throw r.error;
  assert.equal(r.status, 0, r.stderr);
  assert.doesNotMatch(r.stderr, /Lua.*(?:Error|error)|stack traceback|dissector bug/i);
  return r.stdout;
}
function checksum(bytes) {
  let n = 0;
  for (let i = 0; i < bytes.length; i += 2) n += (bytes[i] << 8) | (bytes[i + 1] ?? 0);
  while (n >>> 16) n = (n & 65535) + (n >>> 16);
  return (~n) & 65535;
}
class Capture {
  packets = [];
  nextPort = 40000;
  packet(src, dst, sport, dport, seq, ack, flags, data = Buffer.alloc(0), truncate = 0) {
    const ip = Buffer.alloc(20); ip[0] = 0x45; ip.writeUInt16BE(40 + data.length, 2); ip[8] = 64; ip[9] = 6;
    Buffer.from(src).copy(ip, 12); Buffer.from(dst).copy(ip, 16); ip.writeUInt16BE(checksum(ip), 10);
    const tcp = Buffer.alloc(20); tcp.writeUInt16BE(sport, 0); tcp.writeUInt16BE(dport, 2);
    tcp.writeUInt32BE(seq, 4); tcp.writeUInt32BE(ack, 8); tcp[12] = 0x50; tcp[13] = flags; tcp.writeUInt16BE(65535, 14);
    const pseudo = Buffer.concat([Buffer.from(src), Buffer.from(dst), Buffer.from([0, 6]), Buffer.alloc(2)]);
    pseudo.writeUInt16BE(20 + data.length, 10); tcp.writeUInt16BE(checksum(Buffer.concat([pseudo, tcp, data])), 16);
    const eth = Buffer.from('0200000000020200000000010800', 'hex');
    const packet = Buffer.concat([eth, ip, tcp, data]);
    this.packets.push({ packet: truncate ? packet.subarray(0, packet.length - truncate) : packet, length: packet.length });
    return this.packets.length;
  }
  connect(port = 8086) {
    const cap = this, a = [192, 0, 2, 10], b = [192, 0, 2, 20], client = this.nextPort++;
    let tx = 1000, rx = 5000;
    cap.packet(a, b, client, port, tx++, 0, 2);
    cap.packet(b, a, port, client, rx++, tx, 0x12);
    cap.packet(a, b, client, port, tx, rx, 0x10);
    return {
      send(data, server = false, truncate = 0) {
        const n = server ? cap.packet(b, a, port, client, rx, tx, 0x18, data, truncate)
          : cap.packet(a, b, client, port, tx, rx, 0x18, data, truncate);
        if (server) rx += data.length; else tx += data.length;
        return n;
      },
      ack(server=false) { return server ? cap.packet(b,a,port,client,rx,tx,0x10) : cap.packet(a, b, client, port, tx, rx, 0x10); },
      close(rst = true, server = true) {
        return server ? cap.packet(b, a, port, client, rx, tx, rst ? 0x14 : 0x11)
          : cap.packet(a, b, client, port, tx, rx, rst ? 0x14 : 0x11);
      },
      retransmit(data, server = false) {
        return server ? cap.packet(b, a, port, client, rx - data.length, tx, 0x18, data)
          : cap.packet(a, b, client, port, tx - data.length, rx, 0x18, data);
      },
    };
  }
  save(name) {
    const header = Buffer.alloc(24); header.writeUInt32LE(0xa1b2c3d4); header.writeUInt16LE(2, 4); header.writeUInt16LE(4, 6);
    header.writeUInt32LE(262144, 16); header.writeUInt32LE(1, 20);
    const parts = [header];
    this.packets.forEach(({ packet, length }, i) => {
      const h = Buffer.alloc(16); h.writeUInt32LE(1700000000 + Math.floor(i / 1000), 0); h.writeUInt32LE((i % 1000) * 1000, 4);
      h.writeUInt32LE(packet.length, 8); h.writeUInt32LE(length, 12); parts.push(h, packet);
    });
    const file = path.join(output, `${name}.pcap`); fs.writeFileSync(file, Buffer.concat(parts)); return file;
  }
}
function frame(cmd, addr, dataOrLength, { chip = 0, flag = 0, channel = 0, status = 0 } = {}) {
  const count = typeof dataOrLength === 'number' ? dataOrLength : dataOrLength.length;
  const b = Buffer.alloc(cmd === 10 ? 14 : 14 + count); b[0] = cmd;
  const write = cmd === 9; b.writeUInt32BE(b.length, write ? 3 : 1); b[write ? 7 : 5] = chip;
  b.writeUInt32BE(count, write ? 8 : 6); b.writeUInt16BE(addr, write ? 12 : 10);
  if (write) { b[1] = flag; b[2] = channel; } else if (cmd === 11) b[12] = status;
  if (cmd !== 10 && typeof dataOrLength !== 'number') dataOrLength.copy(b, 14);
  return b;
}
const init = Buffer.from('1b00400600010101000001', 'hex');
const reset = frame(9, 0xf890, Buffer.from([0, 1]));
const read = frame(10, 0xf405, 2);
const reply = frame(11, 0xf405, Buffer.from([0, 3]));
const fields = ['frame.number', 'tcp.stream', 'sigmadsp.cmd', 'sigmadsp.chip', 'sigmadsp.addr', 'sigmadsp.data_len',
  'sigmadsp.register', 'sigmadsp.value16', 'sigmadsp.core_status', 'sigmadsp.request_in', 'sigmadsp.response_in',
  'sigmadsp.match', 'sigmadsp.response_time', 'sigmadsp.close', 'sigmadsp.pending_reads', 'sigmadsp.last_command_in',
  'sigmadsp.observed_init', 'sigmadsp.tcp_ack_only', '_ws.col.Protocol', 'sigmadsp.partial_bytes_needed', '_ws.expert.message', '_ws.col.Info'];
function decode(file, options = [], twoPass = true, filter = 'sigmadsp') {
  const text = run(['-n', ...(twoPass ? ['-2'] : []), '-X', `lua_script:${lua}`, ...options, '-r', file,
    '-Y', filter, '-T', 'fields', '-E', 'separator=/t', '-E', 'quote=n', '-E', 'occurrence=a', '-E', 'aggregator=;',
    ...fields.flatMap(f => ['-e', f])]);
  assert.doesNotMatch(text, /Lua Error|Dissector bug|stack traceback/i);
  return text.trimEnd().split(/\r?\n/).filter(Boolean).map(line => Object.fromEntries(line.split('\t').map((v, i) => [fields[i], v])));
}
function rowFor(rows, n) { return rows.find(r => Number(r['frame.number']) === n); }
const hasCmd = (r, cmd) => r['sigmadsp.cmd'].split(';').includes(`0x${cmd.toString(16).padStart(2, '0')}`);
let groups = 0;
function pass(label) { groups++; console.log(`PASS ${label}`); }

// Pin generated register names/access modes to the independent factual fixture.
const registerFacts = JSON.parse(fs.readFileSync(path.join(root, 'software/tests/fixtures/sigma/registers.json'), 'utf8'));
const expected = new Map(registerFacts.registers.map(r => [parseInt(r.address, 16), [r.name, r.access]]));
const actual = new Map([...fs.readFileSync(lua, 'utf8').matchAll(/\[0x([0-9a-f]{4})\]=\{"(\w+)","(RW|R)"\}/g)]
  .map(m => [parseInt(m[1], 16), [m[2], m[3]]]));
assert.equal(expected.size, 530); assert.deepEqual(actual, expected);
pass('530 register names/access modes exactly match Rev.D overview');

const demo = new Capture(), session = demo.connect();
const initNo = session.send(init), writeNo = session.send(reset), readNo = session.send(read), replyNo = session.send(reply, true);
session.ack(); const closeNo = session.close();
const demoFile = demo.save('demo');
const demoRows = decode(demoFile);
assert.equal(rowFor(demoRows, initNo)['sigmadsp.observed_init'], 'True');
assert.match(rowFor(demoRows, writeNo)['_ws.col.Info'], /SOFT_RESET.*0001.*Exit soft reset/);
assert.equal(rowFor(demoRows, writeNo)['sigmadsp.response_in'], '');
assert.equal(rowFor(demoRows, replyNo)['sigmadsp.core_status'], '3');
assert.match(rowFor(demoRows, replyNo)['_ws.col.Info'], /Sleep/);
assert.equal(rowFor(demoRows, readNo)['sigmadsp.response_in'], String(replyNo));
assert.equal(rowFor(demoRows, replyNo)['sigmadsp.request_in'], String(readNo));
assert.equal(rowFor(demoRows, replyNo)['sigmadsp.match'], 'True');
assert.ok(Number(rowFor(demoRows, replyNo)['sigmadsp.response_time']) > 0);
assert.equal(rowFor(demoRows, closeNo)['sigmadsp.close'], 'RST');
assert.equal(rowFor(demoRows, closeNo)['sigmadsp.pending_reads'], '0');
assert.equal(rowFor(demoRows, closeNo)['sigmadsp.last_command_in'], String(readNo));
assert.doesNotMatch(demoRows.map(r => r['_ws.expert.message']).join('\n'), /Outside current|Invalid frame/);
const verbose = run(['-n', '-2', '-X', `lua_script:${lua}`, '-r', demoFile, '-Y', 'sigmadsp', '-V']);
fs.writeFileSync(path.join(output, 'demo-decoded.txt'), verbose);
pass('exact observed INIT, F890 reset exit, F405 Sleep, request/reply links/RTT, no write ACK, RST context');

const ackCapture=new Capture(), ackSession=ackCapture.connect();
const ackWriteNo=ackSession.send(frame(9,0x18,Buffer.from('00333333','hex')));
const bridgeAckNo=ackSession.ack(true), hostAckNo=ackSession.ack();
ackSession.send(read);const readAckNo=ackSession.ack(true);
const realReplyNo=ackSession.send(reply,true);
const ackRows=decode(ackCapture.save('bridge-ack-only'));
for(const n of [bridgeAckNo,readAckNo]) {
  const r=rowFor(ackRows,n);assert.equal(r['sigmadsp.tcp_ack_only'],'True');
  assert.equal(r['sigmadsp.cmd'],'');assert.equal(r['_ws.col.Protocol'],'TCP');
  // TShark's exported _ws.col.Info field is cached before postdissectors;
  // the actual summary/GUI Info column is checked separately below.
}
const ackSummary=run(['-n','-2','-X',`lua_script:${lua}`,'-r',path.join(output,'bridge-ack-only.pcap'),'-Y','sigmadsp.tcp_ack_only']);
assert.equal((ackSummary.match(/Bridge TCP ACK only; no application response/g)??[]).length,2);
assert.equal(rowFor(ackRows,hostAckNo),undefined);
assert.equal(rowFor(ackRows,ackWriteNo)['sigmadsp.response_in'],'');
assert.equal(rowFor(ackRows,realReplyNo)['sigmadsp.cmd'],'0x0b');
assert.equal(rowFor(ackRows,realReplyNo)['sigmadsp.tcp_ack_only'],'');
pass('bridge ACK-only gets explicit transport annotation, stays TCP, never masquerades as 0B or write success; host ACK excluded');

const splits = new Capture(), endings = [];
for (const pdu of [init, reset, read, reply]) {
  for (let split = 1; split < pdu.length; split++) {
    const s = splits.connect(), isReply = pdu[0] === 11;
    if (isReply) s.send(read);
    s.send(pdu.subarray(0, split), isReply);
    const n = s.send(pdu.subarray(split), isReply); endings.push([n, pdu[0]]);
  }
}
const splitRows = decode(splits.save('all-splits'));
for (const [n, cmd] of endings) assert.ok(hasCmd(rowFor(splitRows, n) ?? { 'sigmadsp.cmd': '' }, cmd), `split PDU ${n}/${cmd}`);
const bytewise = splits.connect(); for (const byte of Buffer.concat([init, reset, read])) bytewise.send(Buffer.from([byte]));
bytewise.send(reply, true);
assert.equal(decode(splits.save('all-splits-bytewise')).filter(r => hasCmd(r, 11)).length, 16);
pass('every TCP split of INIT/write/read/reply plus single-byte stream');

const combined = new Capture(), co = combined.connect();
const read2 = frame(10, 0xf000, 2, { chip: 0x70 }), reply2 = frame(11, 0xf000, Buffer.from([0, 0x60]), { chip: 0x70 });
const coRequest = co.send(Buffer.concat([init, reset, read, read2]));
const coReply = co.send(Buffer.concat([reply, reply2]), true);
co.retransmit(Buffer.concat([reply, reply2]), true);
const partial = combined.connect(); partial.send(Buffer.concat([reset, read.subarray(0, 4)]));
const partialNo = partial.send(read.subarray(4)); const partialReplyNo = partial.send(reply, true);
const combinedRows = decode(combined.save('coalesced'));
assert.equal(rowFor(combinedRows, coRequest)['sigmadsp.cmd'], '0x1b;0x09;0x0a;0x0a');
assert.equal(rowFor(combinedRows, coRequest)['sigmadsp.response_in'], `${coReply};${coReply}`);
assert.equal(rowFor(combinedRows, coReply)['sigmadsp.chip'], '0x00;0x70');
assert.equal(rowFor(combinedRows, coReply)['sigmadsp.request_in'], `${coRequest};${coRequest}`);
assert.equal(rowFor(combinedRows, partialReplyNo)['sigmadsp.request_in'], String(partialNo));
pass('coalesced INIT/writes/reads/replies, mixed wire IDs, partial tail and TCP retransmission');

const multi = new Capture(), m1 = multi.connect(), m2 = multi.connect();
const req1 = m1.send(read), req2 = m2.send(read2); const res2 = m2.send(reply2, true), res1 = m1.send(reply, true);
m1.close(false, false); const waiting = multi.connect(); const waitingNo = waiting.send(read); const waitingClose = waiting.close();
const multiRows = decode(multi.save('connections'));
assert.equal(rowFor(multiRows, res1)['sigmadsp.request_in'], String(req1));
assert.equal(rowFor(multiRows, res2)['sigmadsp.request_in'], String(req2));
assert.equal(rowFor(multiRows, waitingClose)['sigmadsp.pending_reads'], '1');
assert.equal(rowFor(multiRows, waitingClose)['sigmadsp.last_command_in'], String(waitingNo));
assert.ok(multiRows.some(r => r['sigmadsp.close'] === 'FIN'));
for (const twoPass of [false, true]) {
  const r = decode(demoFile, [], twoPass); assert.equal(rowFor(r, replyNo)['sigmadsp.request_in'], String(readNo));
}
pass('independent concurrent TCP streams, FIN/RST pending reads, first/second-pass redissection');

const mismatch = new Capture();
for (const options of [{ chip: 0x70 }, { addr: 0xf000 }, { count: 4 }]) {
  const s = mismatch.connect(); s.send(read);
  s.send(frame(11, options.addr ?? 0xf405, options.count ?? Buffer.from([0, 3]), { chip: options.chip ?? 0 }), true);
}
const orphan = mismatch.connect(); orphan.send(reply, true);
const mismatchRows = decode(mismatch.save('mismatches'));
assert.equal(mismatchRows.filter(r => r['sigmadsp.match'] === 'False').length, 3);
assert.equal(mismatchRows.filter(r => /FIFO request/.test(r['_ws.expert.message'])).length, 3);
assert.ok(mismatchRows.some(r => /no earlier observed request/.test(r['_ws.expert.message'])));
pass('response chip/address/length mismatches annotated without treating chip00/70 as identical; orphan response');

const bad = new Capture();
for (const hex of ['ff', '1b00400600010101000002', '090000ffffffff0000000002f8900001',
  '0a0000000e00fffffffff0000000', '0900000000000f0000000002f8900001', '0a0000000e0000000000f0000000']) {
  const s = bad.connect(); s.send(Buffer.concat([Buffer.from(hex, 'hex'), reset])); s.send(reset);
}
const reserved = bad.connect(); const nonzero = Buffer.from(read); nonzero[13] = 1; reserved.send(nonzero);
const failure = bad.connect(); failure.send(read); failure.send(frame(11, 0xf405, Buffer.alloc(0), { status: 1 }), true);
const failedData = bad.connect(); const failedDataNo=failedData.send(frame(11, 0xf405, Buffer.from([0, 3]), {status: 1}), true);
const zeroSuccess = bad.connect(); zeroSuccess.send(frame(11, 0xf405, Buffer.alloc(0)), true);
const policy = bad.connect(); policy.send(Buffer.concat([
  frame(9, 0xf004, Buffer.from([0, 1])), frame(9, 0xf007, Buffer.from([0, 1])),
  frame(9, 0xf000, Buffer.from([1])), frame(9, 0x1fff, Buffer.alloc(8)),
  frame(9, 0x14, Buffer.alloc(4), { flag: 1 }), frame(9, 0x13, Buffer.alloc(4), { flag: 1 }),
  frame(9, 0xf000, Buffer.alloc(2), { flag: 2, channel: 1, chip: 0xa0 }),
]));
const badRows = decode(bad.save('malformed'));
assert.equal(badRows.filter(r => /undecoded after invalid/i.test(r['_ws.col.Info'])).length, 6);
assert.equal(rowFor(badRows, failedDataNo)['sigmadsp.core_status'], '');
assert.doesNotMatch(rowFor(badRows, failedDataNo)['_ws.col.Info'], /Sleep/);
assert.ok(badRows.some(r => /Zero-byte successful/.test(r['_ws.expert.message'])));
assert.ok(badRows.some(r => /Failure\/unknown/.test(r['_ws.expert.message'])));
assert.ok(badRows.some(r => /Nonzero reserved/.test(r['_ws.expert.message'])));
assert.ok(badRows.some(r => /reserved, read-only-on-write/.test(r['_ws.expert.message'])));
assert.ok(badRows.some(r => /outside this minimal/.test(r['_ws.expert.message'])));
pass('unknown/mutated INIT/bad lengths fail bounded with no guessed resync; status/reserved/alignment/RO/gaps/policy notes');

const big = new Capture(), large = big.connect();
const payload = Buffer.alloc(32768, 0xa5), bigWrite = frame(9, 0, payload), bigRead = frame(10, 0, payload.length), bigReply = frame(11, 0, payload);
for (let i = 0; i < bigWrite.length; i += 1300) large.send(bigWrite.subarray(i, i + 1300));
const bigReadNo = large.send(bigRead); let bigReplyNo;
for (let i = 0; i < bigReply.length; i += 1300) bigReplyNo = large.send(bigReply.subarray(i, i + 1300), true);
const max = big.connect(); const maxPdu = frame(9, 0, 65536);
for (let i = 0; i < maxPdu.length; i += 1400) max.send(maxPdu.subarray(i, i + 1400));
const bigRows = decode(big.save('large'));
assert.equal(rowFor(bigRows, bigReadNo)['sigmadsp.response_in'], String(bigReplyNo));
assert.equal(rowFor(bigRows, bigReplyNo)['sigmadsp.data_len'], '32768');
assert.ok(bigRows.some(r => hasCmd(r, 9) && r['sigmadsp.data_len'] === '65536'));
assert.ok(decode(big.save('preview-zero'), ['-o', 'sigmadsp.words:0']).every(r => r['sigmadsp.value16'] === ''));
pass('32KB transfer/reassembly/response links, 64KiB protocol limit, bounded/disabled decoded-word previews');

const trunc = new Capture(), snap = trunc.connect(); const snapNo = snap.send(reset, false, 1);
for (let i = 1; i < reset.length; i++) { const s = trunc.connect(); s.send(reset.subarray(0, i)); }
const interrupted=trunc.connect(); interrupted.send(reset.subarray(0, 15)); const interruptedClose=interrupted.close();
const truncFile = trunc.save('truncated');
assert.equal(rowFor(decode(truncFile),interruptedClose)['sigmadsp.partial_bytes_needed'], '1');
assert.match(rowFor(decode(truncFile), snapNo)['_ws.expert.message'], /capture truncated/);
assert.ok(decode(truncFile, ['-o', 'tcp.desegment_tcp_streams:FALSE']).some(r => /reassembly unavailable/.test(r['_ws.expert.message'])));
pass('snaplen truncation, every EOF prefix, disabled TCP reassembly without Lua bounds exceptions');

const custom = new Capture(), cs = custom.connect(18086); cs.send(read); cs.send(reply, true);
const customFile = custom.save('custom-port');
assert.equal(decode(customFile).length, 0);
assert.equal(decode(customFile, ['-d', 'tcp.port==18086,sigmadsp']).filter(r => hasCmd(r, 11)).length, 1);
assert.equal(decode(customFile, ['-o', 'sigmadsp.port:18086']).filter(r => hasCmd(r, 11)).length, 1);
assert.equal(decode(demoFile, ['-o', 'sigmadsp.port:0']).length, 0);
assert.equal(decode(demoFile, [], true, 'sigmadsp.addr == 0xf405 && sigmadsp.cmd == 0x0b && sigmadsp.core_status == 3').length, 1);
pass('port preference, disable binding, Decode As and display filters');

const native = new Capture();
const evidence = JSON.parse(fs.readFileSync(path.join(root, 'software/tests/fixtures/sigma/zero-chip-alias.json'), 'utf8'));
for (const trial of evidence.sessions) {
  const s=native.connect(); for(const hex of trial.frames) s.send(Buffer.from(hex,'hex'));
  s.send(Buffer.from(trial.mockResponseHex,'hex'),true);
}
const nativeRows=decode(native.save('native-golden'));
assert.equal(nativeRows.filter(r=>hasCmd(r,11)).length,evidence.sessions.length);
assert.ok(nativeRows.some(r=>r['sigmadsp.chip']==='0x38'));
assert.ok(nativeRows.some(r=>/0x3412/.test(r['sigmadsp.addr']) && r['sigmadsp.match']==='False'));
pass('all pinned native3.4.0 loopback sessions, nondefault chip/channel and reversed-address negative fixture');

// TCP reassembly owns sequence ordering; the Lua parser must not concatenate packet arrival order.
const reordered=new Capture(), ro=reordered.connect();
const start=reordered.packets.length;
ro.send(reset.subarray(0,5));ro.send(reset.subarray(5,10));ro.send(reset.subarray(10));
[reordered.packets[start+1],reordered.packets[start+2]]=[reordered.packets[start+2],reordered.packets[start+1]];
const roRows=decode(reordered.save('out-of-order'),['-o','tcp.reassemble_out_of_order:TRUE']);
assert.equal(roRows.filter(r=>hasCmd(r,9)).length,1);
assert.ok(roRows.some(r=>/Exit soft reset/.test(r['_ws.col.Info'])));
pass('out-of-order TCP segments reassembled by Wireshark, not capture arrival order');

const fuzz = new Capture(); let seed = 0x145134;
function random() { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; return seed >>> 0; }
for (let i = 0; i < 180; i++) {
  const s = fuzz.connect(); const b = Buffer.alloc(1 + random() % 110);
  for (let j = 0; j < b.length; j++) b[j] = random() & 255;
  if (i % 3 === 0) b[0] = [9, 10, 11, 27][i % 4];
  s.send(b); s.close();
}
decode(fuzz.save('fuzz')); pass('180 deterministic random malformed/truncated TCP streams, no Lua/dissector exceptions');
const bounded = new Capture(), busy = bounded.connect();
for (let i=0; i<4097; i++) busy.send(read);
const limitedReply = busy.send(reply,true); busy.close();
const boundedRows=decode(bounded.save('bounded-queue'));
assert.equal(rowFor(boundedRows,limitedReply)['sigmadsp.request_in'], '');
assert.match(rowFor(boundedRows,limitedReply)['_ws.expert.message'], /Correlation budget/);
pass('pending-read overflow disables ambiguous association instead of silently linking wrong requests');
if (process.platform==='win32') {
  const install=path.join(root,'scripts/install-sigma-wireshark.ps1');
  const directory=path.join(output,`installer-${process.pid}`);
  const target=path.join(directory,'sigmadsp.lua');
  function installTest(args,status=0) {
    const r=spawnSync('powershell.exe',['-NoProfile','-ExecutionPolicy','Bypass','-File',install,'-PluginDirectory',directory,...args],
      {encoding:'utf8',windowsHide:true,timeout:15000});
    if(r.error) throw r.error;
    if(status===0) assert.equal(r.status,0,r.stderr); else assert.notEqual(r.status,0);
    return r.stdout;
  }
  installTest(['-WhatIf']);assert.equal(fs.existsSync(target),false);
  installTest([]);assert.deepEqual(fs.readFileSync(target),fs.readFileSync(lua));
  assert.match(installTest([]),/Already installed/);
  fs.writeFileSync(target,'-- user-customized file\n');
  installTest([],1);assert.equal(fs.readFileSync(target,'utf8'),'-- user-customized file\n');
  installTest(['-Force']);assert.deepEqual(fs.readFileSync(target),fs.readFileSync(lua));
  pass('installer WhatIf/install/idempotency/refuse changed file/explicit Force, only in temporary workspace directory');
}
console.log(`All ${groups} SigmaStudio+ Wireshark integration groups passed.`);
console.log(`Synthetic demo (no hardware traffic): ${demoFile}`);
console.log(`Decoded demo: ${path.join(output, 'demo-decoded.txt')}`);

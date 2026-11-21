import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const evidenceRoot = path.join(root, 'software/tests/fixtures/sigma');
const plus = JSON.parse(fs.readFileSync(path.join(evidenceRoot, 'plus-3.4.0.json'), 'utf8'));
assert.equal(plus.schema, 'audio-dsp.sigmastudio-plus-wire-test.v1');
assert.equal(plus.driverSha256, '77ba8dfeb8364781ef5efa5a66efc2e09067c3751f4af0621c46f884eff0aca5');
assert.equal(plus.typesSha256, '80a1513f41167a99325d15b0f9e2627eec185577915d5af32a6887ea433dbb09');
assert.equal(plus.cases.length, 5);
assert.equal(plus.cases[3].responseAccepted, true);
assert.equal(plus.cases[4].responseAccepted, false);
assert.equal(plus.sessions.length, 3);
assert.equal(plus.sessions[0].frames[0], '1b00400600010101000001');
assert.equal(plus.sessions[2].channelArgument, 3);
assert.equal(plus.sessions[2].chipArgument, 0x38);
assert.equal(plus.sessions[2].responseAccepted, true);
const legacy = JSON.parse(fs.readFileSync(path.join(evidenceRoot, 'classic-4.7.json'), 'utf8'));
assert.equal(legacy.schema, 'audio-dsp.sigmastudio-wire-test.v1');
assert.equal(legacy.dllSha256, 'c04b854c5fcff1783c1cf00743e96267931d184f3f1e46bc040d18d7f049b07a');
assert.equal(legacy.cases.length, 5);
assert.equal(legacy.cases[3].matchedPackage, true);
assert.equal(legacy.cases[4].matchedPackage, false);
const names = ['native_write', 'native_flagged_write', 'native_read', 'native_response', 'native_response_reversed'];
function array(name, hex) {
  assert.match(hex, /^(?:[0-9a-f]{2})+$/);
  const bytes = Buffer.from(hex, 'hex');
  return `static const uint8_t ${name}[] = {${[...bytes].map(byte => `0x${byte.toString(16).padStart(2, '0')}`).join(',')}};`;
}
function run(command, args) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', windowsHide: true });
  process.stdout.write(result.stdout ?? '');
  process.stderr.write(result.stderr ?? '');
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${command} failed with status ${result.status}.`);
}
// Independent factual register fixture; internal datasheet copies are not required.
const registerFacts=JSON.parse(fs.readFileSync(path.join(evidenceRoot,'registers.json'),'utf8'));
const registerMap=new Map(registerFacts.registers.map(r=>[parseInt(r.address,16),r.access]));
assert.equal(registerMap.size,530);
const policy=fs.readFileSync(path.join(root,'software/main/include/bench_registers.inc'),'utf8');
const actualMap=new Map();
for(const m of policy.matchAll(/\{0x([0-9a-f]+),0x([0-9a-f]+),2,([^}]+)\}/g)) {
  const mode=(m[3].includes('READ')?'R':'')+(m[3].includes('WRITE')?'W':'');
  for(let address=parseInt(m[1],16);address<=parseInt(m[2],16);++address) {
    assert.equal(actualMap.has(address),false);actualMap.set(address,mode);
  }
}
assert.deepEqual([...actualMap].sort((a,b)=>a[0]-b[0]),[...registerMap].sort((a,b)=>a[0]-b[0]));
console.log('PASS bench register allowlist exactly matches 530 documented Rev.D registers/permissions.');

const compiler = process.env.CC ?? 'gcc';
const msvc = /^cl(?:\.exe)?$/i.test(path.basename(compiler));
for (const [name, evidence, defines] of [
  ['plus-3.4.0-rw', plus, []], ['classic-4.7-rw', legacy, ['SIGMA_TEST_CLASSIC47']],
]) {
  const build = path.join(root, 'tmp', 'sigma-bridge-tests', name);
  fs.mkdirSync(build, { recursive: true });
  const header = evidence.cases.map((entry, index) => array(names[index], entry.hex));
  header.push(array('native_connection_prefix', plus.sessions[0].frames[0]));
  header.push(array('native_channel_write', plus.sessions[2].frames[1]));
  header.push(array('native_channel_response', plus.sessions[2].mockResponseHex));
  fs.writeFileSync(path.join(build, 'native_frames.h'), header.join('\n') + '\n', 'utf8');
  const executable = path.join(build, process.platform === 'win32' ? 'sigma_bridge_tests.exe' : 'sigma_bridge_tests');
  const includes = ['software/components/sigma_tcp_bridge/include', 'software/components/dsp_control/include', build];
  const sources = ['software/tests/bridge/test_bridge.c', 'software/components/sigma_tcp_bridge/sigma_protocol.c',
    'software/components/sigma_tcp_bridge/sigma_tcp_bridge.c', 'software/components/dsp_control/dsp_transfer.c'];
  const args = msvc
    ? ['/nologo', '/std:c11', '/W4', '/WX', ...defines.map(value => `/D${value}`), ...includes.map(dir => `/I${dir}`),
      ...sources, `/Fo${build}${path.sep}`, `/Fe${executable}`, '/link', '/INCREMENTAL:NO']
    : ['-std=c11', '-O1', '-g', '-Wall', '-Wextra', '-Werror', '-Wconversion', '-Wsign-conversion', '-pedantic',
      ...defines.map(value => `-D${value}`), ...includes.map(dir => `-I${dir}`), ...sources, '-o', executable];
  console.log(`Testing pinned ${name} R/W subset (NOT full native connection/download compatibility)`);
  run(compiler, args);
  run(executable, []);
}

// Compile the same portable policy used by the ESP32 bench image.
{
  const build=path.join(root,'tmp','sigma-bridge-tests','bench-dsp');
  fs.mkdirSync(build,{recursive:true});
  const executable=path.join(build,process.platform==='win32'?'bench_dsp_tests.exe':'bench_dsp_tests');
  const includes=['software/main/include','software/components/sigma_tcp_bridge/include','software/components/dsp_control/include'];
  const sources=['software/tests/bridge/test_bench_dsp.c','software/main/bench_dsp.c','software/components/dsp_control/dsp_transfer.c',
    'software/components/sigma_tcp_bridge/sigma_protocol.c','software/components/sigma_tcp_bridge/sigma_tcp_bridge.c'];
  const args=msvc
    ? ['/nologo','/std:c11','/W4','/WX',...includes.map(dir=>`/I${dir}`),...sources,`/Fo${build}${path.sep}`,`/Fe${executable}`,'/link','/INCREMENTAL:NO']
    : ['-std=c11','-O1','-g','-Wall','-Wextra','-Werror','-Wconversion','-Wsign-conversion','-pedantic',...includes.map(dir=>`-I${dir}`),...sources,'-o',executable];
  run(compiler,args);run(executable,[]);
}

// Exercise real owner callback bodies plus hand-back source invariants.
await import('./test-ss-audition.mjs');

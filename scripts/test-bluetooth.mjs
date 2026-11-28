import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.join(root,'tmp','bluetooth-tests');fs.mkdirSync(build,{recursive:true});
const cc=process.env.CC??'gcc',msvc=/^cl(?:\.exe)?$/i.test(path.basename(cc));

// The adapter components must not regain the retired decoder trees or metadata.
for(const [component,legacy] of [
 ['chime_ldac',['src','chime_ldac_core.c','linker.lf','LICENSE','UPSTREAM.json']],
 ['chime_lhdc',['src','LICENSE','NOTICE','UPSTREAM-README.md','UPSTREAM.json']]
]){
 for(const entry of legacy)assert.equal(fs.existsSync(path.join(root,'software/components',component,entry)),false,
  `Retired decoder artifact reintroduced: ${component}/${entry}`);
 const wrapper=fs.readFileSync(path.join(root,'software/components',component,`${component}.c`),'utf8');
 assert.match(wrapper,/chime_reference_open/);assert.match(wrapper,/chime_reference_decode/);
}
console.log('PASS decoder layout: only current LDAC/LHDC reference adapters; no retired decoder artifacts');

function run(cmd,args){const r=spawnSync(cmd,args,{cwd:root,encoding:'utf8',windowsHide:true,timeout:180000});process.stdout.write(r.stdout??'');process.stderr.write(r.stderr??'');if(r.error)throw r.error;assert.equal(r.status,0);return r.stdout??'';}
function test(name,files,includes,defines=[],flags=[]){includes=[...includes,'software/components/chime_codec_reference'];const exe=path.join(build,`${name}${process.platform==='win32'?'.exe':''}`);
 run(cc,msvc?[...flags,...defines.map(d=>`/D${d}`),'/nologo','/std:c11','/experimental:c11atomics','/W4','/WX',...includes.map(p=>`/I${p}`),...files,`/Fo${build}${path.sep}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO']
 :[...flags,...defines.map(d=>`-D${d}`),'-std=c11','-Wall','-Wextra','-Werror',...includes.map(p=>`-I${p}`),...files,'-o',exe,'-lm']);return run(exe,[]);}
test('source',['software/tests/bluetooth/test_chime_source.c','software/main/chime_source.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c'],['software/main/include']);
test('bt-bursts',['software/tests/bluetooth/test_chime_bt_bursts.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c'],['software/main/include'],[],[msvc?'/O2':'-O2']);
test('bluetooth',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c','software/main/chime_bt_delay.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include']);

test('bluetooth-external',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c','software/main/chime_bt_delay.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1']);
test('bluetooth-fault-log',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c','software/main/chime_bt_delay.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CHIME_BT_CAPTURE_LOG=1']);
test('bt-decoder',['software/tests/bluetooth/test_chime_bt_decoder.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1']);
test('bluetooth-vendor',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c','software/main/chime_bt_delay.c','software/main/chime_bt_vendor.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1']);
test('bt-decoder-vendor',['software/tests/bluetooth/test_chime_bt_decoder.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include','software/components/chime_ldac'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1']);
test('bt-decoder-unicore',['software/tests/bluetooth/test_chime_bt_decoder.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_FREERTOS_UNICORE=1']);
// Nominal downstream estimate is deliberately coupled to today's I2S layout.
const audioSource=fs.readFileSync(path.join(root,'software/main/bench_audio.c'),'utf8');
assert.match(audioSource,/#define DMA_DESCRIPTORS 4U/);assert.match(audioSource,/#define DMA_FRAMES 128U/);
assert.match(audioSource,/sample_rate_hz=48000/);
test('bt-delay',['software/tests/bluetooth/test_chime_bt_delay.c','software/main/chime_bt_delay.c'],['software/main/include']);
test('bt-vendor-wire',['software/tests/bluetooth/test_chime_bt_vendor.c','software/main/chime_bt_vendor.c'],['software/main/include']);

// LHDC remains an explicit feature gate: test both disabled and seven-SEP builds.
const lhdcDefines=['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1','CONFIG_CHIME_BT_LHDC_V5=1'];
test('bluetooth-lhdc',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c','software/main/chime_bt_delay.c','software/main/chime_bt_vendor.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],lhdcDefines);
test('bt-decoder-lhdc',['software/tests/bluetooth/test_chime_bt_decoder.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include','software/components/chime_ldac','software/components/chime_lhdc'],lhdcDefines);
test('bt-lhdc-wire',['software/tests/bluetooth/test_chime_bt_vendor.c','software/main/chime_bt_vendor.c'],['software/main/include'],['CONFIG_CHIME_BT_LHDC_V5=1']);

// Exercise the actual owner-side formatter, also with LHDC/external mode off.
const bridgeSource=fs.readFileSync(path.join(root,'software/main/bench_bridge.c'),'utf8');
const monitorStart=bridgeSource.indexOf('static void bluetooth_monitor_log(void)');
const monitorEnd=bridgeSource.indexOf('static void sound_monitor_log(',monitorStart);
assert(monitorStart>=0&&monitorEnd>monitorStart);
const monitor=bridgeSource.slice(monitorStart,monitorEnd);
assert.doesNotMatch(monitor,/pcm_render|pcm_write|i2c|malloc|vTaskDelay|codec_open/);
assert.match(bridgeSource,/if\(monitor_now>=audio_monitor_at\)[\s\S]*?bluetooth_monitor_log\(\)[\s\S]*?audio_monitor_at=monitor_now\+1000000ULL/);
fs.writeFileSync(path.join(build,'bt_monitor.inc'),monitor);
for(const [name,defines] of [['internal',[]],['six',['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1']],['seven',lhdcDefines]])
 test(`bt-monitor-${name}`,['software/tests/bluetooth/test_chime_bt_monitor.c'],
  [build,'software/components/chime_codec_reference','software/tests/bluetooth/stubs','software/main/include'],defines);

// Real codec objects/fixtures now use only the selected S31 import.

if(process.env.IDF_PATH){
 run(process.env.PYTHON??'python',['-X','utf8','scripts/test-bluetooth-sdk-patch.py',process.env.IDF_PATH]);
 for(const mode of [0,1])test(`sdk-delay-${mode}`,['software/tests/bluetooth/test_sdk_delay.c'],[build],[`DELAY_RECOVERY=${mode}`]);
 test('vendor-avdt',['software/tests/bluetooth/test_vendor_avdt.c','software/main/chime_bt_vendor.c'],['software/main/include',build],['CONFIG_CHIME_BT_LHDC_V5=1']);
}else console.log('SKIP pinned SDK/actual AVDTP hook tests: set IDF_PATH and PYTHON to enable');

// Bounded LDAC48k producer/consumer architecture; host execution is functional,
// not CPU timing or actual SIMD evidence.
test('bluetooth-ldac-pipeline',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c','software/main/chime_bt_delay.c','software/main/chime_bt_vendor.c','software/main/chime_bt_ready.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1','CONFIG_CHIME_LDAC_PIPELINE_TEST=1']);

run(process.env.PYTHON??'python',['scripts/test-s31-codecs.py']);

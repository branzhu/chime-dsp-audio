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
function test(name,files,includes,defines=[],flags=[]){const exe=path.join(build,`${name}${process.platform==='win32'?'.exe':''}`);
 run(cc,msvc?[...flags,...defines.map(d=>`/D${d}`),'/nologo','/std:c11','/experimental:c11atomics','/W4','/WX',...includes.map(p=>`/I${p}`),...files,`/Fo${build}${path.sep}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO']
 :[...flags,...defines.map(d=>`-D${d}`),'-std=c11','-Wall','-Wextra','-Werror',...includes.map(p=>`-I${p}`),...files,'-o',exe,'-lm']);return run(exe,[]);}
test('source',['software/tests/bluetooth/test_chime_source.c','software/main/chime_source.c','software/main/chime_bt_pcm.c'],['software/main/include']);
test('bt-bursts',['software/tests/bluetooth/test_chime_bt_bursts.c','software/main/chime_bt_pcm.c'],['software/main/include'],[],[msvc?'/O2':'-O2']);
test('bluetooth',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_delay.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include']);

test('bluetooth-external',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_delay.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1']);
test('bt-decoder',['software/tests/bluetooth/test_chime_bt_decoder.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1']);
test('bluetooth-vendor',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_delay.c','software/main/chime_bt_vendor.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1']);
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
test('bluetooth-lhdc',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_delay.c','software/main/chime_bt_vendor.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],lhdcDefines);
test('bt-decoder-lhdc',['software/tests/bluetooth/test_chime_bt_decoder.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include','software/components/chime_ldac','software/components/chime_lhdc'],lhdcDefines);
test('bt-lhdc-wire',['software/tests/bluetooth/test_chime_bt_vendor.c','software/main/chime_bt_vendor.c'],['software/main/include'],['CONFIG_CHIME_BT_LHDC_V5=1']);

// Exercise the actual owner-side formatter, also with LHDC/external mode off.
const bridgeSource=fs.readFileSync(path.join(root,'software/main/bench_bridge.c'),'utf8');
const monitorStart=bridgeSource.indexOf('static void bluetooth_monitor_log(void)');
const monitorEnd=bridgeSource.indexOf('static void sound_monitor_log(',monitorStart);
assert(monitorStart>=0&&monitorEnd>monitorStart);
const monitor=bridgeSource.slice(monitorStart,monitorEnd);
assert.doesNotMatch(monitor,/pcm_render|pcm_write|i2c|malloc|vTaskDelay|codec_open/);
assert.match(bridgeSource,/if\(monitor_now>=audio_monitor_at\).*bluetooth_monitor_log\(\).*audio_monitor_at=monitor_now\+1000000ULL/);
fs.writeFileSync(path.join(build,'bt_monitor.inc'),monitor);
for(const [name,defines] of [['internal',[]],['six',['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1']],['seven',lhdcDefines]])
 test(`bt-monitor-${name}`,['software/tests/bluetooth/test_chime_bt_monitor.c'],
  [build,'software/components/chime_codec_reference','software/tests/bluetooth/stubs','software/main/include'],defines);

// Compile third-party source separately: its upstream warning policy is not ours.
const aptxObject=path.join(build,msvc?'freeaptx-real.obj':'freeaptx-real.o');
run(cc,msvc?['/nologo','/std:c11','/O2','/W3','/c','/Isoftware/components/chime_freeaptx','software/components/chime_freeaptx/freeaptx.c',`/Fo${aptxObject}`]
 :['-std=c11','-O2','-Isoftware/components/chime_freeaptx','-c','software/components/chime_freeaptx/freeaptx.c','-o',aptxObject]);
test('freeaptx-real',['software/tests/bluetooth/test_freeaptx.c',aptxObject],['software/components/chime_freeaptx']);

// Both decoders now come from the user-selected WillyBilly06 repository.
// Check EVERY imported byte, then run its pinned bounded-port generators.
const refDir='software/components/chime_codec_reference';
const refVendor=`${refDir}/vendor`,native=`${refVendor}/native_bt/codecs`;
const port=path.join(build,'reference-port');
run(process.env.PYTHON??'python',[`${refDir}/prepare.py`,'--output',port]);
run(process.env.PYTHON??'python',['scripts/test-reference-codec-port.py']);
const ldacDir='software/components/chime_ldac',lhdcDir='software/components/chime_lhdc';
for(const dir of ['ldac','ldac-stress','ldac-adaptive','ldac-phone-failure','lhdc']){
 const fixtureDir=`software/tests/bluetooth/fixtures/${dir}`;
 const receipt=JSON.parse(fs.readFileSync(`${fixtureDir}/PROVENANCE.json`,'utf8'));
 for(const [name,hash] of Object.entries(receipt.files))
  assert.equal(createHash('sha256').update(fs.readFileSync(`${fixtureDir}/${name}`)).digest('hex'),hash,`fixture changed: ${name}`);
 if(dir==='ldac-adaptive')assert.equal(createHash('sha256').update(fs.readFileSync(receipt.generator)).digest('hex'),receipt.generator_sha256,'adaptive fixture generator changed');
}
const ldacLib=`${refVendor}/esp-idf/components/bt/host/bluedroid/external/libldac-dec`;
const lhdcSource=`${native}/lhdcv5/source_snapshot/decoder`;
const refIncludes=[refDir,`${native}/common`,`${refVendor}/native_bt/include`,`${native}/ldac`,`${native}/lhdcv5`,
 `${port}/ldac`,`${port}/lhdc`,`${ldacLib}/inc`,`${ldacLib}/src`,lhdcSource,'software/tests/bluetooth/stubs'];
const refDefines=['CONFIG_CHIME_BT_LHDC_V5=1','LHDC_HOST_BUILD=1','DBT_LHDC_TARGET_MATH=1','DBT_LHDC_NO_HOST_WINFILE=1',...(!msvc?['DBT_LHDC_QUALITY=1']:[])];
const refFlags=process.env.BT_LDAC_ASAN||process.env.BT_LHDC_ASAN?(msvc?['/fsanitize=address','/Zi']:['-fsanitize=address,undefined','-fno-omit-frame-pointer']):[];
const refSources=['software/tests/bluetooth/reference_host_globals.c',`${refDir}/chime_reference_codec.c`,`${port}/dbt_codec_lifecycle.c`,`${port}/dbt_ldac.c`,
 `${native}/ldac/dbt_ldac_decoder.c`,`${native}/ldac/dbt_ldac_bt.c`,`${native}/ldac/dbt_ldac_lib.c`,
 `${port}/dbt_lhdcv5_codec.c`,...['lhdc_dec','lhdc_imdct','lhdc_entropy_dec','lhdc_sns_synth'].map(n=>`${port}/lhdc/${n}.c`),
 ...['lhdc_bit_reader','lhdc_diag_config','lhdc_tables'].map(n=>`${lhdcSource}/${n}.c`)];
const refObjects=refSources.map((source,i)=>{
 const obj=path.join(build,`reference-${i}${msvc?'.obj':'.o'}`);
 run(cc,msvc?['/nologo','/std:c11','/experimental:c11atomics','/utf-8','/O2','/W3','/wd4244','/wd4305',`/FI${path.join(root,'software/tests/bluetooth/reference_host_compat.h')}`,...refFlags,
  ...refDefines.map(d=>`/D${d}`),...refIncludes.map(p=>`/I${p}`),'/c',source,`/Fo${obj}`]
 :['-std=gnu11','-O2','-fwrapv',...refFlags,...refDefines.map(d=>`-D${d}`),...refIncludes.map(p=>`-I${p}`),'-c',source,'-o',obj]);
 return obj;
});
// MSVC cannot enable the upstream collector's lock-free-word contract. Exercise
// the real facade separately, mocking only that collector (never its assert).
if(msvc){
 const obj=path.join(build,'reference-quality-facade.obj');
 run(cc,['/nologo','/std:c11','/experimental:c11atomics','/utf-8','/O2','/W4','/WX',...refFlags,
  ...[...refDefines,'DBT_LHDC_QUALITY=1'].map(d=>`/D${d}`),...refIncludes.map(p=>`/I${p}`),
  '/c',`${refDir}/chime_reference_codec.c`,`/Fo${obj}`]);
 test('reference-quality-facade',['software/tests/bluetooth/test_reference_quality.c',obj,...refObjects.filter((_,i)=>i!==1)],
  refIncludes,refDefines,refFlags);
}
test('ldac-reference',['software/tests/bluetooth/test_ldac.c',`${ldacDir}/chime_ldac.c`,...refObjects],[ldacDir,...refIncludes],refDefines,refFlags);
test('lhdc-reference',['software/tests/bluetooth/test_lhdc.c',`${lhdcDir}/chime_lhdc.c`,...refObjects],[lhdcDir,...refIncludes],refDefines,refFlags);
test('reference-lifecycle',['software/tests/bluetooth/test_reference_codec.c',`${ldacDir}/chime_ldac.c`,`${lhdcDir}/chime_lhdc.c`,...refObjects],
 [ldacDir,lhdcDir,...refIncludes],refDefines,refFlags);
test('bt-worker-reference-lhdc',['software/tests/bluetooth/test_chime_bt_decoder.c',`${lhdcDir}/chime_lhdc.c`,...refObjects],
 [ldacDir,lhdcDir,...refIncludes,'software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],
 [...refDefines,...lhdcDefines,'CHIME_BT_REAL_LHDC=1'],refFlags);
test('bt-worker-reference-ldac',['software/tests/bluetooth/test_chime_bt_decoder.c',`${ldacDir}/chime_ldac.c`,...refObjects],
 [ldacDir,lhdcDir,...refIncludes,'software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],
 [...refDefines,...lhdcDefines,'CHIME_BT_REAL_LDAC=1','CHIME_BT_CAPTURE_LOG=1'],refFlags);
// Test-only SDK error introspection includes the pinned decoder C unit; omit
// its normal reference-4 object to avoid duplicate definitions.
test('ldac-phone-failures',['software/tests/bluetooth/test_ldac_phone_failures.c',...refObjects.filter((_,i)=>i!==4)],
 refIncludes,refDefines,refFlags);
test('ldac-reference-replay',['software/tests/bluetooth/test_ldac_throughput.c',`${ldacDir}/chime_ldac.c`,...refObjects],[ldacDir,...refIncludes],refDefines,[...refFlags,msvc?'/O2':'-O2']);

// Compile LHDC without LHDC_HOST_BUILD as well: target window-init allocation
// and its fault cleanup must not be hidden by the host-only init shortcut.
const targetStub=path.join(build,'reference-target-stubs');fs.mkdirSync(targetStub,{recursive:true});
fs.writeFileSync(path.join(targetStub,'esp_attr.h'),'#pragma once\n#define IRAM_ATTR\n');
fs.writeFileSync(path.join(targetStub,'esp_heap_caps.h'),'#pragma once\n/* Codec allocation remains the actual reference hooks. */\n');
const targetDefines=refDefines.filter(d=>!d.startsWith('LHDC_HOST_BUILD='));
const targetSources=refSources.filter(source=>/[/\\]lhdc[/\\]lhdc_(dec|imdct|entropy_dec|sns_synth)\.c$/.test(source));
assert.equal(targetSources.length,4);
const targetObjects=targetSources.map((source,i)=>{
 const obj=path.join(build,`reference-target-${i}${msvc?'.obj':'.o'}`);
 run(cc,msvc?['/nologo','/std:c11','/experimental:c11atomics','/utf-8','/O2','/W3','/wd4244','/wd4305',`/FI${path.join(root,'software/tests/bluetooth/reference_host_compat.h')}`,...refFlags,
  ...targetDefines.map(d=>`/D${d}`),...[targetStub,...refIncludes].map(p=>`/I${p}`),'/c',source,`/Fo${obj}`]
 :['-std=gnu11','-O2','-fwrapv',...refFlags,...targetDefines.map(d=>`-D${d}`),...[targetStub,...refIncludes].map(p=>`-I${p}`),'-c',source,'-o',obj]);
 return obj;
});
const targetAll=refObjects.filter((obj,i)=>!targetSources.includes(refSources[i])).concat(targetObjects,'software/tests/bluetooth/reference_target_platform.c');
test('reference-target-init',['software/tests/bluetooth/test_reference_codec.c',`${ldacDir}/chime_ldac.c`,`${lhdcDir}/chime_lhdc.c`,...targetAll],
 [ldacDir,lhdcDir,...refIncludes],targetDefines,refFlags);
test('reference-target-lhdc',['software/tests/bluetooth/test_lhdc.c',`${lhdcDir}/chime_lhdc.c`,...targetAll],
 [lhdcDir,...refIncludes],targetDefines,refFlags);


if(process.env.IDF_PATH){
 run(process.env.PYTHON??'python',['-X','utf8','scripts/test-bluetooth-sdk-patch.py',process.env.IDF_PATH]);
 for(const mode of [0,1])test(`sdk-delay-${mode}`,['software/tests/bluetooth/test_sdk_delay.c'],[build],[`DELAY_RECOVERY=${mode}`]);
 test('vendor-avdt',['software/tests/bluetooth/test_vendor_avdt.c','software/main/chime_bt_vendor.c'],['software/main/include',build],['CONFIG_CHIME_BT_LHDC_V5=1']);
}else console.log('SKIP pinned SDK/actual AVDTP hook tests: set IDF_PATH and PYTHON to enable');

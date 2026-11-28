// Pipeline regression using the current unified S31 codec backend.
// Host correctness/replay only; neither RTOS schedulability nor S31 PIE timing.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {createHash} from 'node:crypto';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.join(root,'tmp','ldac-pipeline-tests');
fs.mkdirSync(build,{recursive:true});
const cc=process.env.CC??'gcc',python=process.env.PYTHON??'python';
const msvc=/^cl(?:\.exe)?$/i.test(path.basename(cc));
function run(cmd,args){
 const r=spawnSync(cmd,args,{cwd:root,encoding:'utf8',windowsHide:true,timeout:180000});
 process.stdout.write(r.stdout??'');process.stderr.write(r.stderr??'');
 if(r.error)throw r.error;
 assert.equal(r.status,0,`${cmd} failed`);
}
const ref='software/components/chime_codec_reference',ldac='software/components/chime_ldac';
run(python,['scripts/test-s31-codecs.py']);
const inc=[ref];
function test(name,files,includes,defines=[]){includes=[...includes,ref];
 const exe=path.join(build,name+(process.platform==='win32'?'.exe':''));
 run(cc,msvc?['/nologo','/std:c11','/experimental:c11atomics','/O2','/W4','/WX',...defines.map(d=>`/D${d}`),...includes.map(p=>`/I${p}`),...files,`/Fo${build}${path.sep}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO']
 :['-std=c11','-O2','-Wall','-Wextra','-Werror',...defines.map(d=>`-D${d}`),...includes.map(p=>`-I${p}`),...files,'-lm','-o',exe]);
 run(exe,[]);
}
const stubs=['software/tests/usb/stubs','software/tests/bluetooth/stubs','software/tests/audio/stubs','software/main/include'];
// Control/epoch/capacity integration uses mocked codec dispatch, not real other codecs.
test('worker-legacy',['software/tests/bluetooth/test_chime_bt_decoder.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1']);
test('bluetooth-internal',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c','software/main/chime_bt_delay.c'],
 ['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include']);
test('bluetooth-capacity',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c','software/main/chime_bt_delay.c','software/main/chime_bt_vendor.c'],
 ['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1']);
test('replay-worker',['software/tests/bluetooth/test_chime_ldac_replay.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_CHIME_LDAC_REPLAY_TEST=1']);
test('replay-preloaded-worker',['software/tests/bluetooth/test_chime_ldac_replay.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include',ldac],['CONFIG_CHIME_LDAC_REPLAY_TEST=1','CONFIG_CHIME_LDAC_REPLAY_FLASH=1']);
test('replay-format',['software/tests/bluetooth/test_chime_ldac_replay_format.c'],['software/main/include']);
test('replay-session',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c','software/main/chime_bt_delay.c','software/main/chime_bt_vendor.c'],
 ['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1','CONFIG_CHIME_LDAC_REPLAY_TEST=1']);
test('src',['software/tests/bluetooth/test_chime_bt_src.c','software/main/chime_bt_src.c'],stubs);
test('flow',['software/tests/bluetooth/test_chime_bt_flow.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c'],stubs);
test('mixer',['software/tests/audio/test_chime_mixer.c','software/main/chime_mixer.c'],stubs);
test('render',['software/tests/audio/test_chime_render.c','software/main/chime_mixer.c','software/main/chime_pcm.c'],stubs);
test('usb',['software/tests/usb/test_chime_usb.c','software/main/chime_usb.c','software/main/chime_hid.c'],stubs);
console.log('PASS LDAC pipeline host regression; board timing, RF and DMA continuity NOT TESTED');
test('bluetooth-ldac-pipeline',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c','software/main/chime_bt_delay.c','software/main/chime_bt_vendor.c','software/main/chime_bt_ready.c'],['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1','CONFIG_CHIME_LDAC_PIPELINE_TEST=1']);
test('ready',['software/tests/bluetooth/test_chime_bt_ready.c','software/main/chime_bt_ready.c'],['software/main/include']);

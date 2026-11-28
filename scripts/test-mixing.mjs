import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),build=path.join(root,'tmp','mixing-tests');fs.mkdirSync(build,{recursive:true});
const cc=process.env.CC??'gcc',msvc=/^cl(?:\.exe)?$/i.test(path.basename(cc));
function run(cmd,args){const r=spawnSync(cmd,args,{cwd:root,encoding:'utf8',windowsHide:true,timeout:180000});process.stdout.write(r.stdout??'');process.stderr.write(r.stderr??'');if(r.error)throw r.error;assert.equal(r.status,0);}
const inc=['software/tests/usb/stubs','software/tests/bluetooth/stubs','software/tests/audio/stubs','software/main/include'];
function test(name,files,defines=[],extraInc=[]){const exe=path.join(build,name+(process.platform==='win32'?'.exe':''));
run(cc,msvc?[...defines.map(d=>`/D${d}`),'/nologo','/std:c11','/experimental:c11atomics','/O2','/W4','/WX',...[...extraInc,...inc].map(p=>`/I${p}`),...files,`/Fo${build}${path.sep}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO']:
[...defines.map(d=>`-D${d}`),'-std=c11','-O2','-Wall','-Wextra','-Werror',...[...extraInc,...inc].map(p=>`-I${p}`),...files,'-lm','-o',exe]);run(exe,[]);}
test('mixer',['software/tests/audio/test_chime_mixer.c','software/main/chime_mixer.c']);
test('render',['software/tests/audio/test_chime_render.c','software/main/chime_mixer.c','software/main/chime_pcm.c']);
test('usb',['software/tests/usb/test_chime_usb.c','software/main/chime_usb.c','software/main/chime_hid.c']);
test('source',['software/tests/bluetooth/test_chime_source.c','software/main/chime_source.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c']);

test('src',['software/tests/bluetooth/test_chime_bt_src.c','software/main/chime_bt_src.c']);

test('flow',['software/tests/bluetooth/test_chime_bt_flow.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c']);

test('src-pie-model',['software/tests/bluetooth/test_chime_bt_src.c','software/main/chime_bt_src.c','software/tests/bluetooth/chime_bt_src_pie_model.c'],['CHIME_SRC_PIE_MODEL=1']);

test('src-accel-gate',['software/tests/bluetooth/test_chime_bt_src_accel.c','software/main/chime_bt_src_accel.c','software/main/chime_bt_src.c','software/tests/bluetooth/chime_bt_src_pie_model.c'],['CHIME_SRC_PIE_MODEL=1','CHIME_SRC_TEST_ACCEL_GATE=1'],['software/tests/bluetooth/accel_stubs']);

test('ready',['software/tests/bluetooth/test_chime_bt_ready.c','software/main/chime_bt_ready.c']);
test('src-dot-interpolation-model',['software/tests/bluetooth/test_chime_bt_src_dot_interp.c','software/main/chime_bt_src.c','software/tests/bluetooth/chime_bt_src_pie_model.c'],['CHIME_SRC_PIE_MODEL=1','CHIME_SRC_DOT_INTERPOLATION=1']);

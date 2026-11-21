import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.join(root,'tmp','input-tests');fs.mkdirSync(build,{recursive:true});
const cc=process.env.CC??'gcc',msvc=/^cl(?:\.exe)?$/i.test(path.basename(cc));
function run(cmd,args){const r=spawnSync(cmd,args,{cwd:root,encoding:'utf8',windowsHide:true});process.stdout.write(r.stdout??'');process.stderr.write(r.stderr??'');if(r.error)throw r.error;assert.equal(r.status,0);}
function test(name,files,includes,defines=[]){const exe=path.join(build,name+(process.platform==='win32'?'.exe':''));
run(cc,msvc?['/nologo','/std:c11','/experimental:c11atomics','/W4','/WX',...defines.map(d=>`/D${d}`),...includes.map(p=>`/I${p}`),...files,`/Fo${build}${path.sep}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO']:
['-std=c11','-Wall','-Wextra','-Werror',...defines.map(d=>`-D${d}`),...includes.map(p=>`-I${p}`),...files,'-o',exe,'-lm']);run(exe,[]);}
test('decoder',['software/tests/input/test_chime_input.c','software/main/chime_input.c'],['software/main/include']);
for(const reverse of [0,1])test(`controls-${reverse}`,['software/tests/input/test_chime_controls.c','software/main/chime_input.c'],['software/tests/input/stubs','software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'],[`CONFIG_CHIME_ENCODER_REVERSE=${reverse}`]);
const inc=['software/tests/bluetooth/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include'];
for(const [name,defines] of [['internal',[]],['external',['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1']],['vendor',['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1']]])
 test(`bt-${name}`,['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_delay.c',...(name==='vendor'?['software/main/chime_bt_vendor.c']:[])],inc,defines);
test('source',['software/tests/bluetooth/test_chime_source.c','software/main/chime_source.c','software/main/chime_bt_pcm.c'],['software/main/include']);
test('usb',['software/tests/usb/test_chime_usb.c','software/main/chime_usb.c','software/main/chime_pcm.c'],['software/tests/usb/stubs','software/tests/bluetooth/stubs','software/tests/audio/stubs','software/main/include']);
await import('./test-ss-audition.mjs');
await import('./test-led.mjs');

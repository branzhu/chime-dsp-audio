import assert from 'node:assert/strict';
import fs from 'node:fs';import path from 'node:path';import {spawnSync} from 'node:child_process';import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..'),build=path.join(root,'tmp','clock-follow-tests');fs.mkdirSync(build,{recursive:true});
const cc=process.env.CC??'gcc',msvc=/^cl(?:\.exe)?$/i.test(path.basename(cc));
function test(name,files,defs=[],extra=[]){const exe=path.join(build,name+(process.platform==='win32'?'.exe':''));
 const inc=[...extra,'software/tests/usb/stubs','software/tests/bluetooth/stubs','software/tests/audio/stubs','software/main/include'];
 const args=msvc?['/nologo','/std:c11','/experimental:c11atomics','/O2','/W4','/WX',...defs.map(d=>`/D${d}`),...inc.map(p=>`/I${p}`),...files,`/Fo${build}${path.sep}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO']:
 ['-std=c11','-O2','-Wall','-Wextra','-Werror',...defs.map(d=>`-D${d}`),...inc.map(p=>`-I${p}`),...files,'-lm','-o',exe];
 for(const [cmd,a] of [[cc,args],[exe,[]]]){const r=spawnSync(cmd,a,{cwd:root,encoding:'utf8',windowsHide:true});process.stdout.write(r.stdout??'');process.stderr.write(r.stderr??'');if(r.error)throw r.error;assert.equal(r.status,0);}}
const on=['CONFIG_CHIME_LDAC_APLL_FOLLOW=1'];
test('clock-math',['software/tests/audio/test_clock_follow.c','software/main/chime_clock_follow.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c']);
test('clock-render',['software/tests/audio/test_chime_render.c','software/main/chime_clock_follow.c','software/main/chime_mixer.c','software/main/chime_pcm.c'],on);
test('clock-usb',['software/tests/usb/test_chime_usb.c','software/main/chime_usb.c','software/main/chime_clock_follow.c','software/main/chime_hid.c'],on);

test('clock-hardware-owner',['software/tests/audio/test_bench_clock.c','software/main/bench_audio.c','software/main/chime_clock_follow.c','software/main/bench_tone.c'],on,['software/tests/audio/apll_stubs','software/tests/audio/stubs']);
test('clock-bluetooth',['software/tests/bluetooth/test_chime_bluetooth.c','software/main/chime_bluetooth.c','software/main/chime_bt_pcm.c','software/main/chime_bt_src.c','software/main/chime_bt_delay.c','software/main/chime_bt_vendor.c'],[...on,'CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1','CONFIG_CHIME_LDAC_REPLAY_TEST=1'],['software/tests/bluetooth/stubs']);


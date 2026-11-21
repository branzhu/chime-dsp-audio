import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.join(root,'tmp','led-tests');fs.mkdirSync(build,{recursive:true});
const cc=process.env.CC??'gcc',msvc=/^cl(?:\.exe)?$/i.test(path.basename(cc));
function run(cmd,args){const r=spawnSync(cmd,args,{cwd:root,encoding:'utf8',windowsHide:true});process.stdout.write(r.stdout??'');process.stderr.write(r.stderr??'');if(r.error)throw r.error;assert.equal(r.status,0);}
function compile(name,files,includes){const exe=path.join(build,name+(process.platform==='win32'?'.exe':''));
run(cc,msvc?['/nologo','/std:c11','/experimental:c11atomics','/W4','/WX',...includes.map(p=>`/I${p}`),...files,`/Fo${build}${path.sep}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO']:
['-std=c11','-Wall','-Wextra','-Werror',...includes.map(p=>`-I${p}`),...files,'-o',exe]);return exe;}
run(compile('policy',['software/tests/led/test_chime_led.c','software/main/chime_led.c'],['software/main/include']),[]);
const exe=compile('indicator',['software/tests/led/test_chime_indicator.c','software/main/chime_indicator.c','software/main/chime_led.c'],['software/tests/led/stubs','software/tests/usb/stubs','software/tests/audio/stubs','software/main/include']);
for(const scenario of [0,1,2,3])run(exe,[String(scenario)]);
const owner=fs.readFileSync(path.join(root,'software/main/bench_bridge.c'),'utf8');
const a=owner.indexOf('static void indicator_publish('),b=owner.indexOf('static void source_callbacks(',a);assert(a>=0&&b>a);
fs.writeFileSync(path.join(build,'led_owner.inc'),owner.slice(a,b));
run(compile('owner',['software/tests/led/test_led_owner.c'],['software/tests/led/stubs','software/tests/audio/stubs','software/main/include',build]),[]);
const network=fs.readFileSync(path.join(root,'software/main/chime_network.c'),'utf8');
assert.match(network,/else \{atomic_store\(&led_setup,chime_led_setup_requested\(true,false,false\)\);err=open_setup\(\);\}/);
assert.match(network,/atomic_store\(&led_setup,chime_led_setup_requested\(false,true,false\)\)/);
assert.match(network,/atomic_store\(&led_setup,chime_led_setup_requested\(false,false,true\)\)/);
console.log('PASS network first-use/submission/success wiring; automatic fallback does not enable the overlay');

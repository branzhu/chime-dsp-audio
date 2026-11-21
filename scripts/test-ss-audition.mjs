import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const source=fs.readFileSync(path.join(root,'software/main/bench_bridge.c'),'utf8').replace(/\r\n/g,'\n');
const build=path.join(root,'tmp','ss-owner-tests');fs.mkdirSync(build,{recursive:true});
const section=(from,to)=>{const a=source.indexOf(from),b=source.indexOf(to,a+from.length);assert(a>=0&&b>a);return source.slice(a,b);};
const callbacks=[section('typedef struct {','static uint64_t now_us'),section('static bool cancelled(','static void wait_us'),section('static void revoke_tuning(','static esp_err_t sound_read'),section('static int execute(','static int send_io')].join('\n');
fs.writeFileSync(path.join(build,'ss_owner_callbacks.inc'),callbacks);
// Source-level hand-back checks supplement (not replace) executed callbacks.
const close=section('\nclose_client:', '    bench_sound_cancel(&sound,ESP_ERR_INVALID_STATE);sound_publish(&sound);\n    if(client>=0)');
assert.doesNotMatch(close,/load_default|safe_shutdown|set_dsp_reset/);
assert.match(close,/tuning_armed=0/);assert.match(close,/bench_web_tuning_revoke/);
const arm=section('        int tuning=bench_web_take_tuning_request();','        int output=bench_web_take_output_request();');
assert.match(arm,/tuning==4/);assert.match(arm,/load_default/);assert.match(arm,/decoder.used==0&&ss_idle/);assert.match(arm,/bench_sound_check_external/);
const cc=process.env.CC??'gcc',msvc=/^cl(?:\.exe)?$/i.test(path.basename(cc));
const exe=path.join(build,process.platform==='win32'?'ss_owner_tests.exe':'ss_owner_tests');
const includes=['software/tests/audio/stubs','software/main/include','software/components/sigma_tcp_bridge/include','software/components/dsp_control/include',build];
const files=['software/tests/bridge/test_ss_audition_owner.c'];
function run(cmd,args){const r=spawnSync(cmd,args,{cwd:root,encoding:'utf8',windowsHide:true});process.stdout.write(r.stdout??'');process.stderr.write(r.stderr??'');if(r.error)throw r.error;assert.equal(r.status,0);}
run(cc,msvc?['/nologo','/std:c11','/W4','/WX',...includes.map(p=>`/I${p}`),...files,`/Fo${build}${path.sep}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO']:['-std=c11','-Wall','-Wextra','-Werror',...includes.map(p=>`-I${p}`),...files,'-o',exe]);
run(exe,[]);
console.log('PASS source-level retained-RAM hand-back and explicit restore/idle admission; full RTOS/TCP/USB concurrency still requires board acceptance');

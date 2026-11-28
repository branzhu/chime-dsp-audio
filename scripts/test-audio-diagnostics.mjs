import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.join(root,'tmp','audio-diagnostics-tests');fs.mkdirSync(build,{recursive:true});
const cc=process.env.CC??'gcc',msvc=/^cl(?:\.exe)?$/i.test(path.basename(cc));
function run(command,args){
    const r=spawnSync(command,args,{cwd:root,encoding:'utf8',windowsHide:true});
    process.stdout.write(r.stdout??'');process.stderr.write(r.stderr??'');
    if(r.error)throw r.error;assert.equal(r.status,0);
}
// Compile the production handler, not a parallel implementation of its guards.
const source=fs.readFileSync(path.join(root,'software/main/bench_web.c'),'utf8').replace(/\r\n/g,'\n');
const start=source.indexOf('static esp_err_t audio_get(httpd_req_t *req)');
const end=source.indexOf('\n}\n',start)+3;
assert(start>=0&&end>start);
fs.writeFileSync(path.join(build,'audio_get.inc'),source.slice(start,end));
const includes=[build,'software/tests/web/stubs','software/tests/bluetooth/stubs',
    'software/main/include','software/managed_components/espressif__cjson/cJSON'];
const files=['software/tests/web/test_audio_diagnostics.c',
    'software/managed_components/espressif__cjson/cJSON/cJSON.c'];
for(const external of [0,1]){
    const exe=path.join(build,`diagnostics-${external}${process.platform==='win32'?'.exe':''}`);
    const define=`CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=${external}`;
    run(cc,msvc?['/nologo','/std:c11','/W4','/WX',`/D${define}`,...includes.map(x=>`/I${x}`),
        ...files,`/Fo${build}${path.sep}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO']:
        ['-std=c11','-Wall','-Wextra','-Werror',`-D${define}`,...includes.map(x=>`-I${x}`),...files,'-o',exe]);
    run(exe,[]);
}

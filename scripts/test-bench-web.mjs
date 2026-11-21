import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import vm from 'node:vm';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.join(root,'tmp','bench-web-tests');fs.mkdirSync(build,{recursive:true});
const compiler=process.env.CC??'gcc';
const msvc=/^cl(?:\.exe)?$/i.test(path.basename(compiler));
const executable=path.join(build,process.platform==='win32'?'bench_status_tests.exe':'bench_status_tests');
function run(command,args){
  const result=spawnSync(command,args,{cwd:root,encoding:'utf8',windowsHide:true});
  process.stdout.write(result.stdout??'');process.stderr.write(result.stderr??'');
  if(result.error)throw result.error;
  assert.equal(result.status,0,`${command} failed (${result.status})`);
  return result.stdout;
}
const sources=['software/tests/web/test_bench_status.c','software/main/bench_status.c'];
run(compiler,msvc?['/nologo','/std:c11','/W4','/WX','/Isoftware/main/include',...sources,`/Fo${build}${path.sep}`,`/Fe${executable}`,'/link','/INCREMENTAL:NO']
  :['-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wsign-conversion','-pedantic','-Isoftware/main/include',...sources,'-o',executable]);
const output=run(executable,[]);
const adapterExecutable=path.join(build,process.platform==='win32'?'bench_web_tests.exe':'bench_web_tests');
const adapterSources=['software/tests/web/test_bench_web.c','software/main/bench_web.c','software/main/bench_dsp_status.c','software/main/bench_status.c','software/main/chime_setup_guard.c','software/managed_components/espressif__cjson/cJSON/cJSON.c'];
const adapterIncludes=['software/tests/web/stubs','software/main/include','software/managed_components/espressif__cjson/cJSON'];
run(compiler,msvc?['/nologo','/std:c11','/experimental:c11atomics','/W4','/WX',...adapterIncludes.map(dir=>`/I${dir}`),...adapterSources,`/Fo${build}${path.sep}`,`/Fe${adapterExecutable}`,'/link','/INCREMENTAL:NO']
  :['-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wsign-conversion','-pedantic',...adapterIncludes.map(dir=>`-I${dir}`),...adapterSources,'-o',adapterExecutable]);
run(adapterExecutable,[]);


const provisionExe=path.join(build,process.platform==='win32'?'chime_provision_tests.exe':'chime_provision_tests');
const provisionSources=['software/tests/web/test_chime_provision.c','software/main/chime_provision.c'];
run(compiler,msvc?['/nologo','/std:c11','/W4','/WX','/Isoftware/main/include',...provisionSources,`/Fo${build}${path.sep}`,`/Fe${provisionExe}`,'/link','/INCREMENTAL:NO']:['-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wsign-conversion','-pedantic','-Isoftware/main/include',...provisionSources,'-o',provisionExe]);
run(provisionExe,[]);
const guardExe=path.join(build,process.platform==='win32'?'chime_setup_guard_tests.exe':'chime_setup_guard_tests');
const guardSources=['software/tests/web/test_chime_setup_guard.c','software/main/chime_setup_guard.c'];
run(compiler,msvc?['/nologo','/std:c11','/W4','/WX','/Isoftware/main/include',...guardSources,`/Fo${build}${path.sep}`,`/Fe${guardExe}`,'/link','/INCREMENTAL:NO']:['-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wsign-conversion','-pedantic','-Isoftware/main/include',...guardSources,'-o',guardExe]);
run(guardExe,[]);
await import('./test-chime-page.mjs');

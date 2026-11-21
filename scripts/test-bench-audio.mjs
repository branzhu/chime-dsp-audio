import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const root=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');
const build=path.join(root,'tmp','bench-audio-tests');fs.mkdirSync(build,{recursive:true});
const cc=process.env.CC??'gcc';const msvc=/^cl(?:\.exe)?$/i.test(path.basename(cc));
const includes=['software/tests/audio/stubs','software/tests/diagnostics/stubs','software/main/include'];
const sources=['software/tests/audio/test_bench_audio.c','software/main/bench_audio.c','software/main/bench_tone.c'];
const exe=path.join(build,process.platform==='win32'?'bench_audio_tests.exe':'bench_audio_tests');
function run(cmd,args){const r=spawnSync(cmd,args,{cwd:root,encoding:'utf8',windowsHide:true});process.stdout.write(r.stdout??'');process.stderr.write(r.stderr??'');if(r.error)throw r.error;assert.equal(r.status,0);}
run(cc,msvc?['/nologo','/std:c11','/W4','/WX',...includes.map(p=>`/I${p}`),...sources,`/Fo${build}${path.sep}`,`/Fe${exe}`,'/link','/INCREMENTAL:NO']
:['-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wsign-conversion','-pedantic',...includes.map(p=>`-I${p}`),...sources,'-lm','-o',exe]);
run(exe,[]);

const soundExe=path.join(build,process.platform==='win32'?'bench_sound_tests.exe':'bench_sound_tests');
const soundSources=['software/tests/audio/test_bench_sound.c','software/main/bench_sound.c','software/main/bench_tone.c'];
run(cc,msvc?['/nologo','/std:c11','/W4','/WX',...includes.map(p=>`/I${p}`),...soundSources,`/Fo${build}${path.sep}`,`/Fe${soundExe}`,'/link','/INCREMENTAL:NO']
:['-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wsign-conversion','-pedantic',...includes.map(p=>`-I${p}`),...soundSources,'-lm','-o',soundExe]);
run(soundExe,[]);

const statusExe=path.join(build,process.platform==='win32'?'bench_dsp_status_tests.exe':'bench_dsp_status_tests');
const statusSources=['software/tests/audio/test_bench_dsp_status.c','software/main/bench_dsp_status.c'];
run(cc,msvc?['/nologo','/std:c11','/W4','/WX',...includes.map(p=>`/I${p}`),...statusSources,`/Fo${build}${path.sep}`,`/Fe${statusExe}`,'/link','/INCREMENTAL:NO']
:['-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wsign-conversion','-pedantic',...includes.map(p=>`-I${p}`),...statusSources,'-o',statusExe]);
run(statusExe,[]);

for(const [name,extra] of [['chime_dsp_boot',[]],['chime_pcm',[]]]){
 const executable=path.join(build,`${name}${process.platform==='win32'?'.exe':''}`);
 const files=[`software/tests/audio/test_${name}.c`,`software/main/${name}.c`];
 run(cc,msvc?['/nologo','/std:c11','/W4','/WX',...includes.map(p=>`/I${p}`),...files,`/Fo${build}${path.sep}`,`/Fe${executable}`,'/link','/INCREMENTAL:NO']
 :['-std=c11','-Wall','-Wextra','-Werror','-Wconversion','-Wsign-conversion','-pedantic',...includes.map(p=>`-I${p}`),...files,'-lm','-o',executable]);
 run(executable,extra);
}

const usbExe=path.join(build,`chime_usb${process.platform==='win32'?'.exe':''}`);
const usbIncludes=['software/tests/usb/stubs',...includes];
const usbFiles=['software/tests/usb/test_chime_usb.c','software/main/chime_usb.c','software/main/chime_pcm.c'];
run(cc,msvc?['/nologo','/std:c11','/experimental:c11atomics','/W4','/WX',...usbIncludes.map(p=>`/I${p}`),...usbFiles,`/Fo${build}${path.sep}`,`/Fe${usbExe}`,'/link','/INCREMENTAL:NO']
:['-std=c11','-Wall','-Wextra','-Werror',...usbIncludes.map(p=>`-I${p}`),...usbFiles,'-lm','-o',usbExe]);
run(usbExe,[]);

// Verify source/image/parameter-map agreement and generation failure cases.
await import('./test-frozen-dsp.mjs');

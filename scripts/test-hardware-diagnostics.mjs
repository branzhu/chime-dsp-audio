import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const build = path.join(root, 'tmp', 'hardware-diagnostics-tests');
fs.mkdirSync(build, { recursive: true });
const executable = path.join(build, process.platform === 'win32' ? 'diagnostics_tests.exe' : 'diagnostics_tests');
const compiler = process.env.CC ?? 'gcc';
const msvc = /^cl(?:\.exe)?$/i.test(path.basename(compiler));
const sources = ['software/main/hardware_diagnostics.c', 'software/tests/diagnostics/test_diagnostics.c'];
const includes = ['software/tests/diagnostics/stubs', 'software/main/include'];
const compileArgs = msvc
  ? ['/nologo', '/std:c11', '/W4', '/WX', ...includes.map(dir => `/I${dir}`), ...sources,
    `/Fo${build}${path.sep}`, `/Fe${executable}`, '/link', '/INCREMENTAL:NO']
  : ['-std=c11', '-Wall', '-Wextra', '-Werror', '-Wconversion', '-Wsign-conversion', '-pedantic',
    ...includes.map(dir => `-I${dir}`), ...sources, '-o', executable];
for (const [command, args] of [
  [compiler, compileArgs],
  [executable, []],
]) {
  const result = spawnSync(command, args, { cwd: root, encoding: 'utf8', windowsHide: true });
  process.stdout.write(result.stdout ?? '');
  process.stderr.write(result.stderr ?? '');
  if (result.error) throw result.error;
  if (result.status !== 0) throw new Error(`${command} failed with status ${result.status}.`);
}

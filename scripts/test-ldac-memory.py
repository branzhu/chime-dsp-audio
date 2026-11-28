"""LDAC-only: actual pinned decoder, heap vs SRAM PCM identity and bounded OOM.
Does not claim target throughput or RF/USB coexistence acceptance.
"""
import os, shutil, subprocess, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
os.chdir(ROOT)
CC=os.environ.get('S31_CC') or shutil.which('gcc') or ('C:/Qt/Tools/mingw1310_64/bin/gcc.exe' if os.name=='nt' else 'gcc')
os.environ['PATH']=str(Path(CC).parent)+os.pathsep+os.environ.get('PATH','')
REF=ROOT/'software/components/chime_codec_reference'
BUILD=ROOT/'tmp/ldac-fast-memory-tests';BUILD.mkdir(parents=True,exist_ok=True)
PORT=BUILD/'port'
def run(args):return subprocess.run([str(a) for a in args],cwd=ROOT,check=True,capture_output=True,text=True).stdout
print(run([sys.executable,REF/'prepare.py','--output',PORT]),end='')
(BUILD/'sdkconfig.h').write_text('#define CONFIG_CHIME_BT_VENDOR_CODECS 1\n')
LDAC=PORT/'external/libldac-dec'
flags=['-std=gnu11','-O2','-fwrapv',f'-I{REF}',f'-I{BUILD}',f'-I{LDAC}/inc',f'-I{LDAC}/src']
if os.environ.get('S31_SANITIZE'):flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
exe=BUILD/('allocator.exe' if os.name=='nt' else 'allocator')
run([CC,*flags,'software/tests/bluetooth/test_ldac_fast_memory.c',REF/'chime_ldac_memory.c','-o',exe])
print(run([exe]),end='')
outputs=[]
for mode in ('heap','sram'):
    exe=BUILD/(mode+('.exe' if os.name=='nt' else ''))
    definitions=['-DCHIME_LDAC_FAST_MEMORY_TEST=1'] if mode=='sram' else []
    run([CC,*flags,*definitions,'software/tests/bluetooth/test_ldac_memory_decode.c',
        LDAC/'src/ldacBT.c',LDAC/'src/ldaclib.c',REF/'chime_ldac_memory.c','-o',exe,'-lm'])
    output=run([exe]);print(output,end='');outputs.append([s for s in output.splitlines() if s.startswith('PCM ')])
assert outputs[0]==outputs[1] and len(outputs[0])==24
print('PASS LDAC heap/SRAM PCM identity: 12 real 44.1/48k fixtures, reopen, OOM cleanup; not target performance')

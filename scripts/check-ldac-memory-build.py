"""Read-only target artifact audit of LDAC SRAM allocation, not a profiler."""
import argparse,json,re,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser();p.add_argument('build',type=Path,nargs='?',default=ROOT/'software/build');a=p.parse_args()
commands=json.loads((a.build/'compile_commands.json').read_text(encoding='utf-8'))
mapping=(a.build/'audio_dsp_firmware.map').read_text(encoding='utf-8')
region=re.search(r'^sram_seg\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)',mapping,re.M);assert region
lo,size=(int(x,16) for x in region.groups())
arena=re.search(r'^ \.bss\.ldac_fast_storage\s+(0x[0-9a-f]+)\s+(0x[0-9a-f]+)\s+[^\n]*chime_ldac_memory.c.obj',mapping,re.M);assert arena,'Named LDAC SRAM arena missing/stale build'
address,nbytes=(int(x,16) for x in arena.groups())
assert nbytes==16384 and address%16==0 and lo<=address and address+nbytes<=lo+size
assert not re.search(r'\.bss\.(?:tin|tout)\.\d+[^\n]*lhdc_imdct.c.obj',mapping),'Unused LHDC self-test scratch still resident'
cache=(a.build/'CMakeCache.txt').read_text(encoding='utf-8')
nm=re.search(r'^CMAKE_NM:FILEPATH=(.+)$',cache,re.M);assert nm
for unit in ('ldacBT.c','ldaclib.c'):
    matches=[c for c in commands if c['file'].replace('\\','/').endswith('/libldac-dec/src/'+unit)]
    assert len(matches)==1
    c=matches[0];options=re.findall(r'(?<!\S)-O[0-3gsz](?!\S)',c['command']);assert options[-1]=='-O2'
    obj=Path(c['directory'])/c['output']
    symbols=subprocess.run([nm[1],'-u',str(obj)],check=True,capture_output=True,text=True).stdout
    undefined={line.split()[-1] for line in symbols.splitlines() if line.split()}
    assert not undefined & {'malloc','calloc','free'},(unit,undefined)
    assert {'chime_ldac_fast_calloc','chime_ldac_fast_free'}<=undefined,(unit,undefined)
print(f'PASS target LDAC memory: arena=0x{address:08x} bytes={nbytes} in HP SRAM, both LDAC units -O2 with no libc allocation fallback; unused LHDC self-test arrays removed')

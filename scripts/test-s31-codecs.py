"""Build/run the REAL selected S31 libraries, not proprietary/mock backends.
Uses GCC/MinGW for Opus C99 VLAs; worker/RTOS tests may still use MSVC.
"""
import glob,hashlib,json,os,shutil,subprocess,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1];os.chdir(ROOT)
LDAC_ONLY=sys.argv[1:]==['--ldac-only']
if sys.argv[1:] and not LDAC_ONLY:raise SystemExit('Usage: test-s31-codecs.py [--ldac-only]')
CC=os.environ.get('S31_CC') or shutil.which('gcc') or ('C:/Qt/Tools/mingw1310_64/bin/gcc.exe' if os.name=='nt' else 'gcc')
os.environ['PATH']=str(Path(CC).parent)+os.pathsep+os.environ.get('PATH','')
BUILD=ROOT/'tmp/s31-codec-tests';BUILD.mkdir(parents=True,exist_ok=True)
PORT=BUILD/'port';REF=ROOT/'software/components/chime_codec_reference'
def run(args):subprocess.run([str(x) for x in args],check=True,cwd=ROOT)
run([sys.executable,REF/'prepare.py','--output',PORT])
for receipt in (ROOT/'software/tests/bluetooth/fixtures').glob('*/PROVENANCE.json'):
    provenance=json.loads(receipt.read_text(encoding='utf-8'))
    for name,digest in provenance.get('files',{}).items():
        assert hashlib.sha256((receipt.parent/name).read_bytes()).hexdigest()==digest, f'Fixture changed: {name}'
print('PASS codec fixture provenance',flush=True)
E=PORT/'external'
STUB=BUILD/'stubs';STUB.mkdir(exist_ok=True);(STUB/'esp_attr.h').write_text('#pragma once\n#define IRAM_ATTR\n#define DRAM_ATTR\n')
includes=[REF,STUB,ROOT/'software/main/include',ROOT/'software/components/chime_ldac',ROOT/'software/components/chime_lhdc',PORT/'compat',E/'sbc/decoder/include',E/'libaac-lc/libhelix_aac',PORT/'stack/a2dp',E/'libfreeaptx/main',E/'libldac-dec/inc',E/'libldac-dec/src',E/'lhdc-v5',E/'opus_config',E/'opus',E/'opus/include',E/'opus/celt',E/'opus/silk',E/'opus/silk/fixed',E/'opus/silk/float',E/'opus/dnn',ROOT/'software/tests/bluetooth/stubs']
defines=['CONFIG_BT_A2DP_USE_EXTERNAL_CODEC=1','CONFIG_CHIME_BT_VENDOR_CODECS=1','CONFIG_CHIME_BT_LHDC_V5=1','HAVE_CONFIG_H=1','ESP32=1','CHIME_S31_BACKEND=1']
if LDAC_ONLY:defines.append('CHIME_LDAC_FAST_MEMORY_TEST=1')
flags=['-std=gnu11','-O2','-fwrapv',*[f'-D{x}' for x in defines],*[f'-I{x}' for x in includes]]
if os.environ.get('S31_SANITIZE'): flags+=['-fsanitize=address,undefined','-fno-omit-frame-pointer']
patterns=['sbc/decoder/srce/*.c','lhdc-v5/*.c','opus/src/*.c','opus/celt/*.c','opus/silk/*.c','opus/silk/fixed/*.c']
sources=[p for pat in patterns for p in sorted(E.glob(pat)) if p.stem not in ('opus_demo','opus_compare','repacketizer_demo','opus_custom_demo')]+[E/f'libaac-lc/libhelix_aac/{n}.c' for n in 'aacdec aactabs bitstream buffers dct4 decelmnt dequant fft filefmt huffman hufftabs imdct noiseless pns stproc tns trigtabs'.split()]+[PORT/'stack/a2dp/latm_parser.c',E/'libfreeaptx/main/freeaptx.c',E/'libldac-dec/src/ldacBT.c',E/'libldac-dec/src/ldaclib.c',REF/'chime_s31_codec.c',REF/'chime_reference_codec.c',REF/'chime_ldac_memory.c']
objects=[]
for i,source in enumerate(sources):
    obj=BUILD/f'codec-{i}.o';receipt=obj.with_suffix('.json')
    # Include generated headers in cache key, not merely source timestamps.
    key=hashlib.sha256(source.read_bytes()+(PORT/'CHIME_PORT.json').read_bytes()+str(flags).encode()).hexdigest()
    if not obj.exists() or not receipt.exists() or receipt.read_text()!=key:
        result=subprocess.run([CC,*flags,'-w','-c',str(source),'-o',str(obj)],capture_output=True,text=True)
        if result.returncode:print(result.stdout,result.stderr);sys.exit(result.returncode)
        receipt.write_text(key)
    objects.append(obj)
print(f'Compiled {len(objects)} pinned S31 codec objects',flush=True)
timer=BUILD/'host_timer.c';timer.write_text('#include <stdint.h>\nint64_t esp_timer_get_time(void){return 0;}\n')
failures=[]
def test(name,files,extra_flags=(),all_objects=True):
    if not name.startswith('worker-'):files=[*files,timer]
    exe=BUILD/(name+('.exe' if os.name=='nt' else ''))
    run([CC,*flags,*extra_flags,*files,*(objects if all_objects else []),'-o',exe,'-lm'])
    result=subprocess.run([str(exe)],cwd=ROOT)
    if result.returncode:failures.append(name);print(f'FAIL {name}: exit {result.returncode}',flush=True)
wrappers=[ROOT/'software/components/chime_ldac/chime_ldac.c',ROOT/'software/components/chime_lhdc/chime_lhdc.c']
if LDAC_ONLY:
    test('phone-frame-isolation',['software/tests/bluetooth/test_ldac_phone_failures.c',wrappers[0]])
    test('ldac',['software/tests/bluetooth/test_ldac.c',*wrappers])
else:
    test('allocation-faults',['software/tests/bluetooth/test_s31_codec_alloc.c'],['-Wl,--wrap=malloc,--wrap=calloc,--wrap=free'])
    test('reference-frontends',['software/tests/bluetooth/test_reference_codec.c',*wrappers])
    test('quality-unavailable',['software/tests/bluetooth/test_reference_quality.c'])
    test('phone-frame-isolation',['software/tests/bluetooth/test_ldac_phone_failures.c',wrappers[0]])
    test('s31-api',['software/tests/bluetooth/test_s31_codec.c',*wrappers])
    test('ldac',['software/tests/bluetooth/test_ldac.c',*wrappers])
    test('lhdc-unity-regression',['software/tests/bluetooth/test_lhdc.c',*wrappers])
    test('lhdc-structure',['software/tests/bluetooth/test_lhdc.c',*wrappers],['-DCHIME_S31_STRUCTURAL_ONLY=1'])
    test('aptx',['software/tests/bluetooth/test_freeaptx.c',*wrappers])
# Existing actual-worker integration cases keep their deterministic RTOS fakes.
for codec in (('LDAC',) if LDAC_ONLY else ('LDAC','LHDC')):
    test('worker-'+codec.lower(),['software/tests/bluetooth/test_chime_bt_decoder.c',wrappers[0 if codec=='LDAC' else 1]],
         [f'-DCHIME_BT_REAL_{codec}=1',f'-DCHIME_BT_CAPTURE_LOG={1 if codec=="LDAC" else 0}','-Isoftware/tests/usb/stubs','-Isoftware/tests/audio/stubs'])
print('FAIL selected S31 backend tests: '+', '.join(failures) if failures else 'PASS selected S31 backend suite (not RF/hardware acceptance)')
sys.exit(1 if failures else 0)

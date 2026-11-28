"""Verify the S31 import, then produce an auditable codec-only build tree.
No application, AVDTP parser, Bluetooth tasks or I2S renderer are linked.
"""
import argparse, hashlib, json, shutil
from pathlib import Path

def once(text, old, new):
    if text.count(old) != 1: raise RuntimeError('S31 port site moved: '+old[:100])
    return text.replace(old,new)

def generate(output):
    root=Path(__file__).resolve().parent
    pins=json.loads((root/'UPSTREAM.json').read_text(encoding='utf-8'))
    for name,digest in pins['files'].items():
        if hashlib.sha256((root/'vendor'/name).read_bytes()).hexdigest()!=digest:
            raise RuntimeError('Upstream codec source changed: '+name)
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    for name in pins['files']:
        relative=Path(name).relative_to('s31');dest=output/relative
        dest.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(root/'vendor'/name,dest)
    def patch(name,old,new):
        path=output/name;path.write_text(once(path.read_text(encoding='utf-8'),old,new),encoding='utf-8')
    # Keep the previously regression-tested negative scale-factor rejection.
    patch('external/libldac-dec/src/unpack_ldac.o.c',
          'if (p_ac->a_idsf[iqu] > 31) {','if (p_ac->a_idsf[iqu] < 0 || p_ac->a_idsf[iqu] > 31) {')
    # The SBC library only needs this inclusion gate, not a private host header.
    compat=output/'compat/common';compat.mkdir(parents=True,exist_ok=True)
    (compat/'bt_target.h').write_text('#pragma once\n#define TRUE 1\n#define FALSE 0\n#define SBC_DEC_INCLUDED TRUE\n',encoding='utf-8')
    # Keep fixed-point upstream Opus configuration, detach allocator from Bluedroid.
    (output/'external/opus_config/custom_support.h').write_text(
        '#pragma once\n#include <stdlib.h>\nstatic inline void *opus_alloc(size_t n){return malloc(n);}\nstatic inline void opus_free(void *p){free(p);}\n',encoding='utf-8')
    # Exact portable Helix arithmetic on S31 and 64-bit MSVC host test builds.
    patch('external/libaac-lc/libhelix_aac/assembly.h',
          '#if (defined (_WIN32) && !defined (_WIN32_WCE))',
          '#if (defined (_WIN32) && !defined (_WIN64) && !defined (_WIN32_WCE))')
    patch('external/libaac-lc/libhelix_aac/assembly.h',
          '#elif defined(ARDUINO) || defined(ESP32)',
          '#elif defined(__riscv) || defined(_WIN64) || defined(ARDUINO) || defined(ESP32)')
    # Reject bit-truncated LATM payloads (not just their byte-rounded lengths).
    patch('stack/a2dp/latm_parser.c','if (frame_byte_offset + aac_frame_len > data_len) {',
          'if (frame_bit_offset > data_len * 8 || aac_frame_len > (data_len * 8 - frame_bit_offset) / 8) {')
    # Bounds-only adaptation of the previously tested LDAC reader. Preserve
    # Huffman lookahead semantics, but never read beyond a complete frame.
    patch('external/libldac-dec/src/ldaclib.c','#include "ldac.h"',
          '#include "ldac.h"\nstatic unsigned s31_ldac_bit_limit;\nstatic int s31_ldac_overread;')
    path=output/'external/libldac-dec/src/unpack_ldac.o.c'
    text=path.read_text(encoding='utf-8');start=text.index('static void read_unpack_ldac(');end=text.index('\n/* Helper function',start)
    text=text[:start]+'''static void read_unpack_ldac(int nbits, STREAM *p_block, int *p_loc, int *p_idata) {
  if (!nbits) { *p_idata=0; return; }
  if (nbits<0 || nbits>24 || *p_loc<0 || (unsigned)*p_loc>=s31_ldac_bit_limit) {
    s31_ldac_overread=1; *p_idata=0; return;
  }
  unsigned bpos=(unsigned)*p_loc & LDAC_LOC_MASK;
  STREAM *p=p_block+((unsigned)*p_loc >> LDAC_LOC_SHIFT);
  unsigned tmp=(unsigned)p[2] | ((unsigned)p[1]<<8) | ((unsigned)p[0]<<16);
  *p_idata=(int)(((tmp<<bpos)&0xFFFFFFu)>>(24-nbits)); *p_loc+=nbits;
}
'''+text[end:];path.write_text(text,encoding='utf-8')
    patch('external/libldac-dec/src/ldaclib_api.o.c',
          'error_code = unpack_raw_data_frame_ldac(&hData->sfinfo, p_stream, &loc, p_nbytes_used);',
          's31_ldac_bit_limit=(unsigned)frame_length*8u; s31_ldac_overread=0;\n      error_code = unpack_raw_data_frame_ldac(&hData->sfinfo, p_stream, &loc, p_nbytes_used);\n      if(s31_ldac_overread) { hData->error_code=LDAC_ERR_INPUT_BUFFER_SIZE; return LDAC_E_FAIL; }')
    patch('external/libldac-dec/src/ldaclib_api.o.c',
          '  if (unpack_frame_header_ldac(',
          '  s31_ldac_bit_limit=24; s31_ldac_overread=0;\n  if (unpack_frame_header_ldac(')
    # Target-only LDAC memory policy. Include system headers BEFORE remapping
    # allocators, so libc declarations are not macro-rewritten. The same policy
    # is exercised by the dedicated host test, without changing pinned sources.
    allocator = """
#include <stdlib.h>
#include <malloc.h>
#if defined(ESP_PLATFORM) || defined(CHIME_LDAC_FAST_MEMORY_TEST)
#include "chime_ldac_memory.h"
#define malloc chime_ldac_fast_malloc
#define calloc chime_ldac_fast_calloc
#define free chime_ldac_fast_free
#endif
"""
    for unit in ('ldacBT.c', 'ldaclib.c'):
        path=output/'external/libldac-dec/src'/unit
        path.write_text(allocator+path.read_text(encoding='utf-8'),encoding='utf-8')
    # These 96/192k LHDC self-tests reserved 17280 bytes even while LDAC was
    # selected. Make their scratch temporary, offsetting the LDAC SRAM reserve.
    # OOM disables the candidate; never silently certify an untested transform.
    # No transform arithmetic, accepted rates or codec ownership changes.
    for suffix in ('960', '1920'):
        patch('external/lhdc-v5/lhdc_imdct.c',
              f'    static float tin[IMDCT_M_{suffix}];\n    static float tout[IMDCT_N_{suffix}];',
              f'    float *tin=malloc(IMDCT_M_{suffix}*sizeof(float));\n'
              f'    float *tout=malloc(IMDCT_N_{suffix}*sizeof(float));\n'
              f'    if(!tin || !tout){{free(tin);free(tout);s{suffix}_ok=0;return;}}')
        patch('external/lhdc-v5/lhdc_imdct.c',
              f'               s{suffix}_ok ? "ENABLED" : "DISABLED(fallback)", maxabs, maxdiff);',
              f'               s{suffix}_ok ? "ENABLED" : "DISABLED(fallback)", maxabs, maxdiff);\n    free(tin);free(tout);')
    # Never admit LHDC after transform-cache OOM (upstream ignores this result).
    patch('external/lhdc-v5/lhdc_imdct.c','(void)lhdc_cos_table_ensure(N);',
          '{ if (lhdc_cos_table_ensure(N) != 0) return -1; }')
    patch('external/lhdc-v5/lhdc_dec.c','        lhdc_imdct_init(M);',
          '        if (lhdc_imdct_init(M) != 0) return NULL;')
    generated={p.relative_to(output).as_posix():hashlib.sha256(p.read_bytes()).hexdigest()
        for p in sorted(output.rglob('*')) if p.is_file() and p.name!='CHIME_PORT.json'}
    (output/'CHIME_PORT.json').write_text(json.dumps({'repository':pins['repository'],'commit':pins['commit'],'files':generated},indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('--output',required=True,type=Path);a=p.parse_args();generate(a.output)

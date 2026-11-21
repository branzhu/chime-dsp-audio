/* Actual reference codecs: lifecycle, allocation-fault cleanup, retry, and
 * the product frame wrapper. No RTOS/radio timing is simulated. */
#include "chime_ldac.h"
#include "chime_lhdc.h"
#include "dbt_ldac.h"
#include "dbt_lhdcv5_codec.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
/* Model IDF TLSF: successful raw calloc pointers may be only 4-byte aligned.
 * ASan covers the alignment shim's metadata/redzones and exact original free. */
static unsigned raw_live,raw_calls,raw_fail_at;
static void *raw_calloc(size_t n,size_t size){
    assert(n==1);if(++raw_calls==raw_fail_at)return NULL;
    uint8_t *base=calloc(1,size+32);assert(base);
    uintptr_t address=(((uintptr_t)base+sizeof(base)+7u)&~(uintptr_t)7u)+4u;
    memcpy((void *)(address-sizeof(base)),&base,sizeof(base));++raw_live;
    assert((address&7u)==4u);return (void *)address;
}
static void raw_free(void *p){
    if(!p)return;
    uint8_t *base;memcpy(&base,(uint8_t *)p-sizeof(base),sizeof(base));
    assert(raw_live);--raw_live;free(base);
}
#define calloc raw_calloc
#define free raw_free
#include "chime_reference_memory.h"
#undef calloc
#undef free
static void alignment(void){
    for(size_t n=1;n<4096;++n){
        uint8_t *p=chime_reference_calloc8(n);assert(p&&!((uintptr_t)p&7u));
        for(size_t i=0;i<n;++i)assert(p[i]==0);
        memset(p,0xa5,n);chime_reference_free8(p);assert(!raw_live);
    }
    unsigned calls=raw_calls;
    assert(!chime_reference_calloc8(SIZE_MAX)&&!chime_reference_calloc8(0));assert(raw_calls==calls);
    raw_fail_at=raw_calls+1;assert(!chime_reference_calloc8(48));raw_fail_at=0;
    chime_reference_free8(NULL);assert(!raw_live);
}
static unsigned allocations,frees,attempt,fail_at;
static uint8_t *allocate(void *context,uint32_t bytes){
    (void)context;if(++attempt==fail_at)return NULL;
    uint8_t *p=chime_reference_calloc8(bytes);if(p)++allocations;return p;
}
static void release(void *context,uint8_t *p){(void)context;if(p){++frees;chime_reference_free8(p);}}
static dbt_result_t pcm(void *ctx,const void *p,uint16_t n,const dbt_codec_config_t *c,uint32_t ts){
    (void)ctx;(void)p;(void)n;(void)c;(void)ts;return DBT_OK;
}
static uint8_t *unaligned_allocate(void *context,uint32_t bytes){(void)context;return raw_calloc(1,bytes);}
static void unaligned_release(void *context,uint8_t *p){(void)context;raw_free(p);}
static void reject_unaligned(void){
    dbt_codec_decoder_t d;dbt_codec_hooks_t h={NULL,unaligned_allocate,unaligned_release,pcm};
    assert(dbt_codec_init(&d,h)==DBT_OK);
    assert(!dbt_codec_allocate(&d,64));assert(!raw_live&&!d.allocated_bytes);
}
static void fault_cleanup(int lhdc){
    uint8_t ld[]={0,255,0x2d,1,0,0,0xaa,0,0x10,1};
    uint8_t lh[]={0,255,0x3a,5,0,0,0x35,0x4c,0x10,0xa4,0x11,0,0};
    dbt_codec_config_t c;assert((lhdc?dbt_lhdcv5_configure(lh,sizeof(lh),&c):dbt_ldac_configure(ld,sizeof(ld),&c))==DBT_OK);
    c.ops=lhdc?&dbt_lhdcv5_codec:&dbt_ldac_codec;
    unsigned normal_attempts=0;
    for(unsigned fail=0;fail<=normal_attempts;++fail){
        dbt_codec_decoder_t d;dbt_codec_hooks_t h={NULL,allocate,release,pcm};assert(dbt_codec_init(&d,h)==DBT_OK);
        attempt=0;fail_at=fail;dbt_result_t r=dbt_codec_open(&d,&c);
        if(!fail){assert(r==DBT_OK);normal_attempts=attempt;assert(normal_attempts>0&&normal_attempts<128);}
        else assert(r!=DBT_OK);
        dbt_codec_close(&d);assert(d.allocated_bytes==0&&allocations==frees);
        /* Every failed open must release the singleton for the next attempt. */
        fail_at=0;assert(dbt_codec_open(&d,&c)==DBT_OK);dbt_codec_close(&d);
        assert(d.allocated_bytes==0&&allocations==frees);
    }
}
static void wrapper(int lhdc){
    unsigned char raw[8192];FILE *f=fopen(lhdc?"software/tests/bluetooth/fixtures/lhdc/48000-q3.lhdc":"software/tests/bluetooth/fixtures/ldac/48000-2.ldac","rb");
    assert(f);size_t n=fread(raw,1,sizeof(raw),f);assert(n>3);fclose(f);
    n=lhdc?chime_lhdc_frame_bytes(raw,n):chime_ldac_frame_bytes(raw,n,48000);assert(n>3);
    unsigned char first[960],out[962];size_t bytes=lhdc?960:512;
    for(unsigned pass=0;pass<3;++pass){
        void *d=lhdc?chime_lhdc_open(48000,2):chime_ldac_open(48000,2);assert(d);
        void *duplicate=lhdc?chime_lhdc_open(48000,2):chime_ldac_open(48000,2);assert(!duplicate);
        memset(out,0xa5,sizeof(out));
        assert(!(lhdc?chime_lhdc_decode(d,raw,n,out+1,bytes-1):chime_ldac_decode(d,raw,n,out+1,bytes-1)));
        for(size_t i=0;i<sizeof(out);++i)assert(out[i]==0xa5);
        assert((lhdc?chime_lhdc_decode(d,raw,n,out+1,bytes):chime_ldac_decode(d,raw,n,out+1,bytes))==bytes);
        assert(out[0]==0xa5&&out[bytes+1]==0xa5);
        if(pass)assert(!memcmp(first,out+1,bytes));else memcpy(first,out+1,bytes);
        if(lhdc)chime_lhdc_close(d);else chime_ldac_close(d);
    }
}
int main(void){alignment();reject_unaligned();fault_cleanup(0);fault_cleanup(1);wrapper(0);wrapper(1);assert(!raw_live);
    puts("PASS actual reference LDAC/LHDC V5: 4-byte raw heap to 8-byte alignment, overflow/OOM/no-leak, every open-allocation failure cleans up, singleton recovery, close/reopen PCM identity, bounded unaligned output and short-capacity no-write");return 0;}

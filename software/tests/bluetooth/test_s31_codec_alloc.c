/* Allocation-fault ownership tests against actual selected S31 objects.
 * GCC linker wrapping affects these codec calls, not the CRT's own heap. */
#include "chime_s31_codec.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
void *__real_malloc(size_t);void *__real_calloc(size_t,size_t);void __real_free(void *);
static void *live[256];static unsigned live_count,calls,fail_at,baseline;
static uint8_t fixture[2][2048];static size_t frame_bytes[2];
static void *record(void *p){if(p){assert(live_count<256);live[live_count++]=p;}return p;}
void *__wrap_malloc(size_t n){if(++calls==fail_at)return NULL;return record(__real_malloc(n));}
void *__wrap_calloc(size_t n,size_t size){if(++calls==fail_at)return NULL;return record(__real_calloc(n,size));}
void __wrap_free(void *p){if(!p)return;unsigned i=0;while(i<live_count&&live[i]!=p)i++;assert(i<live_count);live[i]=live[--live_count];__real_free(p);}
static void cycle(chime_s31_codec_t codec){
    void *d=chime_s31_open(codec,48000,2);
    if(d&&(codec==CHIME_S31_LDAC||codec==CHIME_S31_LHDC_V5)){
        /* LDAC initializes some state only on the first decoded frame. */
        unsigned index=codec==CHIME_S31_LDAC?0:1;
        uint8_t *raw=fixture[index];size_t frame=frame_bytes[index];
        int16_t pcm[480];chime_codec_input_t in={raw,(uint32_t)frame,0};chime_codec_output_t out={(uint8_t *)pcm,sizeof(pcm),0};chime_codec_info_t info;
        int r=chime_s31_decode(d,&in,&out,&info);if(!fail_at)assert(r==0);
    }
    chime_s31_close(d);if(live_count!=baseline)fprintf(stderr,"codec=%d fail_at=%u calls=%u live=%u\n",codec,fail_at,calls,live_count);assert(live_count==baseline);
}
int main(void){
    for(unsigned i=0;i<2;i++){
        const char *name=i?"software/tests/bluetooth/fixtures/lhdc/48000-q3.lhdc":"software/tests/bluetooth/fixtures/ldac/48000-2.ldac";
        FILE *f=fopen(name,"rb");assert(f);size_t n=fread(fixture[i],1,sizeof(fixture[i]),f);fclose(f);assert(n>3);
        const uint8_t *raw=fixture[i];
        frame_bytes[i]=i?((raw[0]|((unsigned)raw[1]<<8))&1023u)*2+2:((raw[1]&7u)*64+(raw[2]>>2)+4);
    }
    puts("Allocation tracker excludes persistent CRT startup allocations");fflush(stdout);
    baseline=live_count;
    for(int codec=CHIME_S31_SBC;codec<=CHIME_S31_LHDC_V5;codec++){
        fail_at=0;calls=0;cycle((chime_s31_codec_t)codec);unsigned attempts=calls;assert(attempts>0&&attempts<100);
        for(unsigned fail=1;fail<=attempts;fail++){
            calls=0;fail_at=fail;cycle((chime_s31_codec_t)codec);
            fail_at=0;calls=0;cycle((chime_s31_codec_t)codec); /* released singleton and clean retry */
        }
        printf("PASS S31 codec=%d allocation-fault cleanup/retry: %u normal allocation sites\n",codec,attempts);
    }
    return 0;
}

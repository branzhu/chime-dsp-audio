/* Host replay of the selected reference LDAC backend, not a target real-time
 * claim. Report the PCM hash and processing time; no speed pass threshold. */
#include "chime_ldac.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <time.h>
static uint8_t raw[300000];
_Alignas(int16_t) static uint8_t pcm[512];
static size_t offsets[1024],sizes[1024];
int main(void){
    const char *path="software/tests/bluetooth/fixtures/ldac-stress/48000-2.ldac";
    FILE *f=fopen(path,"rb");assert(f);size_t length=fread(raw,1,sizeof(raw),f);
    assert(feof(f)&&!ferror(f));fclose(f);
    unsigned count=0;
    for(size_t off=0;off<length;){size_t n=chime_ldac_frame_bytes(raw+off,length-off,48000);
        assert(n&&count<1024);offsets[count]=off;sizes[count++]=n;off+=n;}
    assert(count==801);uint32_t hash=2166136261U;unsigned frames=0;
    clock_t began=clock();
    for(unsigned run=0;run<64;++run){void *d=chime_ldac_open(48000,2);assert(d);
        for(unsigned i=0;i<count;++i){
            assert(chime_ldac_decode(d,raw+offsets[i],sizes[i],pcm,sizeof(pcm))==512);
            for(unsigned b=0;b<sizeof(pcm);++b)hash=(hash^pcm[b])*16777619U;
            ++frames;
        }
        chime_ldac_close(d);
    }
    double elapsed=(double)(clock()-began)/CLOCKS_PER_SEC;
    printf("LDAC_REPLAY frames=%u pcm_hash=%08x elapsed_s=%.6f us_per_frame=%.3f\n",frames,hash,elapsed,elapsed*1e6/frames);
    return 0;
}

#include "ldacBT.h"
#include "chime_ldac_memory.h"
#include <assert.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
static void fixture(const char *kind,unsigned rate,unsigned quality)
{
    char path[180];snprintf(path,sizeof(path),"software/tests/bluetooth/fixtures/%s/%u-%u.ldac",kind,rate,quality);
    FILE *f=fopen(path,"rb");assert(f);
    uint32_t reference=0;
    for(unsigned pass=0;pass<2;++pass){
        rewind(f);HANDLE_LDAC_BT h=ldacBT_get_handle();assert(h);
        assert(!ldacBT_init_handle_decode(h,LDACBT_CHANNEL_MODE_STEREO,(int)rate,0,0,0));
        uint8_t input[520];int16_t pcm[256];unsigned frames=0;uint32_t hash=2166136261U;
        while(fread(input,1,3,f)==3){
            assert(input[0]==0xaa);unsigned bytes=(input[1]&7)*64+(input[2]>>2)+4;
            assert(bytes>=4 && bytes<=515 && fread(input+3,1,bytes-3,f)==bytes-3);
            memset(input+bytes,0,sizeof(input)-bytes);
            int used=0,written=0;
            assert(!ldacBT_decode(h,input,pcm,LDACBT_SMPL_FMT_S16,(int)bytes,&used,&written));
            assert(used==(int)bytes && written==512);
            for(unsigned i=0;i<512;++i)hash=(hash^((uint8_t *)pcm)[i])*16777619U;
            ++frames;
        }
        assert(feof(f) && !ferror(f) && frames);
#ifdef CHIME_LDAC_FAST_MEMORY_TEST
        chime_ldac_memory_stats_t s;chime_ldac_memory_snapshot(&s);
        assert(s.used_bytes>8000 && s.used_bytes<=s.reserved_bytes && !s.failures);
#endif
        ldacBT_close_handle(h);ldacBT_free_handle(h);
#ifdef CHIME_LDAC_FAST_MEMORY_TEST
        chime_ldac_memory_snapshot(&s);assert(!s.used_bytes);
#endif
        if(pass)assert(reference==hash);else reference=hash;
        printf("PCM %s %u %u pass=%u frames=%u hash=%08x\n",kind,rate,quality,pass,frames,hash);
    }
    fclose(f);
}
int main(void)
{
    for(unsigned rate=44100;rate<=48000;rate+=3900)
        for(unsigned quality=0;quality<3;++quality){fixture("ldac",rate,quality);fixture("ldac-stress",rate,quality);}
#ifdef CHIME_LDAC_FAST_MEMORY_TEST
    chime_ldac_memory_stats_t s;chime_ldac_memory_snapshot(&s);
    printf("SRAM reserved=%u peak=%u used=%u failures=%u\n",s.reserved_bytes,s.peak_bytes,s.used_bytes,s.failures);
    /* Exhaustion must fail closed, not fall back to libc/RTC memory. */
    void *all=chime_ldac_fast_malloc(CHIME_LDAC_FAST_BYTES);assert(all);
    assert(!ldacBT_get_handle());chime_ldac_fast_free(all);
    /* Partially opened handle: leave room for handles, but not the decode pool. */
    all=chime_ldac_fast_malloc(9000);assert(all);
    HANDLE_LDAC_BT h=ldacBT_get_handle();
    if(h){
        int result=ldacBT_init_handle_decode(h,LDACBT_CHANNEL_MODE_STEREO,48000,0,0,0);
        if(!result){
            FILE *f=fopen("software/tests/bluetooth/fixtures/ldac/48000-2.ldac","rb");assert(f);
            uint8_t raw[520]={0};assert(fread(raw,1,3,f)==3);
            int bytes=(raw[1]&7)*64+(raw[2]>>2)+4;assert(fread(raw+3,1,bytes-3,f)==(size_t)bytes-3);fclose(f);
            int16_t pcm[256];int used=0,written=0;
            assert(ldacBT_decode(h,raw,pcm,LDACBT_SMPL_FMT_S16,bytes,&used,&written)!=0);
        }
        ldacBT_close_handle(h);ldacBT_free_handle(h);
    }
    chime_ldac_fast_free(all);chime_ldac_memory_snapshot(&s);assert(!s.used_bytes && s.failures);
#endif
    puts("PASS LDAC actual decoder reset and allocation cleanup");
}

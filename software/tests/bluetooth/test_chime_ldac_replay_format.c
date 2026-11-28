#include "chime_ldac_replay_format.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    uint8_t data[100]={0,0,0,0,3,0,1,0xaa,0xbb,0xcc};
    unsigned n=999;uint32_t frames=999;
    assert(chime_replay_batch(data,10,0,128,&n,&frames)&&n==1&&frames==128);
    assert(!chime_replay_batch(NULL,10,0,128,&n,&frames));
    assert(!chime_replay_batch(data,10,1,128,&n,&frames));
    assert(!chime_replay_batch(data,10,0,127,&n,&frames));
    assert(!chime_replay_batch(data,9,0,128,&n,&frames));
    assert(!chime_replay_batch(data,11,0,128,&n,&frames));
    data[6]=0;assert(!chime_replay_batch(data,10,0,2048,&n,&frames));
    data[6]=16;assert(!chime_replay_batch(data,10,0,2048,&n,&frames));
    data[6]=15;assert(chime_replay_batch(data,10,0,1920,&n,&frames)&&frames==1920);
    data[5]=8;assert(!chime_replay_batch(data,10,0,2048,&n,&frames));data[5]=0;
    data[6]=1;for(unsigned i=1;i<9;++i)memcpy(data+4+i*6,data+4,6);
    assert(chime_replay_batch(data,52,0,1024,&n,&frames)&&n==8&&frames==1024);
    assert(!chime_replay_batch(data,58,0,2048,&n,&frames));
    /* Long-test arithmetic must not overflow: positive ppm means EARLIER. */
    assert(chime_replay_offset_us(48000U*610,48000,0)==610000000ULL);
    assert(chime_replay_offset_us(44100U*610,44100,0)==610000000ULL);
    assert(chime_replay_offset_us(48000U*610,48000,2000)<610000000ULL);
    assert(chime_replay_offset_us(44100U*610,44100,-2000)>610000000ULL);
    puts("PASS replay wire bounds, sequencing, duration and independent clock arithmetic");return 0;
}

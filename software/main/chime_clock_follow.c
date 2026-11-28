#include "chime_clock_follow.h"
#include <string.h>
void chime_clock_servo_reset(chime_clock_servo_t *s){memset(s,0,sizeof(*s));}
int chime_clock_servo_update(chime_clock_servo_t *s,uint64_t now,unsigned queued,unsigned target,bool valid)
{
    if(!valid){chime_clock_servo_reset(s);return 0;}
    if(!s->seen||now<s->sampled_us||now-s->sampled_us>250000ULL){
        chime_clock_servo_reset(s);s->seen=true;s->sampled_us=now;return 0;
    }
    if(now-s->sampled_us<20000ULL)return s->ppm;
    s->sampled_us=now;
    /* >=2.56s low pass at 20ms cadence; discard missed ticks, not a burst of
     * catch-up writes. Positive depth error means faster OUTPUT clock. */
    int32_t error=(int32_t)(queued>8192U?8192U:queued)-(int32_t)(target>8192U?8192U:target);
    s->error_q8+=(error*256-s->error_q8)/128;
    int desired=s->error_q8/128; /* 2 ppm/frame, no integrating windup */
    if(desired>CHIME_CLOCK_MAX_PPM)desired=CHIME_CLOCK_MAX_PPM;
    if(desired< -CHIME_CLOCK_MAX_PPM)desired= -CHIME_CLOCK_MAX_PPM;
    if(s->ppm<desired)++s->ppm;else if(s->ppm>desired)--s->ppm;
    return s->ppm; /* <=50ppm/s, no dithering; quantized by SDM adapter */
}
uint32_t chime_clock_sdm_for_ppm(uint32_t nominal,int ppm)
{
    if(ppm>CHIME_CLOCK_MAX_PPM)ppm=CHIME_CLOCK_MAX_PPM;
    if(ppm< -CHIME_CLOCK_MAX_PPM)ppm= -CHIME_CLOCK_MAX_PPM;
    int64_t scale=(int64_t)nominal+262144;
    int64_t delta=(scale*ppm+(ppm<0?-500000:500000))/1000000;
    int64_t limit=scale*CHIME_CLOCK_MAX_PPM/1000000;
    if(delta>limit)delta=limit;
    if(delta< -limit)delta= -limit;
    return (uint32_t)((int64_t)nominal+delta);
}
uint32_t chime_clock_rate_q16(uint32_t sdm)
{
    return (uint32_t)((40000000ULL*(262144ULL+sdm)+4096ULL)/8192ULL);
}
uint32_t chime_clock_usb_feedback(uint32_t *level,unsigned bytes,uint32_t rate,bool hs)
{
    if(bytes>4096U)bytes=4096U;
    *level=(uint32_t)(((uint64_t)*level*63U+((uint64_t)bytes<<16))/64U);
    int32_t error=2048-(int32_t)(*level>>16);
    /* Existing TinyUSB FIFO gain: 65536/2048, with /8 at HS. */
    int64_t fb=(int64_t)(rate/(hs?8000U:1000U))+error*(hs?4:32);
    int64_t lo=(hs?5:47)*65536LL,hi=(hs?7:49)*65536LL;
    if(fb<lo)fb=lo;
    if(fb>hi)fb=hi;
    return (uint32_t)fb;
}

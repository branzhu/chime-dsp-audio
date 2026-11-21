#include <assert.h>
#include <stdio.h>
#include "chime_bt_delay.h"
int main(void)
{
    chime_bt_delay_t d;uint16_t v=0;
    const unsigned rates[]={16000,32000,44100,48000};
    for(unsigned i=0;i<4;++i){chime_bt_delay_reset(&d);unsigned n=(rates[i]*96+999)/1000;
        chime_bt_delay_sample(&d,0,n,rates[i],3000,17667);
        assert(d.queue_us>=96000&&d.queue_us<96063&&d.filtered_us==d.queue_us+20667);
    }
    chime_bt_delay_reset(&d);
    assert(!chime_bt_delay_candidate(&d,0,&v));
    chime_bt_delay_sample(&d,0,4608,48000,3000,10667);
    assert(d.valid&&d.samples==1&&d.filtered_us==109667&&d.queue_us==96000);
    assert(!chime_bt_delay_candidate(&d,0,&v));
    for(unsigned i=1;i<=100;++i)chime_bt_delay_sample(&d,i*10000ULL,4608,48000,3000,10667);
    assert(chime_bt_delay_candidate(&d,1000000,&v)&&v==1097); /* <120ms accepted, no floor */
    assert(!chime_bt_delay_candidate(&d,1000000,NULL));
    d.attempted=true;d.attempt_us=1000000;d.accepted_100us=v;
    assert(!chime_bt_delay_candidate(&d,2000000,&v));
    assert(!chime_bt_delay_candidate(&d,3000001,&v)); /* stale, not a heartbeat */
    for(unsigned i=101;i<=400;++i)chime_bt_delay_sample(&d,i*10000ULL,5376,48000,3000,10667);
    assert(d.filtered_us>124000&&d.filtered_us<125667);
    assert(chime_bt_delay_candidate(&d,4000000,&v)&&v>1240);
    d.accepted_100us=(uint16_t)((d.filtered_us+99)/100);
    assert(!chime_bt_delay_candidate(&d,4000000,&v));
    unsigned samples=d.samples;chime_bt_delay_sample(&d,3999999,1,48000,0,0);
    chime_bt_delay_sample(&d,4000001,1,0,0,0);assert(d.samples==samples);
    chime_bt_delay_sample(&d,4000001,1,48000,CHIME_BT_DELAY_MAX_US,1);assert(d.samples==samples);
    chime_bt_delay_sample(&d,6000001,2117,44100,2000,10667);
    assert(d.queue_us==48005&&d.filtered_us==60672); /* restart warmup after >1s gap */
    assert(!chime_bt_delay_candidate(&d,6000001,&v));
    chime_bt_delay_reset(&d);chime_bt_delay_sample(&d,0,0,48000,0,0);
    chime_bt_delay_sample(&d,1000000,0,48000,0,0);
    assert(chime_bt_delay_candidate(&d,1000000,&v)&&v==1);
    chime_bt_delay_reset(&d);chime_bt_delay_sample(&d,0,0,48000,0,CHIME_BT_DELAY_MAX_US);
    chime_bt_delay_sample(&d,1000000,0,48000,0,CHIME_BT_DELAY_MAX_US);
    assert(chime_bt_delay_candidate(&d,1000000,&v)&&v==65535);
    puts("PASS delay filter: actual queue/rate + residence + fixed, warmup/freshness, monotonic clock, range, hysteresis and rate limit");
    return 0;
}

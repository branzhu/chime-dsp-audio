#include "chime_clock_follow.h"
#include "chime_bt_pcm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static chime_bt_pcm_t q;
static void direct(void)
{
    uint8_t data[512];int32_t out[256];
    for(unsigned i=0;i<128;++i){data[4*i]=0;data[4*i+1]=0x80;data[4*i+2]=0xff;data[4*i+3]=0x7f;}
    assert(chime_bt_pcm_reset(&q,48000,2));
    assert(!chime_bt_pcm_direct_q27(&q,out,128,0)&&out[0]==0);
    for(unsigned i=0;i<36;++i)assert(chime_bt_pcm_push(&q,data,sizeof(data),false));
    for(unsigned i=0;i<36;++i){
        assert(chime_bt_pcm_direct_q27(&q,out,128,(uint64_t)i*2667));
        for(unsigned j=0;j<128;++j)assert(out[2*j]==-134217728&&out[2*j+1]==134213632);
        assert(q.consumed_frames==(i+1U)*128U&&q.ppm==0&&q.phase==0);
    }
    assert(!chime_bt_pcm_direct_q27(&q,out,128,100000)&&q.primed&&q.short_input);
    chime_bt_pcm_rebuffer(&q,200000);assert(!q.primed&&q.target_frames==5376);
    for(unsigned i=0;i<42;++i)assert(chime_bt_pcm_push(&q,data,sizeof(data),false));
    assert(chime_bt_pcm_direct_q27(&q,out,128,300000));
    /* No adaptive target decay under the hardware controller. */
    assert(chime_bt_pcm_push(&q,data,sizeof(data),false));
    assert(chime_bt_pcm_direct_q27(&q,out,128,120000000)&&q.target_frames==5376);
    assert(q.pushed_frames==q.consumed_frames+q.count+q.discarded_frames);
    assert(chime_bt_pcm_reset(&q,44100,2));assert(!chime_bt_pcm_direct_q27(&q,out,128,0));
    assert(chime_bt_pcm_reset(&q,48000,1));assert(!chime_bt_pcm_direct_q27(&q,out,128,0));
}
static void servo(void)
{
    chime_clock_servo_t s;chime_clock_servo_reset(&s);
    assert(chime_clock_servo_update(&s,0,7000,4608,true)==0);
    for(unsigned i=1;i<2000;++i){int prev=s.ppm;int ppm=chime_clock_servo_update(&s,(uint64_t)i*20000,7000,4608,true);assert(ppm>=prev&&ppm-prev<=1&&ppm<=300);}
    assert(s.ppm==300);
    assert(chime_clock_servo_update(&s,40000000,0,4608,false)==0&&!s.seen);
    for(unsigned i=0;i<2000;++i)chime_clock_servo_update(&s,(uint64_t)i*20000,1000,4608,true);
    assert(s.ppm==-300);
    assert(chime_clock_servo_update(&s,50000000,7000,4608,true)==0); /* stalled worker */
    assert(chime_clock_servo_update(&s,1,7000,4608,true)==0); /* epoch/time reset */
    /* 10 minutes each: packetized source, +/-250ppm skew, actual quantized
     * SDM output. No DMA submission/timing used as a source-rate estimator. */
    for(int sign=-1;sign<=1;sign+=2){
        chime_clock_servo_reset(&s);double depth=4608.0,pending=0.0;
        for(unsigned ms=0;ms<600000;++ms){
            pending+=48.0*(1.0+sign*0.000250);
            if(ms%16U==0){unsigned arrived=(unsigned)pending;depth+=arrived;pending-=arrived;}
            int ppm=chime_clock_servo_update(&s,(uint64_t)ms*1000,(unsigned)depth,4608,true);
            uint32_t sdm=chime_clock_sdm_for_ppm(382101,ppm);
            depth-=(double)chime_clock_rate_q16(sdm)/65536000.0;
            assert(depth>3000.0&&depth<6000.0);
        }
        assert(s.ppm*sign>200&&s.ppm*sign<290);
    }
}
static void clocks(void)
{
    uint32_t nominal=382101;assert(chime_clock_sdm_for_ppm(nominal,0)==nominal);
    assert(chime_clock_sdm_for_ppm(nominal,300)==nominal+193);
    assert(chime_clock_sdm_for_ppm(nominal,-300)==nominal-193);
    assert(chime_clock_sdm_for_ppm(nominal,2000)==nominal+193);
    uint32_t rate=chime_clock_rate_q16(nominal);
    assert(rate>CHIME_CLOCK_NOMINAL_Q16-65536&&rate<CHIME_CLOCK_NOMINAL_Q16+65536);
    for(unsigned hs=0;hs<2;++hs){
        uint32_t level=2048U<<16,base=rate/(hs?8000U:1000U);
        assert(chime_clock_usb_feedback(&level,2048,rate,hs!=0)==base);
        uint32_t higher=chime_clock_usb_feedback(&level,2048,chime_clock_rate_q16(nominal+193),hs!=0);
        assert(higher>base);
        assert(chime_clock_usb_feedback(&level,0,rate,hs!=0)>base);
        level=2048U<<16;assert(chime_clock_usb_feedback(&level,4096,rate,hs!=0)<base);
        for(unsigned i=0;i<10000;++i){uint32_t fb=chime_clock_usb_feedback(&level,65535,rate,hs!=0);assert(fb>=(hs?5U:47U)*65536U&&fb<=(hs?7U:49U)*65536U);}
    }
}
int main(void){direct();servo();clocks();puts("PASS direct LDAC48k exact PCM, prefill/rebuffer, servo direction/slew/clamp/reset, 2x600s packet/skew model, SDM quantization and HS/FS feedback");return 0;}

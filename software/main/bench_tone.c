#include "bench_tone.h"
#include <math.h>
static const int32_t wave[48]={
    0,137844,273329,404138,528032,642890,746749,837831,
    914577,975675,1020079,1047028,1056063,1047028,1020079,975675,
    914577,837831,746749,642890,528032,404138,273329,137844,
    0,-137844,-273329,-404138,-528032,-642890,-746749,-837831,
    -914577,-975675,-1020079,-1047028,-1056063,-1047028,-1020079,-975675,
    -914577,-837831,-746749,-642890,-528032,-404138,-273329,-137844
};
bool bench_tone_begin_config(bench_tone_t *tone,unsigned channels,bench_tone_config_t c)
{
    if(!tone||channels<1U||channels>3U||!bench_tone_config_valid(c))return false;
    *tone=(bench_tone_t){.channels=channels,.frequency_hz=c.frequency_hz,
        .total_frames=c.seconds*48000U,
        .gain_q24=(uint32_t)(pow(10.0,(double)(c.level_dbfs+18)/20.0)*16777216.0+0.5)};
    return true;
}
void bench_tone_begin(bench_tone_t *tone,unsigned channels)
{
    (void)bench_tone_begin_config(tone,channels,(bench_tone_config_t){1000U,-18,3U});
}
bool bench_tone_finished(const bench_tone_t *tone)
{
    return tone->frame>=tone->total_frames+BENCH_TONE_TAIL;
}
size_t bench_tone_render(bench_tone_t *tone,int32_t *stereo,size_t frames)
{
    size_t n=0;
    while(n<frames&&!bench_tone_finished(tone)) {
        uint32_t f=tone->frame++,ramp=BENCH_TONE_RAMP;
        int32_t sample=0;
        if(f<tone->total_frames) {
            if(f<BENCH_TONE_RAMP)ramp=f;
            if(tone->total_frames-1U-f<ramp)ramp=tone->total_frames-1U-f;
            /* Multiply, not left shift of a negative signed integer. */
            /* 48-point periodic table, linear interpolation; <0.02 dB
             * interpolation droop. Integer phase gives exact requested Hz. */
            uint32_t phase=(f*tone->frequency_hz)%48000U;
            unsigned i=phase/1000U,frac=phase%1000U;
            int64_t value=((int64_t)wave[i]*(1000U-frac)+(int64_t)wave[(i+1U)%48U]*frac)/1000;
            value=value*tone->gain_q24/16777216;
            sample=(int32_t)(value*ramp/BENCH_TONE_RAMP)*256;
        }
        stereo[n*2]=(tone->channels&1U)?sample:0;
        stereo[n*2+1]=(tone->channels&2U)?sample:0;
        ++n;
    }
    return n;
}

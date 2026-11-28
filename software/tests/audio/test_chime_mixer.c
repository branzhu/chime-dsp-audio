#include "chime_mixer.h"
#include <assert.h>
#include <stdio.h>
#include <limits.h>
static int32_t a[256],b[256],out[256];
int main(void)
{
    chime_mixer_t m;chime_mixer_init(&m);
    for(unsigned i=0;i<256;++i)a[i]=b[i]=CHIME_PCM_ONE/4;
    for(unsigned n=0;n<8;++n)chime_mixer_process(&m,a,b,true,true,CHIME_GAIN_ONE,CHIME_GAIN_ONE,false,out,128);
    assert(out[0]==1073741824&&!m.clipped_samples); /* two quarter-scale inputs, unity summing */
    for(unsigned n=0;n<4;++n)chime_mixer_process(&m,a,b,true,true,CHIME_GAIN_ONE,CHIME_GAIN_ONE,true,out,128);
    assert(out[255]==0);
    for(unsigned n=0;n<4;++n)chime_mixer_process(&m,a,b,true,true,CHIME_GAIN_ONE,CHIME_GAIN_ONE,false,out,128);
    assert(out[255]==1073741824);
    for(unsigned n=0;n<4;++n)chime_mixer_process(&m,a,b,true,false,CHIME_GAIN_ONE,CHIME_GAIN_ONE,false,out,128);
    assert(out[255]==536870912); /* no automatic gain boost */
    for(unsigned i=0;i<256;++i){a[i]=-CHIME_PCM_ONE/4;b[i]=-3*CHIME_PCM_ONE/4;}
    for(unsigned n=0;n<4;++n)chime_mixer_process(&m,a,b,true,true,CHIME_GAIN_ONE,CHIME_GAIN_ONE,false,out,128);
    assert(out[255]==INT32_MIN&&!m.clipped_samples); /* negative fullscale sum */
    for(unsigned i=0;i<256;++i)a[i]=b[i]=INT32_MAX;
    chime_mixer_process(&m,a,b,true,true,CHIME_GAIN_ONE,CHIME_GAIN_ONE,false,out,128);
    assert(out[255]==2147483392&&m.clipped_samples); /* saturate, never wrap */
    uint8_t raw[]={0,128,255,127};int32_t pcm[2];chime_pcm16_q27(raw,pcm,1);
    assert(pcm[0]==-CHIME_PCM_ONE&&pcm[1]==134213632);
    puts("PASS Q5.27 mix, fullscale, unity gain, gain/mute ramps, saturation, no single-source boost");return 0;
}

#include "chime_pcm.h"
#include <math.h>
int32_t chime_pcm_gain(int volume)
{
    if(volume>0)volume=0;
    if(volume< -60*256)volume= -60*256;
    return (int32_t)(32768.0*pow(10.0,(double)volume/(256.0*20.0))+0.5);
}
void chime_pcm_convert(const uint8_t *pcm,int32_t *out,size_t frames,int32_t gain)
{
    if(gain<0)gain=0;
    if(gain>32768)gain=32768;
    for(size_t i=0;i<frames*2;++i) {
        int32_t v=(int32_t)((uint32_t)pcm[2*i]|((uint32_t)pcm[2*i+1]<<8));
        if(v>=32768)v-=65536;
        /* Unity PCM16 -> full-scale PCM24 in a 32-bit slot; master gain only.
         * Use int64 for the product; INT16_MIN maps exactly to INT32_MIN.
         * Quantize before slot alignment; no signed shifts or overflow. */
        out[i]=(int32_t)((int64_t)v*256*gain/32768)*256;
    }
}

void chime_pcm_measure(chime_pcm_levels_t *levels,const uint8_t *pcm,
    const int32_t *out,size_t frames)
{
    for(size_t frame=0;frame<frames;++frame)for(unsigned ch=0;ch<2;++ch) {
        size_t i=frame*2+ch;
        int32_t v=(int32_t)((uint32_t)pcm[2*i]|((uint32_t)pcm[2*i+1]<<8));
        if(v>=32768)v-=65536;
        uint32_t input=(uint32_t)(v<0?-v:v);
        int64_t slot=out[i];
        uint32_t output=(uint32_t)((slot<0?-slot:slot)/256);
        if(input>levels->input_peak[ch])levels->input_peak[ch]=input;
        if(output>levels->output_peak[ch])levels->output_peak[ch]=output;
        levels->input_nonzero[ch]+=input!=0?1u:0u;
        levels->output_nonzero[ch]+=output!=0?1u:0u;
    }
}

int32_t chime_pcm_gain_q30(int volume)
{
    if(volume>0)volume=0;
    if(volume< -60*256)volume= -60*256;
    return (int32_t)(1073741824.0*pow(10.0,(double)volume/(256.0*20.0))+0.5);
}

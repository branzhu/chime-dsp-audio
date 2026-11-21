#include "chime_lhdc.h"
#include "chime_reference_codec.h"
void *chime_lhdc_open(unsigned rate,unsigned channels){return chime_reference_open(1,rate,channels);}
void chime_lhdc_close(void *d){chime_reference_close(d);}
size_t chime_lhdc_frame_bytes(const uint8_t *data,size_t available)
{
    if(!data||available<2)return 0;
    unsigned h=(unsigned)data[0]|((unsigned)data[1]<<8);
    size_t n=(h&1023U)*2U+2U;
    /* Only normal V5 lossy stereo frames. Optional metadata/AR/lossless off. */
    return (h>>10)==1&&n>=20&&n<=1538&&n<=available?n:0;
}
size_t chime_lhdc_decode(void *d,const uint8_t *data,size_t length,uint8_t *pcm,size_t capacity){
    if(chime_lhdc_frame_bytes(data,length)!=length)return 0;
    return chime_reference_decode(d,data,length,pcm,capacity);
}

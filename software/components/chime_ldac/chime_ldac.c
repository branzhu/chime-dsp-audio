#include "chime_ldac.h"
#include "chime_reference_codec.h"
void *chime_ldac_open(unsigned rate,unsigned channels){return chime_reference_open(0,rate,channels);}
void chime_ldac_close(void *d){chime_reference_close(d);}
size_t chime_ldac_frame_bytes(const uint8_t *data,size_t available,unsigned rate)
{
    if(!data||available<3||(rate!=44100&&rate!=48000)||data[0]!=0xaa||
       (data[1]>>5)!=(rate==48000?1:0)||((data[1]>>3)&3)!=2)return 0;
    size_t n=(((size_t)data[1]&7)<<6)+(data[2]>>2)+4;
    return n<=available&&n>=4&&n<=515?n:0;
}
size_t chime_ldac_decode(void *d,const uint8_t *data,size_t length,uint8_t *pcm,size_t capacity){
    if(chime_ldac_frame_bytes(data,length,44100)!=length&&chime_ldac_frame_bytes(data,length,48000)!=length)return 0;
    return chime_reference_decode(d,data,length,pcm,capacity);
}

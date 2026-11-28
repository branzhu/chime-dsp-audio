#include "chime_mixer.h"
#include <string.h>
#include <limits.h>
#define RAMP 480U
static int32_t advance(chime_mix_lane_t *s,int32_t target)
{
    if(target<0)target=0;
    if(target>CHIME_GAIN_ONE)target=CHIME_GAIN_ONE;
    if(target!=s->target){s->start=s->current;s->target=target;s->position=0;}
    if(s->position<RAMP){++s->position;s->current=s->start+(int32_t)((int64_t)(s->target-s->start)*s->position/RAMP);}
    return s->current;
}
void chime_mixer_init(chime_mixer_t *m){memset(m,0,sizeof(*m));}
void chime_pcm16_q27(const uint8_t *in,int32_t *out,size_t frames)
{
    for(size_t i=0;i<frames*2;++i){int32_t v=(int32_t)((unsigned)in[2*i]|((unsigned)in[2*i+1]<<8));
        if(v>=32768)v-=65536;
        out[i]=v*4096;
    }
}
void chime_mixer_process(chime_mixer_t *m,const int32_t *usb,const int32_t *bt,
    bool ur,bool br,int32_t ug,int32_t bg,bool mute,int32_t *out,size_t frames)
{
    for(size_t f=0;f<frames;++f){
        int32_t gain[2]={advance(&m->lane[0],ur?ug:0),advance(&m->lane[1],br?bg:0)};
        int32_t master=advance(&m->master,mute?0:CHIME_GAIN_ONE);
        for(unsigned c=0;c<2;++c){
            if(ur)m->lane[0].last[c]=usb[f*2+c];
            if(br)m->lane[1].last[c]=bt[f*2+c];
            int64_t v=((int64_t)m->lane[0].last[c]*gain[0]+(int64_t)m->lane[1].last[c]*gain[1])/CHIME_GAIN_ONE;
            v=v*master/CHIME_GAIN_ONE;
            uint64_t magnitude=(uint64_t)(v<0?-v:v);
            uint32_t peak=magnitude>UINT32_MAX?UINT32_MAX:(uint32_t)magnitude;
            if(peak>m->peak)m->peak=peak;
            /* Round once at the final 24-bit boundary; avoid signed shifts. */
            v=(v+(v<0?-8:8))/16;
            if(v>8388607){v=8388607;++m->clipped_samples;}
            if(v< -8388608){v= -8388608;++m->clipped_samples;}
            out[f*2+c]=(int32_t)v*256;
        }
    }
    ++m->blocks;
}

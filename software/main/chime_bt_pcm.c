#include "chime_bt_pcm.h"
#include <string.h>
static unsigned target_frames(unsigned rate,unsigned ms)
{
    return (rate*ms+999U)/1000U;
}
static void timing_start(chime_bt_pcm_t *q,uint64_t now_us)
{
    if(!q->clock_seen){q->clock_seen=true;q->decay_at_us=now_us+CHIME_BT_TARGET_HOLD_US;}
}
static void set_target(chime_bt_pcm_t *q,unsigned ms)
{
    if(ms<CHIME_BT_TARGET_MIN_MS)ms=CHIME_BT_TARGET_MIN_MS;
    if(ms>CHIME_BT_TARGET_MAX_MS)ms=CHIME_BT_TARGET_MAX_MS;
    if(ms==q->target_ms)return;
    if(ms>q->target_ms)++q->target_raises;else ++q->target_decays;
    unsigned frames=target_frames(q->rate,ms);
    /* Translate the filtered error to the new reference immediately, rather
     * than waiting ~0.7s for the filter to acquire the changed reference.
     * Phase/data are NEVER reset, skipped or repeated on target changes. */
    q->filtered_error-=((int32_t)frames-(int32_t)q->target_frames)*256;
    q->target_ms=ms;q->target_frames=frames;
}
void chime_bt_pcm_arrival(chime_bt_pcm_t *q,uint64_t now_us)
{
    if(!q->rate)return;
    timing_start(q,now_us);
    if(q->arrival_seen&&now_us>=q->last_arrival_us){
        uint64_t interval_ms=(now_us-q->last_arrival_us+999U)/1000U;
        unsigned wanted=interval_ms>=CHIME_BT_TARGET_MAX_MS?CHIME_BT_TARGET_MAX_MS:
            (unsigned)interval_ms+CHIME_BT_TARGET_MARGIN_MS;
        if(wanted>=q->target_ms){
            set_target(q,wanted);q->decay_at_us=now_us+CHIME_BT_TARGET_HOLD_US;
        }
    }
    q->last_arrival_us=now_us;q->arrival_seen=true;
}
bool chime_bt_pcm_reset(chime_bt_pcm_t *q,unsigned rate,unsigned channels)
{
    q->head=q->count=0;q->phase=0;q->filtered_error=q->ppm=0;q->primed=false;
    q->target_ms=CHIME_BT_TARGET_START_MS;q->target_frames=0;
    q->target_raises=q->target_decays=0;q->clock_seen=q->arrival_seen=q->short_input=false;
    q->last_arrival_us=q->decay_at_us=0;
    q->render_min=CHIME_BT_QUEUE_FRAMES;q->render_max=q->needed=0;
    q->pushed_frames=q->consumed_frames=q->rendered_frames=q->discarded_frames=0;q->clamp_blocks=0;
    bool valid=(rate==16000||rate==32000||rate==44100||rate==48000)&&(channels==1||channels==2);
    q->rate=valid?rate:0;q->channels=valid?channels:0;
    q->target_frames=valid?target_frames(rate,q->target_ms):0;return valid;
}
void chime_bt_pcm_rebuffer(chime_bt_pcm_t *q,uint64_t now_us)
{
    q->discarded_frames+=q->count;
    q->head=q->count=0;q->phase=0;q->primed=false;q->short_input=false;
    set_target(q,CHIME_BT_TARGET_MAX_MS);
    q->filtered_error=q->ppm=0;q->needed=0;
    q->clock_seen=true;q->decay_at_us=now_us+CHIME_BT_TARGET_HOLD_US;
    q->arrival_seen=false;q->last_arrival_us=0;
}
static int16_t read16(const uint8_t *p)
{
    int32_t v=(int32_t)((uint32_t)p[0]|((uint32_t)p[1]<<8));
    if(v>=32768)v-=65536;
    return (int16_t)v;
}
bool chime_bt_pcm_push(chime_bt_pcm_t *q,const uint8_t *data,size_t bytes,bool latest)
{
    if(!data||!q->channels||bytes%(q->channels*2U))return false;
    size_t frames=bytes/(q->channels*2U);
    if(frames>CHIME_BT_QUEUE_FRAMES-q->count){
        if(!latest||frames>CHIME_BT_QUEUE_FRAMES)return false;
        size_t drop=frames-(CHIME_BT_QUEUE_FRAMES-q->count);
        q->head=(q->head+drop)%CHIME_BT_QUEUE_FRAMES;q->count-=drop;q->phase=0;
    }
    for(size_t i=0;i<frames;++i){
        size_t index=(q->head+q->count)%CHIME_BT_QUEUE_FRAMES;
        q->pcm[index][0]=read16(data+i*q->channels*2U);
        q->pcm[index][1]=q->channels==2?read16(data+i*4U+2U):q->pcm[index][0];
        ++q->count;
    }
    q->pushed_frames+=frames;
    return true;
}
bool chime_bt_pcm_render(chime_bt_pcm_t *q,uint8_t *stereo,size_t frames,uint64_t now_us)
{
    if(!stereo||!frames||frames>128||!q->rate||!q->channels)return false;
    memset(stereo,0,frames*4U);
    timing_start(q,now_us);
    if(q->primed&&now_us>=q->decay_at_us){
        set_target(q,q->target_ms-1U);q->decay_at_us=now_us+CHIME_BT_TARGET_DECAY_US;
    }
    if(!q->primed&&q->count<q->target_frames)return false;
    if(q->count<q->render_min)q->render_min=(unsigned)q->count;
    if(q->count>q->render_max)q->render_max=(unsigned)q->count;
    /* Low-pass occupancy feedback; bounded +/-2000ppm corrects remote clock
     * drift without dropping/repeating arbitrary frames. Adaptive input-time target.
     * Input bursts are filtered over ~0.7s, not tracked sample-by-sample. */
    int32_t error=(int32_t)q->count-(int32_t)q->target_frames;
    q->filtered_error+=(error*256-q->filtered_error)/256;
    q->ppm=q->filtered_error/128;
    if(q->ppm>2000)q->ppm=2000;
    if(q->ppm< -2000)q->ppm= -2000;
    if(q->ppm==2000||q->ppm==-2000)++q->clamp_blocks;
    uint32_t base=(uint32_t)((uint64_t)q->rate*65536U/48000U);
    uint32_t step=(uint32_t)((int64_t)base*(1000000+q->ppm)/1000000);
    uint32_t end=q->phase+(uint32_t)frames*step;
    /* Preserve state on short input. Last interpolation requires the next
     * sample; also leave enough data for the integer phase consumption. */
    size_t needed=(size_t)((q->phase+(uint32_t)(frames-1)*step)>>16)+2;
    if(needed<=(size_t)(end>>16))needed=(size_t)(end>>16)+1;
    q->needed=(unsigned)needed;
    if(q->count<needed){
        if(!q->short_input){
            set_target(q,q->target_ms+8U);q->decay_at_us=now_us+CHIME_BT_TARGET_HOLD_US;
        }
        q->short_input=true;return false;
    }
    q->short_input=false;
    q->primed=true;
    for(size_t i=0;i<frames;++i){
        size_t a=(q->head+(q->phase>>16))%CHIME_BT_QUEUE_FRAMES;
        size_t b=(a+1)%CHIME_BT_QUEUE_FRAMES;
        uint32_t fraction=q->phase&65535U;
        for(unsigned ch=0;ch<2;++ch){
            int32_t v=q->pcm[a][ch]+(int32_t)((int64_t)(q->pcm[b][ch]-q->pcm[a][ch])*fraction/65536);
            uint16_t u=(uint16_t)(int16_t)v;
            stereo[i*4U+ch*2U]=(uint8_t)u;stereo[i*4U+ch*2U+1U]=(uint8_t)(u>>8);
        }
        q->phase+=step;
    }
    size_t consumed=q->phase>>16;q->head=(q->head+consumed)%CHIME_BT_QUEUE_FRAMES;
    q->consumed_frames+=consumed;q->rendered_frames+=frames;
    q->count-=consumed;q->phase&=65535U;return true;
}

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
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
void chime_bt_pcm_attach_tail(chime_bt_pcm_t *q,int16_t tail[6144][2]){q->pcm_tail=tail;}
static int16_t *pcm_at(chime_bt_pcm_t *q,size_t index)
{
    return index<2048?q->pcm[index]:q->pcm_tail[index-2048];
}
#else
static int16_t *pcm_at(chime_bt_pcm_t *q,size_t index){return q->pcm[index];}
#endif
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
        q->head=(q->head+drop)%CHIME_BT_QUEUE_FRAMES;q->count-=drop;q->discarded_frames+=drop;q->phase=0;
    }
    for(size_t i=0;i<frames;++i){
        size_t index=(q->head+q->count)%CHIME_BT_QUEUE_FRAMES;
        int16_t *dest=pcm_at(q,index);dest[0]=read16(data+i*q->channels*2U);
        dest[1]=q->channels==2?read16(data+i*4U+2U):dest[0];
        ++q->count;
    }
    q->pushed_frames+=frames;
    return true;
}
void chime_bt_pcm_control(chime_bt_pcm_t *q,unsigned queued_frames)
{
    int32_t error=(int32_t)queued_frames-(int32_t)q->target_frames;
    q->filtered_error+=(error*256-q->filtered_error)/256;
    int32_t target=q->filtered_error/128;
    if(target>2000)target=2000;
    if(target< -2000)target= -2000;
    /* <= 1 ppm/block (~375 ppm/s); packet bursts cannot abruptly bend pitch. */
    if(q->ppm<target)++q->ppm;else if(q->ppm>target)--q->ppm;
    if(q->ppm==2000||q->ppm== -2000)++q->clamp_blocks;
}
bool chime_bt_pcm_prepare_clocked(chime_bt_pcm_t *q,chime_bt_src_job_t *job,size_t frames,uint64_t now_us,bool update_clock)
{
    if(!job||!frames||frames>128||!q->rate||!q->channels)return false;
    timing_start(q,now_us);
    if(q->primed&&now_us>=q->decay_at_us){set_target(q,q->target_ms-1U);q->decay_at_us=now_us+CHIME_BT_TARGET_DECAY_US;}
    if(!q->primed&&q->count<q->target_frames)return false;
    if(q->count<q->render_min)q->render_min=(unsigned)q->count;
    if(q->count>q->render_max)q->render_max=(unsigned)q->count;
    if(update_clock)chime_bt_pcm_control(q,(unsigned)q->count);
    uint64_t base=((uint64_t)q->rate<<32)/48000U;
    uint64_t step=base*(uint64_t)(1000000+q->ppm)/1000000U;
    uint64_t end=q->phase+frames*step;
    size_t needed=(size_t)((q->phase+(frames-1)*step)>>32)+CHIME_SRC_HISTORY;
    q->needed=(unsigned)needed;
    if(needed>CHIME_SRC_WINDOW||q->count<needed){
        if(!q->short_input){set_target(q,q->target_ms+8U);q->decay_at_us=now_us+CHIME_BT_TARGET_HOLD_US;}
        q->short_input=true;return false;
    }
    q->short_input=false;q->primed=true;
    job->phase=q->phase;job->step=step;job->rate=q->rate;
    for(size_t i=0;i<needed;++i){size_t at=(q->head+i)%CHIME_BT_QUEUE_FRAMES;int16_t *src=pcm_at(q,at);job->pcm[i][0]=src[0];job->pcm[i][1]=src[1];}
    size_t consumed=(size_t)(end>>32);q->head=(q->head+consumed)%CHIME_BT_QUEUE_FRAMES;
    q->consumed_frames+=consumed;q->rendered_frames+=frames;q->count-=consumed;q->phase=(uint32_t)end;
    return true;
}
bool chime_bt_pcm_prepare(chime_bt_pcm_t *q,chime_bt_src_job_t *job,size_t frames,uint64_t now_us)
{
    return chime_bt_pcm_prepare_clocked(q,job,frames,now_us,true);
}
bool chime_bt_pcm_render(chime_bt_pcm_t *q,uint8_t *stereo,size_t frames,uint64_t now_us)
{
    if(!stereo||!frames||frames>128)return false;
    memset(stereo,0,frames*4U);chime_bt_src_job_t job;
    if(!chime_bt_pcm_prepare(q,&job,frames,now_us))return false;
    int32_t out[256];chime_bt_src_process(&job,out,frames);
    for(size_t i=0;i<frames*2;++i){int32_t v=(out[i]+(out[i]<0?-2048:2048))/4096;
    if(v>32767)v=32767;
    if(v< -32768)v= -32768;
        uint16_t u=(uint16_t)(int16_t)v;stereo[2*i]=(uint8_t)u;stereo[2*i+1]=(uint8_t)(u>>8);}
    return true;
}

bool chime_bt_pcm_direct_q27(chime_bt_pcm_t *q,int32_t *pcm,size_t frames,uint64_t now_us)
{
    if(!pcm||!frames||frames>128)return false;
    memset(pcm,0,frames*2U*sizeof(*pcm));
    if(q->rate!=48000||q->channels!=2)return false;
    timing_start(q,now_us);
    /* Hold the target within this admission. Moving a jitter-buffer target
     * during playback cannot be corrected quickly by a +/-300ppm clock. */
    if(!q->primed&&q->count<q->target_frames)return false;
    if(q->count<q->render_min)q->render_min=(unsigned)q->count;
    if(q->count>q->render_max)q->render_max=(unsigned)q->count;
    q->needed=(unsigned)frames;q->ppm=0;q->filtered_error=0;
    if(q->count<frames){q->short_input=true;return false;}
    for(size_t i=0;i<frames;++i){
        int16_t *src=pcm_at(q,(q->head+i)%CHIME_BT_QUEUE_FRAMES);
        pcm[2*i]=(int32_t)src[0]*4096;pcm[2*i+1]=(int32_t)src[1]*4096;
    }
    q->head=(q->head+frames)%CHIME_BT_QUEUE_FRAMES;q->count-=frames;
    q->consumed_frames+=frames;q->rendered_frames+=frames;
    q->phase=0;q->primed=true;q->short_input=false;return true;
}

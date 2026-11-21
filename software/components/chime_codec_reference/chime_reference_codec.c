#include "sdkconfig.h"
#include "chime_reference_codec.h"
#include "chime_reference_memory.h"
#include "chime_reference_diagnostics.h"
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#endif
#include "dbt_ldac.h"
#if CONFIG_CHIME_BT_LHDC_V5
#include "dbt_lhdcv5_codec.h"
#endif
#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
/* Keep the product PCM16 renderer. Upstream LDAC emits signed PCM32; this is
 * only container narrowing, not DSP/gain. No borrowed PCM leaves this call. */
typedef struct {
    dbt_codec_decoder_t decoder;
    uint8_t encoded[1540];
    uint8_t *pcm;
    size_t capacity,written;
    uint16_t sequence;
    bool lhdc,failed;
    uint32_t failed_bytes,allocation_count;
} reference_t;
static const char *TAG="bt_reference";
void chime_reference_init_failed(const char *stage,int result){
    ESP_LOGE(TAG,"init failed stage=%s result=%d",stage,result);
}
static uint8_t *allocate(void *context,uint32_t bytes){
    reference_t *s=context;uint8_t *p=chime_reference_calloc8(bytes);
    if(!p)s->failed_bytes=bytes;else ++s->allocation_count;
    return p;
}
static void release(void *context,uint8_t *bytes){(void)context;chime_reference_free8(bytes);}
static void open_failure(reference_t *s,const char *stage,dbt_result_t result,unsigned rate){
    ESP_LOGE(TAG,"open failed codec=%s rate=%u stage=%s result=%d failed_alloc=%" PRIu32 " allocations=%" PRIu32,
             s->lhdc?"LHDC V5":"LDAC",rate,stage,(int)result,s->failed_bytes,s->allocation_count);
#ifdef ESP_PLATFORM
    const uint32_t caps=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
    ESP_LOGE(TAG,"heap internal free=%u largest=%u minimum=%u (bytes)",
             (unsigned)heap_caps_get_free_size(caps),(unsigned)heap_caps_get_largest_free_block(caps),
             (unsigned)heap_caps_get_minimum_free_size(caps));
#endif
}
static dbt_result_t publish(void *context,const void *samples,uint16_t frames,const dbt_codec_config_t *cfg,uint32_t ts){
    (void)ts;reference_t *s=context;size_t bytes=(size_t)frames*2u*2u;
    if(!samples||!s->pcm||cfg->channels!=2||frames!=(s->lhdc?240:128)||
       (cfg->sample_bytes!=2&&cfg->sample_bytes!=4)||s->written||bytes>s->capacity)return DBT_INVALID;
    if(cfg->sample_bytes==2)memcpy(s->pcm,samples,bytes);
    else for(size_t i=0;i<(size_t)frames*2u;++i){
        int32_t value;memcpy(&value,(const uint8_t *)samples+4u*i,4);
        /* Round the narrower container; truncating the high word would turn
         * tiny negative transform residuals into a permanent -1 PCM bias. */
        int64_t rounded=value>=0?((int64_t)value+32768)/65536:-((-(int64_t)value+32768)/65536);
        int32_t v=rounded>32767?32767:rounded< -32768?-32768:(int32_t)rounded;
        uint16_t u=(uint16_t)(int16_t)v;s->pcm[2u*i]=(uint8_t)u;s->pcm[2u*i+1u]=(uint8_t)(u>>8);
    }
    s->written=bytes;return DBT_OK;
}
void *chime_reference_open(int lhdc,unsigned rate,unsigned channels){
    if((rate!=44100&&rate!=48000)||channels!=2)return NULL;
#if !CONFIG_CHIME_BT_LHDC_V5
    if(lhdc)return NULL;
#endif
    reference_t *s=chime_reference_calloc8(sizeof(*s));
    if(!s){ESP_LOGE(TAG,"open failed codec=%s stage=wrapper-allocation bytes=%u",lhdc?"LHDC V5":"LDAC",(unsigned)sizeof(*s));return NULL;}
    s->lhdc=lhdc!=0;
    dbt_codec_hooks_t hooks={s,allocate,release,publish};dbt_codec_config_t cfg;
    if(dbt_codec_init(&s->decoder,hooks)!=DBT_OK){chime_reference_free8(s);return NULL;}
    dbt_result_t r;
#if CONFIG_CHIME_BT_LHDC_V5
    if(lhdc){
#ifdef DBT_LHDC_QUALITY
        dbt_lhdcv5_quality_start(); /* Before first decoder admission; no counter reset. */
#endif
        uint8_t cie[]={0,0xff,0x3a,5,0,0,0x35,0x4c,0x10,0xa4,0x11,0,0};cie[8]=rate==48000?0x10:0x20;
        r=dbt_lhdcv5_configure(cie,sizeof(cie),&cfg);cfg.ops=&dbt_lhdcv5_codec;
    }else
#endif
    {
        uint8_t cie[]={0,0xff,0x2d,1,0,0,0xaa,0,0x10,1};cie[8]=rate==48000?0x10:0x20;
        r=dbt_ldac_configure(cie,sizeof(cie),&cfg);cfg.ops=&dbt_ldac_codec;
    }
    const char *stage="configure";
    if(r==DBT_OK){stage="codec-open";r=dbt_codec_open(&s->decoder,&cfg);}
    if(r!=DBT_OK){open_failure(s,stage,r,rate);dbt_codec_close(&s->decoder);chime_reference_free8(s);return NULL;}
    ESP_LOGI(TAG,"opened codec=%s rate=%u allocated=%" PRIu32 " allocations=%" PRIu32 " alignment=8",
             s->lhdc?"LHDC V5":"LDAC",rate,s->decoder.allocated_bytes,s->allocation_count);
    return s;
}
void chime_reference_close(void *handle){reference_t *s=handle;if(s){dbt_codec_close(&s->decoder);chime_reference_free8(s);}}
size_t chime_reference_decode(void *handle,const uint8_t *data,size_t length,uint8_t *pcm,size_t capacity){
    reference_t *s=handle;
    if(!s||s->failed||!data||!pcm||!length||length>(s->lhdc?1538u:515u)||capacity<(s->lhdc?960u:512u))return 0;
    unsigned header=s->lhdc?2u:1u;
    s->encoded[0]=s->lhdc?4:1;if(s->lhdc)s->encoded[1]=(uint8_t)s->sequence;
    memcpy(s->encoded+header,data,length);
    dbt_avdtp_media_t media={.payload=s->encoded,.timestamp=0,.ssrc=1,.size=(uint16_t)(length+header),.sequence=s->sequence++,.payload_type=96};
    s->pcm=pcm;s->capacity=capacity;s->written=0;
    dbt_result_t r=s->decoder.config.ops->media(&s->decoder,&media,0);
    s->pcm=NULL;s->capacity=0;
    if(r!=DBT_OK||s->written!=(s->lhdc?960u:512u)){s->failed=true;return 0;}
    return s->written;
}

bool chime_reference_quality_snapshot(chime_reference_quality_t *out){
#if CONFIG_CHIME_BT_LHDC_V5 && defined(DBT_LHDC_QUALITY)
    if(!out)return false;
    dbt_lhdcv5_quality_t q;if(!dbt_lhdcv5_quality_snapshot(&q))return false;
    chime_reference_quality_t s={
        .attempts=q.attempts,.rejected=q.rejected,.channel0_errors=q.channel0_errors,.channel1_silenced=q.channel1_silenced,
        .peak_concealed=q.peak_concealed,.nonfinite_frames=q.nonfinite_frames,.clipped_frames=q.clipped_frames,.clipped_samples=q.clipped_samples,
        .event_attempt=q.event_attempt,.frame_bytes=q.frame_bytes,.flags=q.flags,.peak_f32_bits=q.peak_f32_bits,
        .result=q.result,.channel0_result=q.channel0_result,.channel1_result=q.channel1_result};
    *out=s;return true;
#else
    (void)out;return false;
#endif
}

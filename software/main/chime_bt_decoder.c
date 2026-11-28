#include "sdkconfig.h"
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
#include "chime_bt_decoder.h"
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
#include "chime_bt_src.h"
#endif
#include "chime_s31_codec.h"
#if CONFIG_CHIME_BT_VENDOR_CODECS
#include "chime_ldac.h"
#if CONFIG_CHIME_BT_LHDC_V5
#include "chime_lhdc.h"
#endif
#include <string.h>
#endif
#include "esp_log.h"
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#if CONFIG_CHIME_BT_VENDOR_CODECS
#include "chime_ldac_memory.h"
#endif
#endif
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <stdatomic.h>
#include <stdlib.h>

/* No decode/open/close on the BT task. Queue retains at most 12 SDK buffers;
 * oversized input is rejected before queuing, output never reallocates. */
#define RAW_DEPTH 12
#define RAW_MAX_BYTES 4096
#define PCM_BYTES 4096
/* CPU0 owns scalar PCM rendering, BT/Wi-Fi and DSP/control. A continuously
 * ready floating-point worker must not starve source admission on that core.
 * Explicit affinity also avoids lazy FPU pinning choosing CPU0 accidentally. */
#if CONFIG_FREERTOS_UNICORE
#define DECODE_CORE 0
#else
#define DECODE_CORE 1
#endif

#if CONFIG_CHIME_BT_VENDOR_CODECS
typedef struct {
    const uint8_t *data;
    uint32_t bytes,timestamp,crc32,capture_us;
    uint8_t head[4];
    bool recorded,hashed;
} input_evidence_t;
/* Fixed queue/stack cost, no retained payload copy. Worker evidence is zero in
 * queued records and filled only in a worker-local descriptor. */
_Static_assert(sizeof(input_evidence_t)<=32,"LDAC evidence must stay bounded");
#endif

typedef struct {
    esp_a2d_audio_buff_t *buffer;
    uint32_t epoch;
    chime_bt_codec_t codec;
    unsigned rate, channels;
    uint64_t enqueued_us;
#if CONFIG_CHIME_BT_VENDOR_CODECS
    input_evidence_t submitted,worker;
#endif
} packet_t;
static const char *TAG="bt_decode";
static QueueHandle_t raw_queue;
static chime_bt_decoder_callbacks_t cb;
static atomic_bool stopping, exited;
/* Everything below is owned exclusively by the decoder task. */
static void *decoder;
static chime_bt_codec_t active_codec;
static uint32_t active_epoch;
static uint8_t *output;
/* Completed packet times are diagnostic only; never change admission/safety. */
static atomic_uint last_packet_us,max_packet_us,last_decode_us,last_pcm_us,last_open_us;
static atomic_uint max_decode_us,max_pcm_us,open_free_dma_bytes,open_free_rtc_bytes;
static atomic_uint stats_revision,stats_epoch,stats_packets,stats_slow,stats_audio_us;
static atomic_uint stats_ldac_frames,stats_ldac_pcm_frames,stats_ldac_same_input,stats_ldac_ts_same;
static atomic_uint stats_ldac_ts_delta,stats_ldac_previous_frames;
#if CONFIG_CHIME_BT_VENDOR_CODECS
static bool ldac_previous_seen;
static uint32_t ldac_previous_ts,ldac_previous_crc,ldac_previous_bytes,ldac_previous_samples;
#endif
static atomic_uint stats_wait_us,stats_max_wait_us,stats_raw_after,stats_raw_max_after;
static atomic_uint stats_capacity_us,stats_capacity_max_us,stats_capacity_waits,stats_capacity_timeouts;
bool chime_bt_decoder_snapshot(chime_bt_decoder_stats_t *out)
{
    if(!out)return false;
#if defined(ESP_PLATFORM) && CONFIG_CHIME_BT_VENDOR_CODECS
    chime_ldac_memory_stats_t memory;chime_ldac_memory_snapshot(&memory);
#endif
    for(unsigned attempt=0;attempt<3;++attempt){
        unsigned revision=atomic_load(&stats_revision);if(revision&1u)continue;
        chime_bt_decoder_stats_t s={
            .epoch=atomic_load(&stats_epoch),.packets=atomic_load(&stats_packets),.slow_packets=atomic_load(&stats_slow),
            .packet_us=atomic_load(&last_packet_us),.max_packet_us=atomic_load(&max_packet_us),
            .decode_us=atomic_load(&last_decode_us),.pcm_us=atomic_load(&last_pcm_us),.open_us=atomic_load(&last_open_us),
            .audio_us=atomic_load(&stats_audio_us),
            .max_decode_us=atomic_load(&max_decode_us),.max_pcm_us=atomic_load(&max_pcm_us),
#if defined(ESP_PLATFORM) && CONFIG_CHIME_BT_VENDOR_CODECS
            .ldac_fast_reserved_bytes=memory.reserved_bytes,.ldac_fast_used_bytes=memory.used_bytes,
            .ldac_fast_peak_bytes=memory.peak_bytes,.ldac_fast_failures=memory.failures,
#endif
            .open_free_dma_bytes=atomic_load(&open_free_dma_bytes),.open_free_rtc_bytes=atomic_load(&open_free_rtc_bytes),
            .queue_wait_us=atomic_load(&stats_wait_us),.max_queue_wait_us=atomic_load(&stats_max_wait_us),
            .raw_after_dequeue=atomic_load(&stats_raw_after),.raw_max_after_dequeue=atomic_load(&stats_raw_max_after),
            .ldac_frames=atomic_load(&stats_ldac_frames),.ldac_pcm_frames=atomic_load(&stats_ldac_pcm_frames),
            .ldac_same_input=atomic_load(&stats_ldac_same_input),.ldac_ts_same=atomic_load(&stats_ldac_ts_same),
            .ldac_ts_delta=atomic_load(&stats_ldac_ts_delta),.ldac_previous_frames=atomic_load(&stats_ldac_previous_frames),
            .capacity_wait_us=atomic_load(&stats_capacity_us),.max_capacity_wait_us=atomic_load(&stats_capacity_max_us),
            .capacity_waits=atomic_load(&stats_capacity_waits),.capacity_timeouts=atomic_load(&stats_capacity_timeouts)};
        if(revision==atomic_load(&stats_revision)){*out=s;return true;}
    }
    return false;
}
static void stats_reset(uint32_t epoch)
{
    atomic_fetch_add(&stats_revision,1);
    atomic_store(&max_decode_us,0);atomic_store(&max_pcm_us,0);
    atomic_store(&open_free_dma_bytes,0);atomic_store(&open_free_rtc_bytes,0);
    atomic_store(&stats_epoch,epoch);atomic_store(&stats_packets,0);atomic_store(&stats_slow,0);atomic_store(&stats_audio_us,0);
    atomic_store(&stats_capacity_us,0);atomic_store(&stats_capacity_max_us,0);
    atomic_store(&stats_capacity_waits,0);atomic_store(&stats_capacity_timeouts,0);
    atomic_store(&stats_wait_us,0);atomic_store(&stats_max_wait_us,0);atomic_store(&stats_raw_after,0);atomic_store(&stats_raw_max_after,0);
    atomic_store(&stats_ldac_frames,0);atomic_store(&stats_ldac_pcm_frames,0);
    atomic_store(&stats_ldac_same_input,0);atomic_store(&stats_ldac_ts_same,0);
    atomic_store(&stats_ldac_ts_delta,0);atomic_store(&stats_ldac_previous_frames,0);
#if CONFIG_CHIME_BT_VENDOR_CODECS
    ldac_previous_seen=false;
#endif
    atomic_store(&last_packet_us,0);atomic_store(&max_packet_us,0);
    atomic_store(&last_decode_us,0);atomic_store(&last_pcm_us,0);atomic_store(&last_open_us,0);
    atomic_fetch_add(&stats_revision,1);
}
static uint32_t packet_pcm_bytes,packet_decode_us,packet_pcm_us,packet_open_us,packet_capacity_us;

#if CONFIG_CHIME_BT_VENDOR_CODECS
/* IEEE CRC32, fixed 16-entry flash table. Diagnostics only: no retry, acceptance
 * shortcut, allocation, lock or UART on a successful packet. */
static uint32_t input_crc32(const uint8_t *data,uint32_t bytes)
{
    static const uint32_t table[16]={
        0x00000000u,0x1db71064u,0x3b6e20c8u,0x26d930acu,
        0x76dc4190u,0x6b6b51f4u,0x4db26158u,0x5005713cu,
        0xedb88320u,0xf00f9344u,0xd6d6a3e8u,0xcb61b38cu,
        0x9b64c2b0u,0x86d3d2d4u,0xa00ae278u,0xbdbdf21cu};
    uint32_t crc=UINT32_MAX;
    for(uint32_t i=0;i<bytes;++i){
        crc^=data[i];crc=(crc>>4)^table[crc&15u];crc=(crc>>4)^table[crc&15u];
    }
    return ~crc;
}
static bool input_span_matches(const input_evidence_t *a,const input_evidence_t *b)
{
    return a->data==b->data&&a->bytes==b->bytes;
}
static input_evidence_t input_evidence(const esp_a2d_audio_buff_t *buffer,const input_evidence_t *baseline)
{
    input_evidence_t e={.recorded=true,.data=buffer->data,.bytes=buffer->data_len,.timestamp=buffer->timestamp};
    /* Never follow a changed pointer or expanded/truncated length. Metadata
     * itself remains reportable, but crc32/head must be explicitly unavailable. */
    if(!e.data||!e.bytes||e.bytes>RAW_MAX_BYTES||
       (baseline&&baseline->recorded&&!input_span_matches(&e,baseline)))return e;
    int64_t began=esp_timer_get_time();
    e.crc32=input_crc32(e.data,e.bytes);
    memcpy(e.head,e.data,e.bytes<sizeof(e.head)?e.bytes:sizeof(e.head));e.hashed=true;
    uint64_t elapsed=(uint64_t)(esp_timer_get_time()-began);
    e.capture_us=elapsed>UINT32_MAX?UINT32_MAX:(uint32_t)elapsed;
    return e;
}
static const char *input_crc_comparison(const input_evidence_t *a,const input_evidence_t *b)
{
    return !a->hashed||!b->hashed?"unavailable":a->crc32==b->crc32?"match":"changed";
}
static const char *input_metadata_comparison(const input_evidence_t *a,const input_evidence_t *b)
{
    return !a->recorded||!b->recorded?"unavailable":
        input_span_matches(a,b)&&a->timestamp==b->timestamp?"match":"changed";
}
static void input_evidence_log(const packet_t *p,const input_evidence_t *fault,const char *stage)
{
    const input_evidence_t *samples[]={&p->submitted,&p->worker,fault};
    static const char *const phases[]={"submit","worker","fault"};
    for(unsigned i=0;i<3;++i){const input_evidence_t *e=samples[i];
        ESP_LOGE(TAG,"LDAC INPUT stage=%s phase=%s epoch=%lu buffer=%p recorded=%u hashed=%u bytes=%lu ts=%lu data=%p crc32=%08lx head=%02x:%02x:%02x:%02x sample_us=%lu",
                 stage,phases[i],(unsigned long)p->epoch,(void *)p->buffer,(unsigned)e->recorded,(unsigned)e->hashed,
                 (unsigned long)e->bytes,(unsigned long)e->timestamp,(const void *)e->data,(unsigned long)e->crc32,
                 e->head[0],e->head[1],e->head[2],e->head[3],(unsigned long)e->capture_us);
    }
    ESP_LOGE(TAG,"LDAC INPUT compare submit_worker=%s worker_fault=%s submit_fault=%s metadata_submit_worker=%s metadata_worker_fault=%s scope=original-sdk-buffer; CRC match is not proof against collisions or concurrent writes",
             input_crc_comparison(&p->submitted,&p->worker),input_crc_comparison(&p->worker,fault),
             input_crc_comparison(&p->submitted,fault),input_metadata_comparison(&p->submitted,&p->worker),
             input_metadata_comparison(&p->worker,fault));
}
#endif

static void packet_fail(const packet_t *p,esp_err_t error,const char *stage)
{
    if(!cb.valid(p->epoch))return;
    const uint8_t *d=p->buffer->data;unsigned n=p->buffer->data_len;
#if CONFIG_CHIME_BT_VENDOR_CODECS
    bool ldac=p->codec==CHIME_BT_CODEC_LDAC;input_evidence_t fault={0};
    if(ldac){
        const input_evidence_t *baseline=p->submitted.recorded?&p->submitted:p->worker.recorded?&p->worker:NULL;
        fault=input_evidence(p->buffer,baseline);
        /* Do not dereference corrupted metadata even for the generic prefix. */
        d=fault.hashed?fault.head:NULL;
    }
#endif
    ESP_LOGE(TAG,"packet failed codec=%u epoch=%lu stage=%s error=%d rate=%u bytes=%u ts=%lu head=%02x:%02x:%02x:%02x raw_q=%u previous_us=%u max_us=%u decode_us=%u pcm_us=%u open_us=%u",
        (unsigned)p->codec,(unsigned long)p->epoch,stage,(int)error,p->rate,n,
        (unsigned long)p->buffer->timestamp,n&&d?d[0]:0,n>1&&d?d[1]:0,n>2&&d?d[2]:0,n>3&&d?d[3]:0,
        (unsigned)uxQueueMessagesWaiting(raw_queue),atomic_load(&last_packet_us),atomic_load(&max_packet_us),atomic_load(&last_decode_us),atomic_load(&last_pcm_us),atomic_load(&last_open_us));
    cb.fail(p->epoch,error);
#if CONFIG_CHIME_BT_VENDOR_CODECS
    if(ldac)input_evidence_log(p,&fault,stage); /* after stream invalidation */
#endif
}

#if CONFIG_CHIME_BT_VENDOR_CODECS
static uint8_t vendor_fragment[1275];
static unsigned fragment_size,fragment_remaining;
static uint32_t fragment_timestamp;
#if CONFIG_CHIME_BT_LHDC_V5
static bool lhdc_sequence_valid;
static uint8_t lhdc_sequence;
#endif
#endif

static void close_decoder(void)
{
    if (decoder) {
        if (active_codec == CHIME_BT_CODEC_AAC) chime_s31_close(decoder);
#if CONFIG_CHIME_BT_VENDOR_CODECS
#if CONFIG_CHIME_BT_LHDC_V5
        else if(active_codec==CHIME_BT_CODEC_LHDC_V5)chime_lhdc_close(decoder);
#endif
        else if(active_codec==CHIME_BT_CODEC_LDAC)chime_ldac_close(decoder);
        else if(active_codec==CHIME_BT_CODEC_OPUS)chime_s31_close(decoder);
        else if(active_codec==CHIME_BT_CODEC_APTX||active_codec==CHIME_BT_CODEC_APTX_HD)chime_s31_close(decoder);
#endif
        else chime_s31_close(decoder);
        decoder = NULL;
    }
}
static bool open_decoder(const packet_t *p)
{
    close_decoder();
    active_codec = p->codec;
    active_epoch = p->epoch;
#if CONFIG_CHIME_BT_VENDOR_CODECS
    fragment_size=fragment_remaining=0;
#if CONFIG_CHIME_BT_LHDC_V5
    lhdc_sequence_valid=false;
#endif
#endif
    int err=CHIME_CODEC_ERROR;
    if(p->codec==CHIME_BT_CODEC_AAC)decoder=chime_s31_open(CHIME_S31_AAC,p->rate,p->channels);
    else if(p->codec==CHIME_BT_CODEC_SBC)decoder=chime_s31_open(CHIME_S31_SBC,p->rate,p->channels);
#if CONFIG_CHIME_BT_VENDOR_CODECS
#if CONFIG_CHIME_BT_LHDC_V5
    else if(p->codec==CHIME_BT_CODEC_LHDC_V5)decoder=chime_lhdc_open(p->rate,p->channels);
#endif
    else if(p->codec==CHIME_BT_CODEC_LDAC)decoder=chime_ldac_open(p->rate,p->channels);
    else if(p->codec==CHIME_BT_CODEC_OPUS)decoder=chime_s31_open(CHIME_S31_OPUS,p->rate,p->channels);
    else if(p->codec==CHIME_BT_CODEC_APTX||p->codec==CHIME_BT_CODEC_APTX_HD)
        decoder=chime_s31_open(p->codec==CHIME_BT_CODEC_APTX_HD?CHIME_S31_APTX_HD:CHIME_S31_APTX,p->rate,p->channels);
#endif
    else return false;
    if(decoder)err=CHIME_CODEC_OK;
    if(err != CHIME_CODEC_OK)ESP_LOGE(TAG,"open codec=%u error=%d",(unsigned)p->codec,(int)err);
#ifdef ESP_PLATFORM
    if(err==CHIME_CODEC_OK&&decoder){
        atomic_store(&open_free_dma_bytes,heap_caps_get_free_size(MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT|MALLOC_CAP_DMA));
        atomic_store(&open_free_rtc_bytes,heap_caps_get_free_size(MALLOC_CAP_RTCRAM|MALLOC_CAP_8BIT));
    }
#endif
    return err == CHIME_CODEC_OK && decoder;
}
#if CONFIG_CHIME_BT_VENDOR_CODECS
/* Raw callback is post-AVDTP: no second RTP header to skip. The one-byte
 * Opus/LDAC payload headers carry fragment flags/count. */
static bool vendor_payload(const packet_t *p,uint8_t **data,uint32_t *length,unsigned *frames)
{
    if(!*length)return false;
    bool ldac=p->codec==CHIME_BT_CODEC_LDAC;
    unsigned limit=ldac?515U:(unsigned)sizeof(vendor_fragment);
    uint8_t h=**data;unsigned count=h&15U;bool fragmented=(h&0x80)!=0;
    ++*data;--*length;
    if(h&0x10)return false;
    if(!fragmented){
        fragment_size=fragment_remaining=0;
        *frames=count;
        return h==count&&count&&*length>0&&(ldac||(*frames==1&&*length<=limit));
    }
    if(!count||!*length)return false;
    bool first=(h&0x40)!=0,last=(h&0x20)!=0;
    if(first){
        if(last||count<2)return false;
        fragment_size=0;fragment_remaining=count;fragment_timestamp=p->buffer->timestamp;
    }else if(!fragment_remaining||count+1!=fragment_remaining||fragment_timestamp!=p->buffer->timestamp)return false;
    if(*length>limit-fragment_size||last!=(count==1))return false;
    memcpy(vendor_fragment+fragment_size,*data,*length);fragment_size+=*length;fragment_remaining=count;
    if(!last){*length=0;return true;}
    *data=vendor_fragment;*length=fragment_size;*frames=1;fragment_size=fragment_remaining=0;return true;
}
/* Fault-only diagnostic, never a steady-state packet logger. Invalidate the
 * stream BEFORE UART output. The old four-byte packet prefix cannot tell us
 * which later ABR frame failed (132/164/220/330 can coexist across packets). */
static void ldac_layout_fail(const packet_t *p,const uint8_t *data,uint32_t length,
                             unsigned frames,unsigned index,uint32_t offset,const char *reason)
{
    packet_fail(p,ESP_ERR_INVALID_SIZE,"frame-layout");
    uint32_t remaining=length-offset;
    const uint8_t *bad=data+offset;
    unsigned b0=remaining?bad[0]:0,b1=remaining>1?bad[1]:0,b2=remaining>2?bad[2]:0;
    unsigned declared=remaining>=3?(((b1&7u)<<6)|(b2>>2))+4u:0;
    ESP_LOGE(TAG,"LDAC layout reason=%s frame=%u/%u payload_offset=%u remaining=%u declared_bytes=%u head=%02x:%02x:%02x frame_rate_id=%u mode=%u expected_rate=%u payload_bytes=%u",
             reason,index+1u,frames,(unsigned)offset,(unsigned)remaining,declared,b0,b1,b2,
             b1>>5,(b1>>3)&3u,p->rate,(unsigned)length);
    /* Capture the complete usual <=1024-byte failing packet for deterministic
     * replay; larger packets explicitly mark truncation. No allocation, no
     * retry/resync, and no data from outside the validated SDK buffer. */
    unsigned n=p->buffer->data_len,captured=n<1024u?n:1024u;
    if(p->worker.recorded&&(p->buffer->data!=p->worker.data||n!=p->worker.bytes)){
        ESP_LOGE(TAG,"LDAC rejected packet capture=0/%u; metadata changed; no payload dereference",n);return;
    }
    ESP_LOGE(TAG,"LDAC rejected packet capture=%u/%u; offsets are original post-AVDTP bytes",captured,n);
    for(unsigned at=0;at<captured;at+=32u){
        char hex[32u*3u+1u];unsigned used=0;
        for(unsigned j=at;j<captured&&j<at+32u;++j){
            static const char digits[]="0123456789abcdef";
            uint8_t v=p->buffer->data[j];hex[used++]=digits[v>>4];hex[used++]=digits[v&15u];hex[used++]=' ';
        }
        hex[used]=0;ESP_LOGE(TAG,"LDAC packet +%04x: %s",at,hex);
    }
}
/* A2DP aggregation is a production burst, not permission to overflow PCM.
 * Only the decoder owns this producer; the consumer can only increase space.
 * Never hold the Bluetooth spinlock while waiting or decoding. */
#define LDAC_PCM_CAPACITY_TIMEOUT_US 100000U
static bool ldac_wait_for_capacity(const packet_t *p)
{
    if(cb.service)cb.service(p->epoch);
    if(!cb.pcm_capacity||cb.pcm_capacity(p->epoch)>=128U)return true;
    int64_t began=esp_timer_get_time();bool ready=false,timed_out=false;
    while(!atomic_load(&stopping)&&cb.valid(p->epoch)){
        if(esp_timer_get_time()-began>=LDAC_PCM_CAPACITY_TIMEOUT_US){timed_out=true;break;}
        if(cb.service)cb.service(p->epoch);
        if(cb.pcm_capacity(p->epoch)>=128U){ready=true;break;}
        vTaskDelay(1); /* one tick, not a busy loop; USB task remains runnable */
    }
    unsigned elapsed=(unsigned)(esp_timer_get_time()-began);
    packet_capacity_us+=elapsed;
    atomic_fetch_add(&stats_revision,1);
    atomic_fetch_add(&stats_capacity_waits,1);
    if(timed_out)atomic_fetch_add(&stats_capacity_timeouts,1);
    atomic_fetch_add(&stats_revision,1);
    if(timed_out)packet_fail(p,ESP_ERR_TIMEOUT,"pcm-capacity-timeout");
    return ready;
}
static bool opus_twenty_ms(const uint8_t *data,uint32_t length)
{
    if(!length)return false;
    unsigned toc=data[0],samples;
    if(toc&0x80)samples=120U<<((toc>>3)&3U);
    else if((toc&0x60)==0x60)samples=(toc&8)?960U:480U;
    else {unsigned mode=(toc>>3)&3U;samples=mode==3?2880U:480U<<mode;}
    unsigned code=toc&3U,frames=code==0?1U:code==3?(length>1?data[1]&63U:0U):2U;
    return frames&&samples*frames==960U;
}
#endif
static void process_packet(const packet_t *p)
{
    if (!cb.valid(p->epoch)) return;
#if CONFIG_CHIME_BT_VENDOR_CODECS
    packet_t observed;
    if(p->codec==CHIME_BT_CODEC_LDAC){
        observed=*p;
        observed.worker=input_evidence(p->buffer,p->submitted.recorded?&p->submitted:NULL);p=&observed;
        if(p->submitted.recorded&&!input_span_matches(&p->submitted,&p->worker)){
            packet_fail(p,ESP_ERR_INVALID_SIZE,"input-metadata");return;
        }
        if(!p->worker.hashed){packet_fail(p,ESP_ERR_INVALID_SIZE,"input-size");return;}
    }
#endif
    if (!decoder || active_epoch != p->epoch || active_codec != p->codec) {
        int64_t began=esp_timer_get_time();bool opened=open_decoder(p);
        packet_open_us=(unsigned)(esp_timer_get_time()-began);
        /* NULL alone does not prove OOM (configuration/singleton/init can fail). */
        if(!opened){packet_fail(p,ESP_FAIL,"open");return;}
    }
    bool timing_sent=false;
    chime_codec_input_t in = {.buffer=p->buffer->data, .len=p->buffer->data_len};
#if CONFIG_CHIME_BT_VENDOR_CODECS
    unsigned frames=0;
#if CONFIG_CHIME_BT_LHDC_V5
    if(p->codec==CHIME_BT_CODEC_LHDC_V5){
        /* Reference V5: count is bits 7:2, sequence is the second octet.
         * The low two bits are not part of the count and upstream does not
         * require zero (real phone packet: 09:02 + two 252-byte frames).
         * Do not interpret these as SBC/LDAC fragment flags. RTP was already
         * removed once; still validate every full frame before publishing. */
        if(in.len<2||!(frames=in.buffer[0]>>2)||
           (lhdc_sequence_valid&&in.buffer[1]!=(uint8_t)(lhdc_sequence+1U))){
            packet_fail(p,ESP_ERR_INVALID_SIZE,"frame-layout");return;
        }
        lhdc_sequence=in.buffer[1];lhdc_sequence_valid=true;in.buffer+=2;in.len-=2;
        uint8_t *next=in.buffer;uint32_t remaining=in.len;
        for(unsigned i=0;i<frames;++i){
            size_t n=chime_lhdc_frame_bytes(next,remaining);
            if(!n){packet_fail(p,ESP_ERR_INVALID_SIZE,"frame-layout");return;}
            next+=n;remaining-=(uint32_t)n;
        }
        if(remaining){packet_fail(p,ESP_ERR_INVALID_SIZE,"frame-layout");return;}
    }
#endif
    if(p->codec==CHIME_BT_CODEC_OPUS||p->codec==CHIME_BT_CODEC_LDAC){
        if(!vendor_payload(p,&in.buffer,&in.len,&frames)){packet_fail(p,ESP_ERR_INVALID_SIZE,"payload-header/fragment");return;}
        if(p->codec==CHIME_BT_CODEC_LDAC){
            uint32_t ts=p->buffer->timestamp;
            atomic_fetch_add(&stats_revision,1);
            if(ldac_previous_seen){
                atomic_store(&stats_ldac_ts_delta,ts-ldac_previous_ts);
                atomic_store(&stats_ldac_previous_frames,ldac_previous_samples);
                if(ts==ldac_previous_ts){
                    atomic_fetch_add(&stats_ldac_ts_same,1);
                    if(p->worker.hashed&&p->worker.crc32==ldac_previous_crc&&p->worker.bytes==ldac_previous_bytes)
                        atomic_fetch_add(&stats_ldac_same_input,1);
                }
            }
            atomic_fetch_add(&stats_revision,1);
            ldac_previous_seen=p->worker.hashed;ldac_previous_ts=ts;
            ldac_previous_crc=p->worker.crc32;ldac_previous_bytes=p->worker.bytes;
            ldac_previous_samples=frames*128U;
        }
        if(!in.len)return; /* bounded fragment collection, not a decoder call */
        if(p->codec==CHIME_BT_CODEC_OPUS&&!opus_twenty_ms(in.buffer,in.len)){packet_fail(p,ESP_ERR_NOT_SUPPORTED,"packet-duration");return;}
    }
    if(p->codec==CHIME_BT_CODEC_LDAC){
        /* Header/fragment assembly already removed the one-byte payload header.
         * Validate all frames/count before publishing any PCM. */
        unsigned count=frames;uint32_t remaining=in.len;
        uint8_t *next=in.buffer;
        for(unsigned i=0;i<count;++i){
            size_t n=chime_ldac_frame_bytes(next,remaining,p->rate);
            if(!n){ldac_layout_fail(p,in.buffer,in.len,count,i,in.len-remaining,"invalid-or-truncated-frame");return;}
            next+=n;remaining-=(uint32_t)n;
        }
        if(remaining){ldac_layout_fail(p,in.buffer,in.len,count,count,in.len-remaining,"trailing-bytes-after-frame-count");return;}
        atomic_fetch_add(&stats_revision,1);atomic_fetch_add(&stats_ldac_frames,count);atomic_fetch_add(&stats_revision,1);
    }
    if(p->codec==CHIME_BT_CODEC_APTX||p->codec==CHIME_BT_CODEC_APTX_HD){
        unsigned frame=p->codec==CHIME_BT_CODEC_APTX_HD?6U:4U;
        if(in.len%frame){packet_fail(p,ESP_ERR_INVALID_SIZE,"frame-layout");return;}
    }
#endif
    /* A packet cannot make more than RAW_MAX_BYTES iterations: successful
     * iterations must consume input. Reject zero-progress and corrupt lengths. */
    while (in.len && !atomic_load(&stopping) && cb.valid(p->epoch)) {
#if CONFIG_CHIME_BT_VENDOR_CODECS
        if(p->codec==CHIME_BT_CODEC_LDAC&&!ldac_wait_for_capacity(p))return;
#endif
        in.consumed = 0;
        chime_codec_output_t out = {.buffer=output, .len=PCM_BYTES};
        chime_codec_info_t info = {0};
        int err;
        int64_t decode_began=esp_timer_get_time();
#if CONFIG_CHIME_BT_VENDOR_CODECS
#if CONFIG_CHIME_BT_LHDC_V5
        if(p->codec==CHIME_BT_CODEC_LHDC_V5){
            size_t n=chime_lhdc_frame_bytes(in.buffer,in.len);
            out.decoded_size=(uint32_t)chime_lhdc_decode(decoder,in.buffer,n,output,PCM_BYTES);
            in.consumed=(uint32_t)n;err=out.decoded_size==960?CHIME_CODEC_OK:-1;
            info=(chime_codec_info_t){.sample_rate=p->rate,.channel=2,.bits_per_sample=16};
        }else
#endif
        if(p->codec==CHIME_BT_CODEC_LDAC){
            size_t n=chime_ldac_frame_bytes(in.buffer,in.len,p->rate);
            out.decoded_size=(uint32_t)chime_ldac_decode(decoder,in.buffer,n,output,PCM_BYTES);
            in.consumed=(uint32_t)n;err=out.decoded_size==512?CHIME_CODEC_OK:-1;
            info=(chime_codec_info_t){.sample_rate=p->rate,.channel=2,.bits_per_sample=16};
        }else
#endif
        {
            err=chime_s31_decode(decoder,&in,&out,&info);
            if(p->codec==CHIME_BT_CODEC_OPUS&&err==CHIME_CODEC_OK&&
               (in.consumed!=in.len||out.decoded_size!=3840)){
                packet_fail(p,ESP_ERR_INVALID_SIZE,"frame-layout");return;
            }
        }
        packet_decode_us+=(unsigned)(esp_timer_get_time()-decode_began);
        if (err != CHIME_CODEC_OK || !in.consumed || in.consumed > in.len ||
            out.decoded_size > PCM_BYTES || out.decoded_size % (p->channels * 2U)) {
            ESP_LOGE(TAG,"decode codec=%u error=%d input=%u consumed=%u output=%u",
                (unsigned)p->codec,(int)err,(unsigned)in.len,(unsigned)in.consumed,(unsigned)out.decoded_size);
            packet_fail(p,ESP_ERR_INVALID_SIZE,"decode");
            return;
        }
        if (out.decoded_size) {
            if (info.sample_rate != p->rate || info.channel != p->channels || info.bits_per_sample != 16) {
                ESP_LOGE(TAG,"decoded format mismatch rate=%u channels=%u bits=%u",
                    (unsigned)info.sample_rate,(unsigned)info.channel,(unsigned)info.bits_per_sample);
                packet_fail(p,ESP_ERR_NOT_SUPPORTED,"decoded-format");
                return;
            }
#if CONFIG_CHIME_BT_VENDOR_CODECS
            if(p->codec==CHIME_BT_CODEC_LDAC){
                atomic_fetch_add(&stats_revision,1);atomic_fetch_add(&stats_ldac_pcm_frames,out.decoded_size/(p->channels*2U));atomic_fetch_add(&stats_revision,1);
            }
#endif
            packet_pcm_bytes+=out.decoded_size;
            int64_t pcm_began=esp_timer_get_time();
            if(!timing_sent&&cb.timing&&cb.valid(p->epoch)){cb.timing(p->epoch,p->enqueued_us);timing_sent=true;}
            cb.pcm(output, out.decoded_size, p->epoch);
            packet_pcm_us+=(unsigned)(esp_timer_get_time()-pcm_began);
        }
        in.buffer += in.consumed;
        in.len -= in.consumed;
    }
}
#if CONFIG_CHIME_LDAC_PIPELINE_TEST || CONFIG_CHIME_BT_VENDOR_CODECS
/* Match the S31 upstream decoder worker budget (Opus/LHDC scratch). */
#define DECODE_STACK_BYTES 8192
#else
#define DECODE_STACK_BYTES 6144
#endif
static void decode_task(void *arg)
{
    (void)arg;
    packet_t p;
#ifdef ESP_PLATFORM
    ESP_LOGI(TAG,"worker memory stack_sample=%p output=%p; HP SRAM=0x2f..., RTC/LP=0x2e... on S31",
        (void *)&p,(void *)output);
#endif
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
    /* Already pinned CPU1. Probing here must NEVER migrate source_pcm. */
    chime_bt_src_accel_init();
#endif
    uint32_t timing_epoch=0;unsigned epoch_packets=0;bool slow_reported=false;
    while (!atomic_load(&stopping)) {
        if(cb.service&&active_epoch)cb.service(active_epoch);
        if (xQueueReceive(raw_queue, &p, pdMS_TO_TICKS(cb.service?1:20)) == pdTRUE) {
            if(timing_epoch!=p.epoch){timing_epoch=p.epoch;slow_reported=false;epoch_packets=0;
                stats_reset(p.epoch);}
            packet_pcm_bytes=packet_decode_us=packet_pcm_us=packet_open_us=packet_capacity_us=0;
            int64_t began=esp_timer_get_time();
            uint64_t waited=(uint64_t)began>=p.enqueued_us?(uint64_t)began-p.enqueued_us:0;
            unsigned wait_us=waited>UINT32_MAX?UINT32_MAX:(unsigned)waited;
            unsigned raw_after=(unsigned)uxQueueMessagesWaiting(raw_queue);
            atomic_fetch_add(&stats_revision,1);
            atomic_store(&stats_wait_us,wait_us);atomic_store(&stats_raw_after,raw_after);
            if(wait_us>atomic_load(&stats_max_wait_us))atomic_store(&stats_max_wait_us,wait_us);
            if(raw_after>atomic_load(&stats_raw_max_after))atomic_store(&stats_raw_max_after,raw_after);
            atomic_fetch_add(&stats_revision,1);
            process_packet(&p);
            unsigned elapsed=(unsigned)(esp_timer_get_time()-began);
            ++epoch_packets;
            unsigned budget=p.rate&&p.channels?(unsigned)((uint64_t)packet_pcm_bytes*1000000U/(p.rate*p.channels*2U)):0;
            atomic_fetch_add(&stats_revision,1);
            atomic_store(&stats_packets,epoch_packets);atomic_store(&stats_audio_us,budget);
            if(budget&&elapsed>budget)atomic_fetch_add(&stats_slow,1);
            atomic_store(&last_decode_us,packet_decode_us);atomic_store(&last_pcm_us,packet_pcm_us);atomic_store(&last_open_us,packet_open_us);
            if(packet_decode_us>atomic_load(&max_decode_us))atomic_store(&max_decode_us,packet_decode_us);
            if(packet_pcm_us>atomic_load(&max_pcm_us))atomic_store(&max_pcm_us,packet_pcm_us);
            atomic_store(&last_packet_us,elapsed);
            atomic_store(&stats_capacity_us,packet_capacity_us);
            if(packet_capacity_us>atomic_load(&stats_capacity_max_us))atomic_store(&stats_capacity_max_us,packet_capacity_us);
            if(elapsed>atomic_load(&max_packet_us))atomic_store(&max_packet_us,elapsed);
            atomic_fetch_add(&stats_revision,1);
            if(budget&&elapsed>budget&&(!slow_reported||epoch_packets==8)){
                unsigned stages=packet_decode_us+packet_pcm_us+packet_open_us+packet_capacity_us;
                ESP_LOGW(TAG,"packet slower than audio codec=%u epoch=%lu packet=%u packet_us=%u audio_us=%u raw_q=%u decode_us=%u pcm_us=%u open_us=%u capacity_us=%u other_us=%u; WALL times include preemption and capacity wait, not CPU utilization",
                    (unsigned)p.codec,(unsigned long)p.epoch,epoch_packets,elapsed,budget,(unsigned)uxQueueMessagesWaiting(raw_queue),
                    packet_decode_us,packet_pcm_us,packet_open_us,packet_capacity_us,elapsed>stages?elapsed-stages:0U);
                slow_reported=true;
            }
            esp_a2d_audio_buff_free(p.buffer);
        }
        if (decoder && !cb.valid(active_epoch)) close_decoder();
    }
    close_decoder();
    while (xQueueReceive(raw_queue, &p, 0) == pdTRUE) esp_a2d_audio_buff_free(p.buffer);
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
    chime_bt_src_accel_deinit();
#endif
    /* Last access to shared resources; caller may now delete queue/output. */
    atomic_store(&exited, true);
    vTaskDelete(NULL);
}
esp_err_t chime_bt_decoder_start(const chime_bt_decoder_callbacks_t *callbacks)
{
    if (raw_queue) return ESP_ERR_INVALID_STATE;
    cb = *callbacks;
    stats_reset(0);
    raw_queue = xQueueCreate(RAW_DEPTH, sizeof(packet_t));
    output = malloc(PCM_BYTES);
    if (!raw_queue || !output) goto fail;
    atomic_store(&stopping, false);
    atomic_store(&exited, false);
    if (xTaskCreatePinnedToCore(decode_task, "bt_decode", DECODE_STACK_BYTES, NULL, 5, NULL, DECODE_CORE) != pdPASS) goto fail;
#if defined(ESP_PLATFORM) && CONFIG_CHIME_BT_VENDOR_CODECS
    chime_ldac_memory_stats_t memory;chime_ldac_memory_snapshot(&memory);
    ESP_LOGI(TAG,"LDAC fast workspace base=%p reserved=%lu; no RTC/PSRAM fallback",
        (void *)memory.base,(unsigned long)memory.reserved_bytes);
#endif
    ESP_LOGI(TAG,"worker ready core=%u priority=5 raw_depth=%u; decode only while BT output enabled",(unsigned)DECODE_CORE,(unsigned)RAW_DEPTH);
#if CONFIG_CHIME_BT_VENDOR_CODECS
#if CONFIG_CHIME_BT_LHDC_V5
    ESP_LOGI(TAG,"LDAC/LHDC backend=WillyBilly06 reference 71071a7 lhdc_v5=1; PCM16 44.1/48k");
#else
    ESP_LOGI(TAG,"LDAC/LHDC backend=WillyBilly06 reference 71071a7 lhdc_v5=0; PCM16 44.1/48k");
#endif
#endif
    return ESP_OK;
fail:
    if (raw_queue) vQueueDelete(raw_queue);
    raw_queue = NULL;
    free(output); output = NULL;
    return ESP_ERR_NO_MEM;
}
void chime_bt_decoder_submit(esp_a2d_audio_buff_t *buffer, uint32_t epoch,
                            chime_bt_codec_t codec, unsigned rate, unsigned channels)
{
    if (!buffer) return;
    if (!raw_queue || atomic_load(&stopping) || !cb.valid(epoch)) goto discard;
    packet_t p = {.buffer=buffer, .epoch=epoch, .codec=codec, .rate=rate, .channels=channels};
    if (!buffer->data || !buffer->data_len || buffer->data_len > RAW_MAX_BYTES ||
        !rate || (channels != 1 && channels != 2)) {
        packet_fail(&p,ESP_ERR_INVALID_SIZE,"input-size");
        goto discard;
    }
#if CONFIG_CHIME_BT_VENDOR_CODECS
    if(codec==CHIME_BT_CODEC_LDAC)p.submitted=input_evidence(buffer,NULL);
#endif
    p.enqueued_us=(uint64_t)esp_timer_get_time();
    if (xQueueSend(raw_queue, &p, 0) == pdTRUE) return;
    packet_fail(&p,ESP_ERR_NO_MEM,"raw-queue-full");
discard:
    esp_a2d_audio_buff_free(buffer);
}
bool chime_bt_decoder_stop(void)
{
    if (!raw_queue) return true;
    atomic_store(&stopping, true);
    for (unsigned i=0; i<200 && !atomic_load(&exited); ++i) vTaskDelay(pdMS_TO_TICKS(5));
    if (!atomic_load(&exited)) return false;
    vQueueDelete(raw_queue); raw_queue = NULL;
    free(output); output = NULL;
    return true;
}
#endif

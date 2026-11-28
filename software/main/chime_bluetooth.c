#include "sdkconfig.h"
#include "chime_bluetooth.h"
#include "chime_bt_pcm.h"
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
#include "chime_bt_ready.h"
#endif
#include "chime_bt_flow.h"
#include "chime_render.h"
#include "chime_bt_delay.h"
#include "chime_bt_vendor.h"
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
#include "chime_bt_decoder.h"
#endif
#include <stdatomic.h>
#include <string.h>
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_gap_bt_api.h"
#include "esp_a2dp_api.h"
#include "esp_avrc_api.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG="chime_bt";
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static chime_bt_pcm_t queue;
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#else
#define RTC_DATA_ATTR
#define RTC_NOINIT_ATTR
#endif
/* Short-latency ready handoff stays in main SRAM; bulk PCM is banked. */
static chime_bt_ready_t ready48;
/* Only scalar reads/writes, copied to main-SRAM SRC job before any PIE load.
 * Keep the exact 8192-frame capacity, not a larger jitter buffer. */
static RTC_NOINIT_ATTR int16_t pcm_tail[6144][2];
static uint64_t delivered_input;
static unsigned ready_input[CHIME_BT_READY_BLOCKS];
static esp_err_t compute_q27(int32_t *pcm,size_t frames,bool *ready,uint32_t epoch,uint32_t output_id,unsigned *input_frames);

#endif
static chime_bt_flow_t flow;
static chime_bt_delay_t delay;
static uint32_t delay_generation,delay_pending_generation;
static uint64_t delay_sent_us,delay_retry_us;
/* Keep a retired request in flight until its callback arrives: SDK completion
 * has no link/epoch token. Never reuse that slot after a timeout. */
static bool delay_inflight;
static uint16_t delay_inflight_value;
#define DELAY_FIXED_US (CHIME_BT_DELAY_DMA_US + CONFIG_CHIME_BT_DELAY_EXTRA_MS*1000U)
static chime_bluetooth_status_t state;
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
static bool pipeline_mode_locked(void)
{
    return state.codec==CHIME_BT_CODEC_LDAC&&state.rate==48000;
}
#endif
static bool rendering,closing;
#if CONFIG_CHIME_LDAC_REPLAY_TEST
static uint32_t replay_token; /* survives fault epoch invalidation for STOP */
#endif
static bool session_running_locked(void)
{
    return (state.connected&&state.playing)||state.simulated;
}
static unsigned absolute_volume;
static bool volume_notification,volume_dirty,avrc_connected,remote_volume_valid;
static uint8_t avrc_peer[6],audio_peer[6];
static uint32_t gap_run,output_counter;
static uint64_t last_rx_us,last_pcm_us,rebuffer_started;
static bool rx_seen,pcm_seen;
static void delay_sample_locked(uint64_t now,unsigned residence)
{
    if(rendering&&queue.primed&&!gap_run&&state.error==ESP_OK)
        chime_bt_delay_sample(&delay,now,(unsigned)queue.count,state.rate,residence,DELAY_FIXED_US);
}
static int volume_db(unsigned v);
static unsigned elapsed_us(uint64_t now,uint64_t before)
{
    uint64_t n=now>=before?now-before:0;
    return n>UINT32_MAX?UINT32_MAX:(unsigned)n;
}
static unsigned elapsed_ms(uint64_t now,uint64_t before)
{
    uint64_t ms=now>=before?(now-before)/1000U:0;
    return ms>UINT32_MAX?UINT32_MAX:(unsigned)ms;
}
/* Called under mux, before resetting the queue. Preserve diagnostics across
 * pause/STOP/fault; only a new output admission clears them. */
static void pcm_metrics_locked(void)
{
    if(!rendering)return;
    state.queue_min=queue.render_min==CHIME_BT_QUEUE_FRAMES&&!queue.render_max?0:queue.render_min;
    state.target_ms=queue.target_ms;state.target_frames=queue.target_frames;
    state.target_raises=queue.target_raises;state.target_decays=queue.target_decays;
    state.queue_max=queue.render_max;state.clamp_blocks=queue.clamp_blocks;
    state.discarded_frames=queue.discarded_frames;state.pcm_frames=queue.pushed_frames;state.consumed_frames=queue.consumed_frames;
    state.rendered_frames=queue.rendered_frames;
}
static uint64_t output_started,starved_since;
static atomic_int profile; /* 0=pending, 1=ready, 2=deinitialized, -1=failed */
static bool sink_requested,tg_requested;
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
#if CONFIG_CHIME_BT_VENDOR_CODECS
#if CONFIG_CHIME_BT_LHDC_V5
#define ENDPOINT_COUNT 7
#define LAST_VENDOR_CODEC CHIME_VENDOR_LHDC_V5
#else
#define ENDPOINT_COUNT 6
#define LAST_VENDOR_CODEC CHIME_VENDOR_LDAC
#endif
#else
#define ENDPOINT_COUNT 2
#endif
#define ENDPOINT_MASK ((1<<ENDPOINT_COUNT)-1)
static atomic_int endpoints;
static esp_a2d_conn_hdl_t connection;
#endif
const char *chime_bluetooth_codec_name(chime_bt_codec_t codec)
{
    return codec==CHIME_BT_CODEC_LHDC_V5?"LHDC V5":codec==CHIME_BT_CODEC_LDAC?"LDAC":codec==CHIME_BT_CODEC_OPUS?"Opus":codec==CHIME_BT_CODEC_APTX_HD?"aptX HD":codec==CHIME_BT_CODEC_APTX?"aptX":codec==CHIME_BT_CODEC_AAC?"AAC":codec==CHIME_BT_CODEC_SBC?"SBC":"none";
}

/* Called only after releasing mux, once for the latched stream fault.
 * Capture queue occupancy before invalidation clears it. No logging in ISR or
 * per-block success paths, and no relaxation of fail-closed ownership. */
static void stream_fault_log(const char *reason,const chime_bluetooth_status_t *s,unsigned queued)
{
    /* Capture BEFORE UART (and a possible retry), not a second later when
     * the periodic owner monitor eventually gets scheduled. No nested locks. */
    chime_bt_flow_t trace;
    portENTER_CRITICAL(&mux);trace=flow;portEXIT_CRITICAL(&mux);
    chime_render_stats_t rs;chime_render_snapshot(&rs);
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    chime_bt_decoder_stats_t ds;bool decoder_stats=chime_bt_decoder_snapshot(&ds);
#endif
    ESP_LOGE(TAG,"stream stopped reason=%s codec=%s epoch=%lu error=%d rate=%u queued=%u packets=%lu overflows=%lu underruns=%lu gap_events=%lu gap_blocks=%lu; pause/play or reconnect to retry",
        reason,chime_bluetooth_codec_name(s->codec),(unsigned long)s->epoch,s->error,s->rate,queued,
        (unsigned long)s->packets,(unsigned long)s->overflows,(unsigned long)s->underruns,(unsigned long)s->pcm_gap_events,(unsigned long)s->pcm_gap_blocks);
    ESP_LOGE(TAG,"fault PCM pushed=%llu consumed=%llu rendered=%llu discarded=%llu target=%u queue_min=%u queue_max=%u clamp_blocks=%lu",
        (unsigned long long)s->pcm_frames,(unsigned long long)s->consumed_frames,
        (unsigned long long)s->rendered_frames,(unsigned long long)s->discarded_frames,
        s->target_frames,s->queue_min,s->queue_max,(unsigned long)s->clamp_blocks);
    ESP_LOGE(TAG,"fault MIX blocks=%lu bt_short=%lu src_blocks=%lu src_avg_us=%llu compute_us=%lu/%lu src_us=%lu/%lu write_us=%lu/%lu period_us=%lu/%lu misses=%lu yields=%lu src_active=%u src_inflight_us=%lu; WALL times, snapshot before UART",
        (unsigned long)rs.blocks,(unsigned long)rs.bt_short,(unsigned long)rs.src_blocks,
        (unsigned long long)(rs.src_blocks?rs.src_total_us/rs.src_blocks:0),
        (unsigned long)rs.last_compute_us,(unsigned long)rs.max_compute_us,
        (unsigned long)rs.last_src_us,(unsigned long)rs.max_src_us,
        (unsigned long)rs.last_write_us,(unsigned long)rs.max_write_us,
        (unsigned long)rs.last_period_us,(unsigned long)rs.max_period_us,
        (unsigned long)rs.deadline_misses,(unsigned long)rs.unpaced_yields,
        (unsigned)rs.src_active,(unsigned long)rs.src_inflight_us);
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    if(decoder_stats)ESP_LOGE(TAG,"fault MEMORY LDAC fast reserved=%u used=%u peak=%u failures=%u; HP SRAM only",
        ds.ldac_fast_reserved_bytes,ds.ldac_fast_used_bytes,ds.ldac_fast_peak_bytes,ds.ldac_fast_failures);
    if(decoder_stats)ESP_LOGE(TAG,"fault DECODE epoch=%lu completed=%lu wait_us=%u/%u raw_after=%u/%u packet_us=%u/%u ldac_frames=%lu ldac_pcm_frames=%lu same_input=%lu ts_same=%lu ts_delta=%lu previous_frames=%lu; counters include in-flight PCM before admission, repeated input diagnostic ONLY",
        (unsigned long)ds.epoch,(unsigned long)ds.packets,ds.queue_wait_us,ds.max_queue_wait_us,
        ds.raw_after_dequeue,ds.raw_max_after_dequeue,ds.packet_us,ds.max_packet_us,
        (unsigned long)ds.ldac_frames,(unsigned long)ds.ldac_pcm_frames,
        (unsigned long)ds.ldac_same_input,(unsigned long)ds.ldac_ts_same,
        (unsigned long)ds.ldac_ts_delta,(unsigned long)ds.ldac_previous_frames);
#endif
    if(trace.output_id!=s->output_id){
        ESP_LOGE(TAG,"fault FLOW unavailable: output admission changed fault_output=%lu current_output=%lu",
            (unsigned long)s->output_id,(unsigned long)trace.output_id);return;
    }
    ESP_LOGE(TAG,"fault START first_pcm_us=%lu first_render_us=%lu prime_queue=%lu prime_target=%lu; relative to output admission, UINT32_MAX=unseen",
        (unsigned long)trace.first_pcm_us,(unsigned long)trace.first_render_us,
        (unsigned long)trace.prime_queue,(unsigned long)trace.prime_target);
    /* Fixed 20ms wall-time bins; missing indices have no recorded activity.
     * Iterate in chronological order; do not log at all on successful blocks. */
    uint32_t latest=0;
    for(unsigned i=0;i<CHIME_BT_FLOW_BINS;++i)if(trace.bins[i].valid&&trace.bins[i].index>latest)latest=trace.bins[i].index;
    uint32_t first=latest>=CHIME_BT_FLOW_BINS?latest-CHIME_BT_FLOW_BINS+1U:0;
    for(uint32_t i=first;i<=latest;++i){
        const chime_bt_flow_bin_t *b=&trace.bins[i%CHIME_BT_FLOW_BINS];
        if(!b->valid||b->index!=i)continue;
        ESP_LOGE(TAG,"fault FLOW at_ms=%llu span_ms=20 pushed=%lu consumed=%lu rx_packets=%lu queue_end=%lu queue_max=%lu; edge bins may be partial",
            (unsigned long long)i*20U,(unsigned long)b->pushed,(unsigned long)b->consumed,
            (unsigned long)b->packets,(unsigned long)b->queue_end,(unsigned long)b->queue_max);
    }
}
static void recovery_cancel_locked(void)
{
    if(state.stream_state==CHIME_BT_STREAM_REBUFFERING){
        ++state.rebuffer_cancels;
        state.rebuffer_last_wait_ms=elapsed_ms((uint64_t)esp_timer_get_time(),rebuffer_started);
    }
    state.stream_state=state.error==ESP_OK?CHIME_BT_STREAM_IDLE:CHIME_BT_STREAM_FAULT;
}
static void invalidate_locked(void)
{
    pcm_metrics_locked();recovery_cancel_locked();
    ++state.epoch;rendering=false;state.clock_follow=false;starved_since=0;gap_run=0;
    ++delay_generation;chime_bt_delay_reset(&delay);delay_retry_us=0;
    state.delay_estimate_us=state.delay_samples=state.delay_queue_us=state.delay_residence_us=0;
    state.delay_pending=false;state.delay_timed_out=false;
    chime_bt_pcm_reset(&queue,state.rate,state.channels);
}
void chime_bluetooth_snapshot(chime_bluetooth_status_t *s)
{
    portENTER_CRITICAL(&mux);if(rendering)pcm_metrics_locked();
    state.delay_estimate_us=delay.filtered_us;state.delay_samples=delay.samples;
    state.delay_queue_us=delay.queue_us;state.delay_residence_us=delay.residence_us;
    state.rebuffer_wait_ms=state.stream_state==CHIME_BT_STREAM_REBUFFERING?
        elapsed_ms((uint64_t)esp_timer_get_time(),rebuffer_started):0;
    state.delay_fixed_us=DELAY_FIXED_US;*s=state;
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
    s->pipeline_enabled=pipeline_mode_locked();s->pipeline_ready_blocks=ready48.count;
    s->pipeline_high_water=ready48.high_water;s->pipeline_underruns=ready48.underruns;
    s->pipeline_produced=ready48.produced;s->pipeline_consumed=ready48.consumed;
    s->pipeline_pending_input_frames=rendering?(unsigned)(queue.consumed_frames-delivered_input):0;
#endif
    s->queued_frames=(unsigned)queue.count;s->drift_ppm=queue.ppm;s->output_active=rendering;s->primed=queue.primed;portEXIT_CRITICAL(&mux);
}
bool chime_bluetooth_connected(void)
{
    chime_bluetooth_status_t s;chime_bluetooth_snapshot(&s);return s.connected;
}
bool chime_bluetooth_wanted(void)
{
    chime_bluetooth_status_t s;chime_bluetooth_snapshot(&s);
    return s.available&&((s.connected&&s.playing)||s.simulated)&&s.rate&&s.channels&&s.error==ESP_OK;
}
uint32_t chime_bluetooth_epoch(void)
{
    chime_bluetooth_status_t s;chime_bluetooth_snapshot(&s);return s.epoch;
}
int chime_bluetooth_volume(void)
{
    chime_bluetooth_status_t s;chime_bluetooth_snapshot(&s);return s.volume;
}
void chime_bluetooth_fail(esp_err_t err)
{
    portENTER_CRITICAL(&mux);state.error=err;invalidate_locked();portEXIT_CRITICAL(&mux);
}
/* Epoch check and PCM copy are one transaction per <=64-frame chunk.
 * An external decode started before pause/reconnect can never repopulate PCM. */
static void pcm_for_epoch(const uint8_t *data,uint32_t bytes,uint32_t epoch)
{
    unsigned channels;
    portENTER_CRITICAL(&mux);channels=state.channels;portEXIT_CRITICAL(&mux);
    if(!data||!channels)return;
    if(bytes%(channels*2U))return;
    for(size_t offset=0;offset<bytes;){
        size_t n=bytes-offset,chunk=64U*channels*2U;if(n>chunk)n=chunk;
        uint64_t pcm_now=(uint64_t)esp_timer_get_time();
        portENTER_CRITICAL(&mux);
        if(closing||!rendering||state.epoch!=epoch||!session_running_locked()||state.error!=ESP_OK){portEXIT_CRITICAL(&mux);return;}
        unsigned queued=(unsigned)queue.count;
#if !CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
        if(!offset)delay_sample_locked(pcm_now,0);
#endif
        bool ok=chime_bt_pcm_push(&queue,data+offset,n,!rendering);
        if(ok){
            /* The +/-300ppm hardware servo must not chase a moving latency
             * target while playing. Prefill/rebuffer may still raise it. */
            if(!offset&&(!state.clock_follow||!queue.primed))chime_bt_pcm_arrival(&queue,pcm_now);
            unsigned interval=elapsed_us(pcm_now,last_pcm_us);
            if(pcm_seen&&interval>state.pcm_interval_max_us)state.pcm_interval_max_us=interval;
            last_pcm_us=pcm_now;pcm_seen=true;
        }
        chime_bt_flow_record(&flow,pcm_now,ok?(uint32_t)(n/(channels*2U)):0,0,0,(uint32_t)queue.count,queue.target_frames);
        chime_bluetooth_status_t fault={0};
        if(!ok){++state.overflows;state.error=ESP_ERR_NO_MEM;pcm_metrics_locked();fault=state;invalidate_locked();}
        portEXIT_CRITICAL(&mux);
        if(!ok){stream_fault_log("pcm-overflow",&fault,queued);return;}
        offset+=n;
    }
}
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
static bool decoder_valid(uint32_t epoch)
{
    portENTER_CRITICAL(&mux);
    bool valid=!closing&&rendering&&state.epoch==epoch&&session_running_locked()&&state.error==ESP_OK;
    portEXIT_CRITICAL(&mux);return valid;
}
static unsigned decoder_pcm_capacity(uint32_t epoch)
{
    portENTER_CRITICAL(&mux);
    unsigned free_frames=0;
    if(!closing&&rendering&&state.epoch==epoch&&session_running_locked()&&state.error==ESP_OK)
        free_frames=CHIME_BT_QUEUE_FRAMES-(unsigned)queue.count;
    portEXIT_CRITICAL(&mux);return free_frames;
}
static void decoder_fail(uint32_t epoch,esp_err_t err)
{
    chime_bluetooth_status_t fault={0};bool failed=false;unsigned queued=0;
    portENTER_CRITICAL(&mux);
    if(!closing&&rendering&&state.epoch==epoch&&state.error==ESP_OK){
        if(err==ESP_ERR_NO_MEM)++state.overflows;
        state.error=err;pcm_metrics_locked();fault=state;queued=(unsigned)queue.count;failed=true;invalidate_locked();
    }
    portEXIT_CRITICAL(&mux);
    if(failed)stream_fault_log("decoder/input",&fault,queued);
}
static void decoder_timing(uint32_t epoch,uint64_t submitted_us)
{
    uint64_t now=(uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    if(epoch==state.epoch&&now>=submitted_us&&now-submitted_us<=CHIME_BT_DELAY_MAX_US)
        delay_sample_locked(now,(unsigned)(now-submitted_us));
    portEXIT_CRITICAL(&mux);
}
static void raw_callback(esp_a2d_conn_hdl_t handle,esp_a2d_audio_buff_t *buffer)
{
    chime_bluetooth_status_t snapshot;
    portENTER_CRITICAL(&mux);
    /* Pre-admission PCM was always discarded by output_begin. Do not spend
     * CPU decoding it while the lower-priority control owner is still trying
     * to admit/start BT (or when STOP/SS owns the silent output). */
    bool accept=!closing&&rendering&&state.connected&&state.playing&&state.error==ESP_OK&&handle==connection;
    snapshot=state;if(accept){
        uint64_t now=(uint64_t)esp_timer_get_time();
        unsigned interval=elapsed_us(now,last_rx_us);
        if(rx_seen&&interval>state.rx_interval_max_us)state.rx_interval_max_us=interval;
        last_rx_us=now;rx_seen=true;++state.packets;
        chime_bt_flow_record(&flow,now,0,0,1,(uint32_t)queue.count,queue.target_frames);
    }
    portEXIT_CRITICAL(&mux);
    if(accept)chime_bt_decoder_submit(buffer,snapshot.epoch,snapshot.codec,snapshot.rate,snapshot.channels);
    else if(buffer)esp_a2d_audio_buff_free(buffer);
}
#if CONFIG_CHIME_LDAC_REPLAY_TEST
esp_err_t chime_bluetooth_replay_begin(unsigned rate,uint32_t *epoch)
{
    if(!epoch||(rate!=44100&&rate!=48000))return ESP_ERR_INVALID_ARG;
    portENTER_CRITICAL(&mux);
    if(closing||!state.available||state.connected||state.playing||state.simulated||rendering){
        portEXIT_CRITICAL(&mux);return ESP_ERR_INVALID_STATE;
    }
    state.simulated=true;state.codec=CHIME_BT_CODEC_LDAC;state.rate=rate;
    state.channels=2;state.error=ESP_OK;
    /* Same conservative admission gain as a new real BT link, not a knob to
     * overcome low microphone SNR. Never touch USB/device/DSP/host volume. */
    state.volume=-30*256;
    invalidate_locked();*epoch=replay_token=state.epoch;
    portEXIT_CRITICAL(&mux);return ESP_OK;
}
void chime_bluetooth_replay_end(uint32_t epoch,esp_err_t error)
{
    portENTER_CRITICAL(&mux);
    if(state.simulated&&replay_token==epoch){
        state.simulated=false;if(state.error==ESP_OK)state.error=error;invalidate_locked();
    }
    portEXIT_CRITICAL(&mux);
}
bool chime_bluetooth_replay_submit(void *owned,uint32_t epoch)
{
    esp_a2d_audio_buff_t *buffer=owned;unsigned rate=0;
    portENTER_CRITICAL(&mux);
    bool accept=buffer&&!closing&&rendering&&state.simulated&&state.epoch==epoch&&state.error==ESP_OK;
    if(accept){
        rate=state.rate;uint64_t now=(uint64_t)esp_timer_get_time();
        unsigned interval=elapsed_us(now,last_rx_us);
        if(rx_seen&&interval>state.rx_interval_max_us)state.rx_interval_max_us=interval;
        last_rx_us=now;rx_seen=true;++state.packets;
        chime_bt_flow_record(&flow,now,0,0,1,(uint32_t)queue.count,queue.target_frames);
    }
    portEXIT_CRITICAL(&mux);
    if(accept)chime_bt_decoder_submit(buffer,epoch,CHIME_BT_CODEC_LDAC,rate,2);
    else if(buffer)esp_a2d_audio_buff_free(buffer);
    return accept;
}
#endif
static esp_err_t register_endpoints(void)
{
    /* Native IDF 6.1 external-codec API; no reference-repository SDK patches.
     * AAC-LC only, no HE/LTP/96k advertisement beyond this renderer's limits. */
    esp_a2d_mcc_t aac={.type=ESP_A2D_MCT_M24};
    aac.cie.m24_info.obj_type=ESP_A2D_M24_CIE_OBJ_TYPE_2_AAC_LC|ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LC;
    aac.cie.m24_info.samp_freq1=ESP_A2D_M24_CIE_SF1_44K;
    aac.cie.m24_info.samp_freq2=ESP_A2D_M24_CIE_SF2_48K;
    aac.cie.m24_info.ch=ESP_A2D_M24_CIE_CH_1|ESP_A2D_M24_CIE_CH_2;
    aac.cie.m24_info.vbr=ESP_A2D_M24_CIE_VBR_SUPPORT;
    /* 320kbps, including VBR peak per A2DP codec capability. */
    aac.cie.m24_info.br1=(320000U>>16)&0x7f;
    aac.cie.m24_info.br2=(320000U>>8)&0xff;
    aac.cie.m24_info.br3=320000U&0xff;
    esp_err_t err=esp_a2d_sink_register_stream_endpoint(0,&aac);
    if(err!=ESP_OK)return err;
    esp_a2d_mcc_t sbc={.type=ESP_A2D_MCT_SBC};
    sbc.cie.sbc_info.samp_freq=ESP_A2D_SBC_CIE_SF_16K|ESP_A2D_SBC_CIE_SF_32K|ESP_A2D_SBC_CIE_SF_44K|ESP_A2D_SBC_CIE_SF_48K;
    sbc.cie.sbc_info.ch_mode=ESP_A2D_SBC_CIE_CH_MODE_MONO|ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL|ESP_A2D_SBC_CIE_CH_MODE_STEREO|ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO;
    sbc.cie.sbc_info.block_len=ESP_A2D_SBC_CIE_BLOCK_LEN_4|ESP_A2D_SBC_CIE_BLOCK_LEN_8|ESP_A2D_SBC_CIE_BLOCK_LEN_12|ESP_A2D_SBC_CIE_BLOCK_LEN_16;
    sbc.cie.sbc_info.num_subbands=ESP_A2D_SBC_CIE_NUM_SUBBANDS_4|ESP_A2D_SBC_CIE_NUM_SUBBANDS_8;
    sbc.cie.sbc_info.alloc_mthd=ESP_A2D_SBC_CIE_ALLOC_MTHD_SNR|ESP_A2D_SBC_CIE_ALLOC_MTHD_LOUDNESS;
    sbc.cie.sbc_info.min_bitpool=2;sbc.cie.sbc_info.max_bitpool=53;
    err=esp_a2d_sink_register_stream_endpoint(1,&sbc);
    if(err!=ESP_OK)return err;
#if CONFIG_CHIME_BT_VENDOR_CODECS
    for(unsigned codec=CHIME_VENDOR_APTX;codec<=LAST_VENDOR_CODEC;++codec){
        uint8_t wire[CHIME_VENDOR_WIRE_MAX];
        chime_vendor_capability(codec,wire);
        esp_a2d_mcc_t vendor={.type=ESP_A2D_MCT_NON_A2DP};
        unsigned length=wire[0]-2U;
        vendor.cie.vendor[0]=(uint8_t)length;
        memcpy(vendor.cie.vendor+1,wire+3,length);
        err=esp_a2d_sink_register_stream_endpoint((uint8_t)(codec-1),&vendor);
        if(err!=ESP_OK)return err;
    }
#endif
    return ESP_OK;
}
#else
/* SDK-owned PCM expires on callback return. This path remains selectable for
 * recovery builds by disabling CONFIG_BT_A2DP_USE_EXTERNAL_CODEC. */
static void pcm_callback(const uint8_t *data,uint32_t bytes)
{
    chime_bluetooth_status_t s;chime_bluetooth_snapshot(&s);
    portENTER_CRITICAL(&mux);++state.packets;portEXIT_CRITICAL(&mux);
    if(data&&s.channels&&bytes%(s.channels*2U)){chime_bluetooth_fail(ESP_ERR_INVALID_SIZE);return;}
    pcm_for_epoch(data,bytes,s.epoch);
}
#endif
static void a2dp_callback(esp_a2d_cb_event_t event,esp_a2d_cb_param_t *p)
{
    if(event==ESP_A2D_PROF_STATE_EVT){
        if(p->a2d_prof_stat.init_state==ESP_A2D_INIT_SUCCESS)atomic_store(&profile,1);
        else if(p->a2d_prof_stat.init_state==ESP_A2D_DEINIT_SUCCESS)atomic_store(&profile,2);
        else atomic_store(&profile,-1);
        return;
    }
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    if(event==ESP_A2D_SEP_REG_STATE_EVT){
        if(p->a2d_sep_reg_stat.reg_state!=ESP_A2D_SEP_REG_SUCCESS)atomic_store(&endpoints,-1);
        else if(p->a2d_sep_reg_stat.seid<ENDPOINT_COUNT&&atomic_load(&endpoints)>=0)
            atomic_fetch_or(&endpoints,1<<p->a2d_sep_reg_stat.seid);
        return;
    }
#endif
    portENTER_CRITICAL(&mux);
    if(closing){portEXIT_CRITICAL(&mux);return;}
    /* A real link/config transition preempts the test session, never inherits
     * its PCM/epoch. The feeder will observe cancellation and discard its tail. */
    if(state.simulated&&(event==ESP_A2D_CONNECTION_STATE_EVT||
        event==ESP_A2D_AUDIO_CFG_EVT||event==ESP_A2D_AUDIO_STATE_EVT)){
        state.simulated=false;invalidate_locked();
    }
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    /* Delayed events from a retired link may not revoke/reformat a new one. */
    if(state.connected&&((event==ESP_A2D_AUDIO_STATE_EVT&&p->audio_stat.conn_hdl!=connection)||
        (event==ESP_A2D_AUDIO_CFG_EVT&&p->audio_cfg.conn_hdl!=connection)||
        (event==ESP_A2D_CONNECTION_STATE_EVT&&p->conn_stat.state==ESP_A2D_CONNECTION_STATE_DISCONNECTED&&p->conn_stat.conn_hdl!=connection))){
        portEXIT_CRITICAL(&mux);return;
    }
#endif
    if(event==ESP_A2D_SNK_SET_DELAY_VALUE_EVT){
        if(delay_inflight&&p->a2d_set_delay_value_stat.delay_value==delay_inflight_value){
            delay_inflight=false;
            if(delay_pending_generation==delay_generation){
                state.delay_pending=false;
                if(p->a2d_set_delay_value_stat.set_state==ESP_A2D_SET_SUCCESS){
                    delay.accepted_100us=delay_inflight_value;
                    state.delay_accepted_100us=delay_inflight_value;++state.delay_accepts;state.delay_error=ESP_OK;
                }else{++state.delay_failures;state.delay_error=ESP_FAIL;delay_retry_us=(uint64_t)esp_timer_get_time()+5000000ULL;}
            }
        }
    }else if(event==ESP_A2D_SNK_PSC_CFG_EVT){
        /* IDF emits PSC during SETCONFIG, BEFORE CONNECTED/OPEN. Retain it
         * through CONNECTED; reset at the next connect attempt/disconnect. */
        state.delay_caps_known=true;state.delay_supported=(p->a2d_psc_cfg_stat.psc_mask&ESP_A2D_PSC_DELAY_RPT)!=0;
    }else if(event==ESP_A2D_CONNECTION_STATE_EVT){
        bool connected=p->conn_stat.state==ESP_A2D_CONNECTION_STATE_CONNECTED;
        bool disconnected=p->conn_stat.state==ESP_A2D_CONNECTION_STATE_DISCONNECTED;
        if(disconnected||(!state.connected&&p->conn_stat.state==ESP_A2D_CONNECTION_STATE_CONNECTING))
            state.delay_caps_known=state.delay_supported=false;
        if((connected||disconnected)&&state.connected!=connected){
            state.connected=connected;state.playing=false;state.error=ESP_OK;
            state.delay_requested_100us=state.delay_accepted_100us=0;
            state.delay_requests=state.delay_accepts=state.delay_failures=0;state.delay_error=ESP_OK;
            if(disconnected){state.rate=state.channels=0;state.codec=CHIME_BT_CODEC_NONE;}
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
            if(connected)connection=p->conn_stat.conn_hdl;
#endif
            /* New devices never inherit another phone's high volume. */
            if(connected)state.volume=-30*256;
            state.volume_synced=false;volume_dirty=false;
            if(connected){
                memcpy(audio_peer,p->conn_stat.remote_bda,sizeof(audio_peer));
                if(avrc_connected&&remote_volume_valid&&!memcmp(audio_peer,avrc_peer,sizeof(audio_peer))){
                    state.volume=volume_db(absolute_volume);state.volume_synced=true;
                }
            }else {volume_notification=false;remote_volume_valid=false;}
            invalidate_locked();
        }
    }else if(event==ESP_A2D_AUDIO_STATE_EVT){
        bool playing=p->audio_stat.state==ESP_A2D_AUDIO_STATE_STARTED;
        if(state.playing!=playing){state.playing=playing;state.error=ESP_OK;invalidate_locked();}
    }else if(event==ESP_A2D_AUDIO_CFG_EVT){
        const esp_a2d_mcc_t *c=&p->audio_cfg.mcc;
        unsigned sf=c->cie.sbc_info.samp_freq,mode=c->cie.sbc_info.ch_mode;
        unsigned rate=sf==ESP_A2D_SBC_CIE_SF_48K?48000U:sf==ESP_A2D_SBC_CIE_SF_44K?44100U:
            sf==ESP_A2D_SBC_CIE_SF_32K?32000U:sf==ESP_A2D_SBC_CIE_SF_16K?16000U:0U;
        unsigned channels=mode==ESP_A2D_SBC_CIE_CH_MODE_MONO?1U:
            (mode==ESP_A2D_SBC_CIE_CH_MODE_DUAL_CHANNEL||mode==ESP_A2D_SBC_CIE_CH_MODE_STEREO||mode==ESP_A2D_SBC_CIE_CH_MODE_JOINT_STEREO)?2U:0U;
        chime_bt_codec_t codec=c->type==ESP_A2D_MCT_SBC?CHIME_BT_CODEC_SBC:CHIME_BT_CODEC_NONE;
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
        if(c->type==ESP_A2D_MCT_M24){
            const esp_a2d_cie_m24_t *a=&c->cie.m24_info;
            bool lc=a->obj_type==ESP_A2D_M24_CIE_OBJ_TYPE_2_AAC_LC||a->obj_type==ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LC;
            rate=a->samp_freq1==ESP_A2D_M24_CIE_SF1_44K&&!a->samp_freq2?44100U:
                !a->samp_freq1&&a->samp_freq2==ESP_A2D_M24_CIE_SF2_48K?48000U:0U;
            channels=a->ch==ESP_A2D_M24_CIE_CH_1?1U:a->ch==ESP_A2D_M24_CIE_CH_2?2U:0U;
            codec=lc?CHIME_BT_CODEC_AAC:CHIME_BT_CODEC_NONE;
        }
#endif
#if CONFIG_CHIME_BT_VENDOR_CODECS
        if(c->type==ESP_A2D_MCT_NON_A2DP){
            uint8_t wire[CHIME_VENDOR_WIRE_MAX]={0};unsigned length=c->cie.vendor[0];
            codec=CHIME_BT_CODEC_NONE;rate=channels=0;
            if(length==7||length==8||length==11){
                wire[0]=(uint8_t)(length+2);wire[2]=0xff;memcpy(wire+3,c->cie.vendor+1,length);
                if(chime_vendor_format(wire,&rate,&channels))codec=(chime_bt_codec_t)chime_vendor_codec(wire);
            }
        }
#endif
        state.codec=codec;state.rate=rate;state.channels=channels;
        state.error=codec!=CHIME_BT_CODEC_NONE&&rate&&channels?ESP_OK:ESP_ERR_NOT_SUPPORTED;
        invalidate_locked();
    }
    portEXIT_CRITICAL(&mux);
    if(event==ESP_A2D_AUDIO_CFG_EVT){
        chime_bluetooth_status_t s;chime_bluetooth_snapshot(&s);
        ESP_LOGI(TAG,"Negotiated codec=%s rate=%u channels=%u error=%d",chime_bluetooth_codec_name(s.codec),s.rate,s.channels,s.error);
    }
    if(event==ESP_A2D_CONNECTION_STATE_EVT){
        bool connected=chime_bluetooth_connected();
        esp_bt_gap_set_scan_mode(connected?ESP_BT_NON_CONNECTABLE:ESP_BT_CONNECTABLE,
            connected?ESP_BT_NON_DISCOVERABLE:ESP_BT_GENERAL_DISCOVERABLE);
    }
}
static void gap_callback(esp_bt_gap_cb_event_t event,esp_bt_gap_cb_param_t *p)
{
    /* No display/no input: SSP Just Works. Reject legacy PIN pairing instead
     * of installing a shared, undocumented fixed PIN. Bonds live in BT NVS. */
    if(event==ESP_BT_GAP_CFM_REQ_EVT)esp_bt_gap_ssp_confirm_reply(p->cfm_req.bda,true);
    if(event==ESP_BT_GAP_PIN_REQ_EVT){esp_bt_pin_code_t pin={0};esp_bt_gap_pin_reply(p->pin_req.bda,false,0,pin);}
}
static int volume_db(unsigned v)
{
    return v?-(int)((127U-v)*60U*256U/126U):-96*256;
}
esp_err_t chime_bluetooth_adjust_volume(int steps)
{
    if(steps>127)steps=127;
    if(steps< -127)steps=-127;
    portENTER_CRITICAL(&mux);
    /* Never guess a phone volume from our startup fallback or a prior phone. */
    if(!state.connected||!state.volume_synced){portEXIT_CRITICAL(&mux);return ESP_ERR_INVALID_STATE;}
    int next=(int)absolute_volume+steps;
    if(next<0)next=0;
    if(next>127)next=127;
    if((unsigned)next==absolute_volume){portEXIT_CRITICAL(&mux);return ESP_OK;}
    absolute_volume=(unsigned)next;state.volume=volume_db(absolute_volume);
    volume_dirty=true;
    portEXIT_CRITICAL(&mux);
    return ESP_OK;
}
void chime_bluetooth_volume_notify(void)
{
    portENTER_CRITICAL(&mux);
    if(!state.connected||!state.volume_synced||!volume_notification||!volume_dirty){portEXIT_CRITICAL(&mux);return;}
    unsigned v=absolute_volume;uint32_t generation=state.epoch;
    volume_notification=false;volume_dirty=false; /* CHANGED consumes subscription. */
    portEXIT_CRITICAL(&mux);
    esp_avrc_rn_param_t value={.volume=(uint8_t)v};
    esp_err_t err=esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE,ESP_AVRC_RN_RSP_CHANGED,&value);
    if(err!=ESP_OK){
        portENTER_CRITICAL(&mux);
        if(state.connected&&state.volume_synced&&state.epoch==generation)volume_dirty=true;
        portEXIT_CRITICAL(&mux);
        ESP_LOGW(TAG,"AVRCP local volume notification failed=%d; waiting for peer subscription",err);
    }
    /* Further movement coalesces in the SAME volume state until the peer
     * subscribes again. Never drop fast-turn deltas or replay them on reconnect. */
}
/* Control plane only. API success means queued, SET_SUCCESS means SDK
 * accepted/queued a report, neither proves a peer ACK or Android compensation. */
void chime_bluetooth_delay_poll(void)
{
    uint64_t now=(uint64_t)esp_timer_get_time();uint16_t value;
    portENTER_CRITICAL(&mux);
    if(delay_inflight){
        if(delay_pending_generation==delay_generation&&now>=delay_sent_us&&now-delay_sent_us>=3000000ULL&&!state.delay_timed_out){
            state.delay_timed_out=true;state.delay_error=ESP_ERR_TIMEOUT;++state.delay_failures;
        }
        portEXIT_CRITICAL(&mux);return;
    }
    if(closing||!state.available||!state.connected||!state.playing||!rendering||!queue.primed||gap_run||
       !state.delay_supported||now<delay_retry_us||!chime_bt_delay_candidate(&delay,now,&value)){
        portEXIT_CRITICAL(&mux);return;
    }
    uint32_t generation=delay_generation;
    delay.attempted=true;delay.attempt_us=now;delay_sent_us=now;
    delay_inflight=true;delay_inflight_value=value;delay_pending_generation=generation;
    state.delay_pending=true;state.delay_timed_out=false;state.delay_requested_100us=value;++state.delay_requests;
    portEXIT_CRITICAL(&mux);
    esp_err_t err=esp_a2d_sink_set_delay_value(value);
    if(err!=ESP_OK){
        portENTER_CRITICAL(&mux);
        if(delay_inflight&&delay_pending_generation==generation){
            delay_inflight=false;
            if(delay_generation==generation){state.delay_pending=false;state.delay_error=err;++state.delay_failures;delay_retry_us=now+5000000ULL;}
        }
        portEXIT_CRITICAL(&mux);
    }
}
static void avrc_callback(esp_avrc_tg_cb_event_t event,esp_avrc_tg_cb_param_t *p)
{
    if(event==ESP_AVRC_TG_CONNECTION_STATE_EVT){
        portENTER_CRITICAL(&mux);
        avrc_connected=p->conn_stat.connected;
        memcpy(avrc_peer,p->conn_stat.remote_bda,sizeof(avrc_peer));
        remote_volume_valid=false;volume_notification=false;volume_dirty=false;state.volume_synced=false;
        portEXIT_CRITICAL(&mux);
    }else if(event==ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT){
        unsigned v=p->set_abs_vol.volume;if(v>127)v=127;
        /* Monotonic -60..0dB mapping, 0=mute. No UAC mutation, no feedback
         * notification loop: set-volume itself is acknowledged by the SDK. */
        portENTER_CRITICAL(&mux);
        if(avrc_connected){
            absolute_volume=v;remote_volume_valid=true;volume_dirty=false;
            if(state.connected&&!memcmp(audio_peer,avrc_peer,sizeof(audio_peer))){state.volume=volume_db(v);state.volume_synced=true;}
        }
        portEXIT_CRITICAL(&mux);
    }else if(event==ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT&&p->reg_ntf.event_id==ESP_AVRC_RN_VOLUME_CHANGE){
        portENTER_CRITICAL(&mux);
        unsigned v=remote_volume_valid?absolute_volume:64U;
        volume_notification=avrc_connected;
        portEXIT_CRITICAL(&mux);
        esp_avrc_rn_param_t value={.volume=(uint8_t)v};
        esp_avrc_tg_send_rn_rsp(ESP_AVRC_RN_VOLUME_CHANGE,ESP_AVRC_RN_RSP_INTERIM,&value);
    }
}
static esp_err_t output_begin(bool follow)
{
    if(!chime_bluetooth_wanted())return ESP_ERR_INVALID_STATE;
    uint64_t now=(uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&mux);
    if(follow&&(state.codec!=CHIME_BT_CODEC_LDAC||state.rate!=48000||state.channels!=2)){
        portEXIT_CRITICAL(&mux);return ESP_ERR_INVALID_STATE;
    }
    state.clock_follow=follow;
    chime_bt_pcm_reset(&queue,state.rate,state.channels);
    if(!++output_counter)++output_counter;
    state.output_id=output_counter;
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
    chime_bt_ready_reset(&ready48,state.epoch,state.output_id);delivered_input=0;
    state.pipeline_last_compute_us=state.pipeline_max_compute_us=0;
    state.pipeline_compute_total_us=0;state.pipeline_computed_blocks=0;
#endif
    ++delay_generation;chime_bt_delay_reset(&delay);delay_retry_us=0;
    state.delay_pending=state.delay_timed_out=false;
    chime_bt_flow_reset(&flow,now);flow.output_id=state.output_id;
    rendering=true;output_started=now;starved_since=0;gap_run=0;
    state.stream_state=CHIME_BT_STREAM_PREFILL;
    state.rebuffer_events=state.rebuffer_resumes=state.rebuffer_cancels=state.rebuffer_blocks=0;
    state.rebuffer_wait_ms=state.rebuffer_last_wait_ms=0;
    state.pcm_gap_events=state.pcm_gap_blocks=state.pcm_gap_max_blocks=state.prefill_blocks=0;
    state.rx_interval_max_us=state.pcm_interval_max_us=0;
    state.gap_epoch=state.gap_at_ms=0;state.gap_queue=state.gap_needed=state.gap_rx_age_us=state.gap_pcm_age_us=0;state.gap_ppm=0;
    rx_seen=pcm_seen=false;last_rx_us=last_pcm_us=0;pcm_metrics_locked();
    portEXIT_CRITICAL(&mux);
    return ESP_OK;
}
esp_err_t chime_bluetooth_output_begin(void){return output_begin(false);}
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
esp_err_t chime_bluetooth_output_begin_clocked(bool follow){return output_begin(follow);}
#endif
void chime_bluetooth_output_end(void)
{
    portENTER_CRITICAL(&mux);pcm_metrics_locked();recovery_cancel_locked();rendering=false;state.clock_follow=false;gap_run=0;starved_since=0;chime_bt_pcm_reset(&queue,state.rate,state.channels);portEXIT_CRITICAL(&mux);
}
static esp_err_t compute_q27(int32_t *pcm,size_t frames,bool *ready,uint32_t expected_epoch,uint32_t expected_output,unsigned *input_frames)
{
    if(!pcm||!ready||frames!=128)return ESP_ERR_INVALID_ARG;
    uint64_t now=(uint64_t)esp_timer_get_time();
    chime_bt_src_job_t job;memset(pcm,0,frames*2*sizeof(*pcm));*ready=false;
    portENTER_CRITICAL(&mux);
    if(!rendering||!session_running_locked()||state.error!=ESP_OK){portEXIT_CRITICAL(&mux);return ESP_ERR_INVALID_STATE;}
    /* Validate the producer token under the SAME lock as PCM preparation.
     * A STOP/restart between work selection and here cannot consume new PCM. */
    if(expected_epoch&&(state.epoch!=expected_epoch||state.output_id!=expected_output)){
        portEXIT_CRITICAL(&mux);return ESP_ERR_INVALID_STATE;
    }
    unsigned queued=(unsigned)queue.count;
    bool was_primed=queue.primed;
    uint64_t before_consumed=queue.consumed_frames;
    bool direct=state.clock_follow;
    *ready=direct?chime_bt_pcm_direct_q27(&queue,pcm,frames,now):
        chime_bt_pcm_prepare_clocked(&queue,&job,frames,now,expected_epoch==0);
    if(input_frames)*input_frames=(unsigned)(queue.consumed_frames-before_consumed);
    chime_bt_flow_record(&flow,now,0,(uint32_t)(queue.consumed_frames-before_consumed),0,(uint32_t)queue.count,queue.target_frames);
    if(!*ready){
        if(was_primed){
            if(!gap_run){
                ++state.pcm_gap_events;state.gap_epoch=state.epoch;
                uint64_t ms=(now-output_started)/1000U;
                state.gap_at_ms=ms>UINT32_MAX?UINT32_MAX:(uint32_t)ms;
                state.gap_queue=queued;state.gap_needed=queue.needed;state.gap_ppm=queue.ppm;
                /* UINT32_MAX means no admitted arrival yet, not age zero. */
                state.gap_rx_age_us=rx_seen?elapsed_us(now,last_rx_us):UINT32_MAX;
                state.gap_pcm_age_us=pcm_seen?elapsed_us(now,last_pcm_us):UINT32_MAX;
            }
            ++state.pcm_gap_blocks;++gap_run;
            if(gap_run==1)starved_since=now;
            if(gap_run>state.pcm_gap_max_blocks)state.pcm_gap_max_blocks=gap_run;
            state.stream_state=CHIME_BT_STREAM_GAP;
        }else if(state.stream_state==CHIME_BT_STREAM_REBUFFERING)++state.rebuffer_blocks;
        else ++state.prefill_blocks;
        if(state.stream_state!=CHIME_BT_STREAM_REBUFFERING&&
           ((was_primed&&now-starved_since>=100000ULL)||(!was_primed&&now-output_started>=500000ULL))){
            /* A radio outage is not a decoder fault. Keep this admitted output
             * and epoch alive so raw/PCM callbacks can fill a fresh prebuffer.
             * No UART, codec allocation or source retry on the audio task. */
            chime_bt_pcm_rebuffer(&queue,now);
            state.stream_state=CHIME_BT_STREAM_REBUFFERING;++state.rebuffer_events;
            rebuffer_started=now;gap_run=0;starved_since=0;
            ++delay_generation;chime_bt_delay_reset(&delay);delay_retry_us=0;
            state.delay_pending=state.delay_timed_out=false;
        }
    }else {
        if(state.stream_state==CHIME_BT_STREAM_REBUFFERING){
            ++state.rebuffer_resumes;
            state.rebuffer_last_wait_ms=elapsed_ms(now,rebuffer_started);
            /* Hold the conservative target after recovery, not just after
             * entry: an outage may have lasted longer than the hold time. */
            queue.decay_at_us=now+CHIME_BT_TARGET_HOLD_US;
        }
        state.stream_state=CHIME_BT_STREAM_PLAYING;starved_since=0;gap_run=0;
    }
    portEXIT_CRITICAL(&mux);
    if(*ready&&!direct)chime_bt_src_process(&job,pcm,frames); /* Never FIR under the BT spinlock. */
    return ESP_OK;
}
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
/* Exactly one CPU1 producer. The service is bounded to one SRC block and never
 * waits on output/input. A packet invokes it between its 128-frame LDAC frames;
 * idle and PCM-capacity waits also service it, avoiding a backpressure deadlock.
 * All FIR math is outside mux. No audio-path allocation or new high-priority task. */
static void decoder_service(uint32_t epoch)
{
    uint32_t output_id;
    portENTER_CRITICAL(&mux);
    bool work=rendering&&session_running_locked()&&state.error==ESP_OK&&
        state.epoch==epoch&&pipeline_mode_locked()&&ready48.count<CHIME_BT_READY_BLOCKS;
    output_id=state.output_id;
    portEXIT_CRITICAL(&mux);
    if(!work)return;
    int32_t pcm[CHIME_BT_READY_SAMPLES];bool ready=false;unsigned consumed_input=0;
    uint64_t begin=(uint64_t)esp_timer_get_time();
    if(compute_q27(pcm,128,&ready,epoch,output_id,&consumed_input)!=ESP_OK||!ready)return;
    unsigned compute_us=(unsigned)((uint64_t)esp_timer_get_time()-begin);
    portENTER_CRITICAL(&mux);
    if(rendering&&session_running_locked()&&state.error==ESP_OK&&state.epoch==epoch&&
       state.output_id==output_id&&pipeline_mode_locked())
    {
        unsigned tail=(ready48.head+ready48.count)%CHIME_BT_READY_BLOCKS;
        if(chime_bt_ready_push(&ready48,epoch,output_id,pcm)){
            ready_input[tail]=consumed_input;
            state.pipeline_last_compute_us=compute_us;
            if(compute_us>state.pipeline_max_compute_us)state.pipeline_max_compute_us=compute_us;
            state.pipeline_compute_total_us+=compute_us;++state.pipeline_computed_blocks;
        }
    }
    portEXIT_CRITICAL(&mux);
}
#endif
esp_err_t chime_bluetooth_render_q27(int32_t *pcm,size_t frames,bool *ready)
{
    if(!pcm||!ready||frames!=128)return ESP_ERR_INVALID_ARG;
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
    portENTER_CRITICAL(&mux);
    if(pipeline_mode_locked()){
        memset(pcm,0,CHIME_BT_READY_SAMPLES*sizeof(*pcm));*ready=false;
        esp_err_t err=ESP_ERR_INVALID_STATE;
        if(rendering&&session_running_locked()&&state.error==ESP_OK){
            if(queue.primed){
                uint64_t ahead=queue.consumed_frames-delivered_input;
                chime_bt_pcm_control(&queue,(unsigned)(queue.count+ahead));
            }
            unsigned head=ready48.head;
            *ready=chime_bt_ready_pop(&ready48,state.epoch,state.output_id,pcm);
            if(*ready)delivered_input+=ready_input[head];
            err=ESP_OK;
        }
        portEXIT_CRITICAL(&mux);return err;
    }
    portEXIT_CRITICAL(&mux);
#endif
    return compute_q27(pcm,frames,ready,0,0,NULL);
}
/* Compatibility API for host diagnostics; production never requantizes to PCM16. */
esp_err_t chime_bluetooth_render(uint8_t *pcm,size_t frames,bool *ready)
{
    if(!pcm||!ready||frames!=128)return ESP_ERR_INVALID_ARG;
    int32_t out[256]={0};esp_err_t err=chime_bluetooth_render_q27(out,frames,ready);
    for(size_t i=0;i<frames*2;++i){int32_t v=out[i]/4096;if(v>32767)v=32767;if(v< -32768)v= -32768;
        uint16_t u=(uint16_t)(int16_t)v;pcm[2*i]=(uint8_t)u;pcm[2*i+1]=(uint8_t)(u>>8);}
    return err;
}
static bool wait_profile(int desired)
{
    for(unsigned i=0;i<200;++i){int value=atomic_load(&profile);if(value==desired)return true;if(value<0)return false;vTaskDelay(pdMS_TO_TICKS(5));}
    return false;
}
esp_err_t chime_bluetooth_start(void)
{
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
    chime_bt_pcm_attach_tail(&queue,pcm_tail);
#endif
    if(esp_bt_controller_get_status()!=ESP_BT_CONTROLLER_STATUS_IDLE)return ESP_ERR_INVALID_STATE;
    portENTER_CRITICAL(&mux);state=(chime_bluetooth_status_t){.volume=-30*256};closing=false;
    chime_bt_delay_reset(&delay);delay_inflight=false;++delay_generation;delay_retry_us=0;
    avrc_connected=remote_volume_valid=volume_notification=volume_dirty=false;absolute_volume=0;portEXIT_CRITICAL(&mux);
    /* Never erase NVS: it also contains Wi-Fi credentials. */
    esp_err_t err=nvs_flash_init();if(err!=ESP_OK)goto fail;
    esp_bt_controller_config_t config=BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    if((err=esp_bt_controller_init(&config))!=ESP_OK)goto fail;
    if((err=esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT))!=ESP_OK)goto fail;
    if((err=esp_bluedroid_init())!=ESP_OK)goto fail;
    if((err=esp_bluedroid_enable())!=ESP_OK)goto fail;
    if((err=esp_bt_gap_set_device_name("Chime Bluetooth"))!=ESP_OK)goto fail;
    if((err=esp_bt_gap_register_callback(gap_callback))!=ESP_OK)goto fail;
    esp_bt_io_cap_t capability=ESP_BT_IO_CAP_NONE;
    if((err=esp_bt_gap_set_security_param(ESP_BT_SP_IOCAP_MODE,&capability,sizeof(capability)))!=ESP_OK)goto fail;
    if((err=esp_avrc_tg_register_callback(avrc_callback))!=ESP_OK)goto fail;
    if((err=esp_avrc_tg_init())!=ESP_OK)goto fail;
    tg_requested=true;
    esp_avrc_rn_evt_cap_mask_t notifications={0};
    esp_avrc_rn_evt_bit_mask_operation(ESP_AVRC_BIT_MASK_OP_SET,&notifications,ESP_AVRC_RN_VOLUME_CHANGE);
    if((err=esp_avrc_tg_set_rn_evt_cap(&notifications))!=ESP_OK)goto fail;
    if((err=esp_a2d_register_callback(a2dp_callback))!=ESP_OK)goto fail;
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    const chime_bt_decoder_callbacks_t callbacks={.valid=decoder_valid,.pcm=pcm_for_epoch,
        .fail=decoder_fail,.timing=decoder_timing,.pcm_capacity=decoder_pcm_capacity,
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
        .service=decoder_service,
#endif
    };
    if((err=chime_bt_decoder_start(&callbacks))!=ESP_OK)goto fail;
    if((err=esp_a2d_sink_register_audio_data_callback(raw_callback))!=ESP_OK)goto fail;
#else
    if((err=esp_a2d_sink_register_data_callback(pcm_callback))!=ESP_OK)goto fail;
#endif
    atomic_store(&profile,0);
    if((err=esp_a2d_sink_init())!=ESP_OK)goto fail;
    sink_requested=true;
    if(!wait_profile(1)){err=ESP_ERR_TIMEOUT;goto fail;}
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    atomic_store(&endpoints,0);
    if((err=register_endpoints())!=ESP_OK)goto fail;
    for(unsigned i=0;i<200&&atomic_load(&endpoints)>=0&&atomic_load(&endpoints)!=ENDPOINT_MASK;++i)vTaskDelay(pdMS_TO_TICKS(5));
    if(atomic_load(&endpoints)!=ENDPOINT_MASK){err=ESP_ERR_NOT_SUPPORTED;goto fail;}
#endif
    if((err=esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE,ESP_BT_GENERAL_DISCOVERABLE))!=ESP_OK)goto fail;
    portENTER_CRITICAL(&mux);state.available=true;portEXIT_CRITICAL(&mux);
#if CONFIG_CHIME_BT_LHDC_V5
    ESP_LOGW(TAG,"LHDC V5 EXPERIMENTAL enabled: PCM16 stereo 44.1/48k 256..500kbps; independent decoder, not certified");
#endif
#if CONFIG_CHIME_BT_VENDOR_CODECS
    ESP_LOGW(TAG,"SBC/AAC/aptX/aptX HD/Android Opus/LDAC sink READY name=Chime Bluetooth; vendor codecs experimental, PCM16; initial BT -30dB; no power limiter");
#elif CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    ESP_LOGW(TAG,"SBC/AAC sink READY name=Chime Bluetooth; initial BT -30dB, AVRCP independent volume; no power limiter");
#else
    ESP_LOGW(TAG,"Internal SBC sink READY name=Chime Bluetooth; initial BT -30dB, AVRCP independent volume; no power limiter");
#endif
    return ESP_OK;
fail:
    chime_bluetooth_stop();chime_bluetooth_fail(err);
    ESP_LOGE(TAG,"Bluetooth unavailable: %s; USB retained",esp_err_to_name(err));return err;
}
void chime_bluetooth_stop(void)
{
    portENTER_CRITICAL(&mux);closing=true;state.available=false;state.connected=state.playing=state.simulated=false;invalidate_locked();portEXIT_CRITICAL(&mux);
    if(sink_requested){
        atomic_store(&profile,0);
        if(esp_a2d_sink_deinit()!=ESP_OK||!wait_profile(2)){
            ESP_LOGE(TAG,"A2DP teardown unconfirmed; retaining live stack, reset required");return;
        }
        sink_requested=false;
    }
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    if(!chime_bt_decoder_stop()){
        ESP_LOGE(TAG,"Decoder teardown unconfirmed; retaining resources, reset required");return;
    }
#endif
    esp_err_t err=ESP_OK;
    if(tg_requested){err=esp_avrc_tg_deinit();if(err!=ESP_OK)goto retained;tg_requested=false;}
    if(esp_bluedroid_get_status()==ESP_BLUEDROID_STATUS_ENABLED){err=esp_bluedroid_disable();if(err!=ESP_OK)goto retained;}
    if(esp_bluedroid_get_status()==ESP_BLUEDROID_STATUS_INITIALIZED){err=esp_bluedroid_deinit();if(err!=ESP_OK)goto retained;}
    if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_ENABLED){err=esp_bt_controller_disable();if(err!=ESP_OK)goto retained;}
    if(esp_bt_controller_get_status()==ESP_BT_CONTROLLER_STATUS_INITED){err=esp_bt_controller_deinit();if(err!=ESP_OK)goto retained;}
    return;
retained:
    chime_bluetooth_fail(err);
    ESP_LOGE(TAG,"Bluetooth teardown failed: %s; retaining ownership, reset required",esp_err_to_name(err));
}

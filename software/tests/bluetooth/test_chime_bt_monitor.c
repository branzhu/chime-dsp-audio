/* Compile the actual low-priority bridge monitor function, not a rewritten
 * logger. Only the snapshots/UART sink are faked. No PCM or I2C work is allowed. */
#include "sdkconfig.h"
#include "chime_bluetooth.h"
#include "chime_bt_decoder.h"
#include "chime_reference_codec.h"
#include <assert.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
static chime_bluetooth_status_t status;
static unsigned lines,decoder_reads,quality_reads;
static bool snapshot_available=true;
static char text[7][1024];
static const char *TAG="test";
static void capture(const char *tag,const char *fmt,...){
    assert(!strcmp(tag,TAG)&&lines<7);
    va_list args;va_start(args,fmt);int n=vsnprintf(text[lines],sizeof(text[lines]),fmt,args);va_end(args);
    assert(n>0&&(size_t)n<sizeof(text[lines]));++lines;
}
#define ESP_LOGI capture
void chime_bluetooth_snapshot(chime_bluetooth_status_t *out){*out=status;}
const char *chime_bluetooth_codec_name(chime_bt_codec_t codec){return codec==CHIME_BT_CODEC_LHDC_V5?"LHDC V5":"LDAC";}
bool chime_bt_decoder_snapshot(chime_bt_decoder_stats_t *out){
    ++decoder_reads;if(!snapshot_available)return false;
    *out=(chime_bt_decoder_stats_t){.epoch=39,.packets=100,.slow_packets=2,.packet_us=4000,.max_packet_us=12000,
        .decode_us=3900,.pcm_us=100,.open_us=0,.audio_us=5000,
        .queue_wait_us=7000,.max_queue_wait_us=15000,.raw_after_dequeue=1,.raw_max_after_dequeue=4};return true;
}
bool chime_reference_quality_snapshot(chime_reference_quality_t *out){
    ++quality_reads;if(!snapshot_available)return false;
    *out=(chime_reference_quality_t){.attempts=200,.rejected=1,.channel0_errors=2,.channel1_silenced=3,
        .peak_concealed=4,.nonfinite_frames=5,.clipped_frames=6,.clipped_samples=7,.event_attempt=199,
        .frame_bytes=252,.flags=0xa,.peak_f32_bits=0x3f800000,.result=-2,.channel0_result=-3,.channel1_result=-4};return true;
}
#include "bt_monitor.inc"
int main(void){
    status=(chime_bluetooth_status_t){.codec=CHIME_BT_CODEC_LHDC_V5,.epoch=40,.output_active=true,.primed=true,
        .queued_frames=1500,.drift_ppm=-123,.packets=101,.pcm_gap_events=2,.pcm_gap_blocks=3,.pcm_gap_max_blocks=2,.prefill_blocks=7,.underruns=0,.rate=48000,
        .queue_min=17,.queue_max=4096,.clamp_blocks=20,.pcm_frames=5000,.consumed_frames=3500,.rendered_frames=3456,
        .rx_interval_max_us=30000,.pcm_interval_max_us=31000,.output_id=5,.gap_epoch=40,.gap_at_ms=1200,
        .stream_state=CHIME_BT_STREAM_REBUFFERING,.rebuffer_events=3,.rebuffer_resumes=2,
        .rebuffer_wait_ms=2000,.rebuffer_last_wait_ms=300,.rebuffer_blocks=50,.discarded_frames=70,
        .target_ms=103,.target_frames=4944,.target_raises=1,.target_decays=2,
        .delay_caps_known=true,.delay_supported=true,.delay_estimate_us=107000,.delay_queue_us=94000,.delay_residence_us=2333,.delay_fixed_us=10667,
        .delay_samples=300,.delay_requested_100us=1070,.delay_accepted_100us=1060,.delay_pending=true,.delay_requests=2,.delay_accepts=1,.delay_failures=3,.delay_error=-9,
        .gap_queue=17,.gap_needed=130,.gap_ppm=800,.gap_rx_age_us=25000,.gap_pcm_age_us=26000};
    bluetooth_monitor_log();
    assert(strstr(text[0],"BT FLOW codec=LHDC V5 epoch=40 active=1 primed=1 queue=1500 drift_ppm=-123 packets=101 gap_events=2 gap_blocks=3 gap_max_blocks=2 prefill_blocks=7 underruns=0 error=0"));
    assert(strstr(text[1],"BT BUFFER epoch=40 output_id=5 rate=48000 queue_min=17 queue_max=4096 clamp_blocks=20 pcm_frames=5000 consumed_frames=3500 rendered_frames=3456 rx_interval_max_us=30000 pcm_interval_max_us=31000 target_ms=103 target_frames=4944 target_raises=1 target_decays=2"));
    assert(strstr(text[lines-2],"caps=1/1 estimate_us=107000 queue_us=94000 residence_us=2333 fixed_us=10667 samples=300 request_100us=1070 set_100us=1060 pending=1 timeout=0 requests=2 accepts=1 failures=3 error=-9"));
    assert(strstr(text[2],"BT GAP epoch=40 output_id=5 event=2 at_ms=1200 queue=17 needed=130 drift_ppm=800 rx_age_us=25000 pcm_age_us=26000"));
    unsigned expected=3;
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    assert(strstr(text[expected++],"BT DECODE epoch=39 packets=100 slow_packets=2 packet_us=4000 max_us=12000 decode_us=3900 pcm_us=100 open_us=0 audio_us=5000 queue_wait_us=7000 max_queue_wait_us=15000 raw_after_dequeue=1 raw_max_after_dequeue=4"));
    assert(decoder_reads==1);
#else
    assert(!decoder_reads);
#endif
#if CONFIG_CHIME_BT_LHDC_V5
    assert(strstr(text[expected++],"LHDC QUALITY attempts=200 rejected=1 ch0_errors=2 ch1_silenced=3 concealed=4 nonfinite=5 clipped_frames=6 clipped_samples=7 event_attempt=199 frame_bytes=252 flags=0xa peak_f32_bits=0x3f800000 result=-2 ch0=-3 ch1=-4"));
    assert(quality_reads==1);
#else
    assert(!quality_reads);
#endif
    assert(lines==expected+2);
    assert(strstr(text[lines-1],"BT RECOVERY output_id=5 state=4 events=3 resumes=2 cancels=0 wait_ms=2000 last_wait_ms=300 blocks=50 discarded_frames=70"));
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    assert(strstr(text[lines-2],"scope=raw-submit-to-first-PCM"));
#else
    assert(strstr(text[lines-2],"scope=PCM-only"));
#endif
    /* No garbage metrics on a bounded/contended snapshot failure. */
    snapshot_available=false;lines=0;bluetooth_monitor_log();assert(lines==4);
    /* Same event is not printed again. New event or admission is printed. */
    ++status.pcm_gap_events;lines=0;bluetooth_monitor_log();assert(lines==5&&strstr(text[2],"event=3"));
    ++status.output_id;lines=0;bluetooth_monitor_log();assert(lines==5&&strstr(text[2],"output_id=6"));
    snapshot_available=true;status.codec=CHIME_BT_CODEC_LDAC;lines=0;unsigned before=quality_reads;
    bluetooth_monitor_log();assert(quality_reads==before);
    puts("PASS actual low-priority BT monitor: per-output zero gaps and prefill, independent decoder epoch/WALL budget, LHDC boot-cumulative last-event fields, snapshot contention, codec/config gates (snapshots faked)");
    return 0;
}

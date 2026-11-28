#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum { CHIME_BT_CODEC_NONE, CHIME_BT_CODEC_SBC, CHIME_BT_CODEC_AAC, CHIME_BT_CODEC_APTX, CHIME_BT_CODEC_APTX_HD, CHIME_BT_CODEC_OPUS, CHIME_BT_CODEC_LDAC, CHIME_BT_CODEC_LHDC_V5 } chime_bt_codec_t;
/* Audio-session state, independent of the Bluetooth connection. */
typedef enum { CHIME_BT_STREAM_IDLE, CHIME_BT_STREAM_PREFILL, CHIME_BT_STREAM_PLAYING,
    CHIME_BT_STREAM_GAP, CHIME_BT_STREAM_REBUFFERING, CHIME_BT_STREAM_FAULT } chime_bt_stream_state_t;
const char *chime_bluetooth_codec_name(chime_bt_codec_t codec);
typedef struct {
    bool available,connected,playing,output_active,primed;
    bool pipeline_enabled;
    bool clock_follow; /* direct LDAC48k; drift_ppm remains SOFTWARE servo only */
    unsigned pipeline_ready_blocks,pipeline_high_water,pipeline_pending_input_frames;
    uint32_t pipeline_produced,pipeline_consumed,pipeline_underruns;
    unsigned pipeline_last_compute_us,pipeline_max_compute_us;
    uint64_t pipeline_compute_total_us;
    uint32_t pipeline_computed_blocks;
    bool simulated; /* explicit LDAC bench session; NEVER a real BT connection */
    chime_bt_stream_state_t stream_state;
    /* Per admission; waits do not invalidate the epoch or close decoder admission. */
    uint32_t rebuffer_events,rebuffer_resumes,rebuffer_cancels,rebuffer_blocks;
    unsigned rebuffer_wait_ms,rebuffer_last_wait_ms;
    uint64_t discarded_frames;
    bool volume_synced; /* current connection has supplied absolute volume */
    chime_bt_codec_t codec;
    unsigned rate,channels,queued_frames;
    uint32_t epoch,packets,overflows,underruns;
    /* Per output admission. Gaps count zero-filled 128-frame blocks AFTER
     * priming, up to recovery or entry to rebuffering after 100ms. Rebuffer
     * silence is counted separately; prefill is never a fatal timeout. */
    uint32_t pcm_gap_events,pcm_gap_blocks,pcm_gap_max_blocks,prefill_blocks;
    /* Per-admission cumulative diagnostics, retained when output ends/faults.
     * max intervals exclude the first arrival; zero age uses a separate seen
     * flag internally. Latest gap describes FIRST block of the latest event. */
    unsigned queue_min,queue_max,rx_interval_max_us,pcm_interval_max_us;
    uint32_t clamp_blocks;
    unsigned target_ms,target_frames;
    uint32_t target_raises,target_decays;
    uint64_t pcm_frames,consumed_frames,rendered_frames;
    uint32_t output_id,gap_epoch,gap_at_ms;
    unsigned gap_queue,gap_needed,gap_rx_age_us,gap_pcm_age_us;
    int gap_ppm;
    /* Estimated, uncalibrated receiver delay. accepted != peer ACK/video sync. */
    bool delay_caps_known,delay_supported,delay_pending,delay_timed_out;
    unsigned delay_estimate_us,delay_queue_us,delay_residence_us,delay_fixed_us;
    uint16_t delay_requested_100us,delay_accepted_100us;
    uint32_t delay_samples; /* per-output estimator, reset on invalidation */
    uint32_t delay_requests,delay_accepts,delay_failures; /* per A2DP connection */
    int delay_error;
    int error,volume,drift_ppm;
} chime_bluetooth_status_t;
esp_err_t chime_bluetooth_start(void);
void chime_bluetooth_stop(void);
bool chime_bluetooth_connected(void);
bool chime_bluetooth_wanted(void);
uint32_t chime_bluetooth_epoch(void);
void chime_bluetooth_snapshot(chime_bluetooth_status_t *s);
esp_err_t chime_bluetooth_output_begin(void);
void chime_bluetooth_output_end(void);
esp_err_t chime_bluetooth_render_q27(int32_t *pcm,size_t frames,bool *ready);
esp_err_t chime_bluetooth_render(uint8_t *pcm,size_t frames,bool *ready);
int chime_bluetooth_volume(void); /* dB*256; independent of USB */
void chime_bluetooth_fail(esp_err_t error);
esp_err_t chime_bluetooth_adjust_volume(int steps); /* AVRCP units, current phone value */
void chime_bluetooth_delay_poll(void); /* low-priority owner; no audio-path API calls */
void chime_bluetooth_volume_notify(void); /* input task; coalesced AVRCP CHANGED */

#if CONFIG_CHIME_LDAC_REPLAY_TEST
/* Test transport only. No gain increase, AVRCP/connected/playing spoofing, or
 * owner bypass. Epoch is the cancellation token; submit consumes ownership. */
esp_err_t chime_bluetooth_replay_begin(unsigned rate, uint32_t *epoch);
void chime_bluetooth_replay_end(uint32_t epoch, esp_err_t error);
bool chime_bluetooth_replay_submit(void *buffer, uint32_t epoch);
#endif

#if CONFIG_CHIME_LDAC_APLL_FOLLOW
/* Render owner only; eligibility rechecked under BT lock at admission. */
esp_err_t chime_bluetooth_output_begin_clocked(bool follow);
#endif

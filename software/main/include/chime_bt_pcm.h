#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "chime_bt_src.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
/* Bounded Bluetooth PCM16 queue + adaptive polyphase FIR SRC, independent of IDF.
 * Production output is 48k Q5.27. Queue preparation is separate from FIR compute.
 * Caller serializes access. No allocations, blocking or signed shifts. */
#define CHIME_BT_QUEUE_FRAMES 8192U
/* In INPUT time, independent of 16/32/44.1/48k. Startup covers the observed
 * ~71ms callback gap plus decode/packet/render margin, without growing RAM. */
#define CHIME_BT_TARGET_MIN_MS 48U
#define CHIME_BT_TARGET_START_MS 96U
#define CHIME_BT_TARGET_MAX_MS 112U
#define CHIME_BT_TARGET_MARGIN_MS 32U
#define CHIME_BT_TARGET_HOLD_US 60000000ULL
#define CHIME_BT_TARGET_DECAY_US 10000000ULL
typedef struct {
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
    int16_t pcm[2048][2];
    int16_t (*pcm_tail)[2]; /* 6144 scalar-only frames in RTC, attached once */
#else
    int16_t pcm[CHIME_BT_QUEUE_FRAMES][2];
#endif
    size_t head,count;
    unsigned rate,channels;
    uint32_t phase;
    int32_t filtered_error,ppm;
    bool primed;
    unsigned target_ms,target_frames;
    uint32_t target_raises,target_decays;
    bool clock_seen,arrival_seen,short_input;
    uint64_t last_arrival_us,decay_at_us;
    /* Per-reset diagnostics only. No changes to SRC phase/control decisions.
     * Watermarks observe render attempts after the initial prefill gate. */
    unsigned render_min,render_max,needed;
    uint64_t pushed_frames,consumed_frames,rendered_frames,discarded_frames;
    uint32_t clamp_blocks;
} chime_bt_pcm_t;
bool chime_bt_pcm_reset(chime_bt_pcm_t *q,unsigned rate,unsigned channels);
/* Enter a fresh prefill without resetting per-output diagnostics. Discard only
 * residual PCM, never decoder state; caller serializes with push/render. */
void chime_bt_pcm_rebuffer(chime_bt_pcm_t *q,uint64_t now_us);
bool chime_bt_pcm_push(chime_bt_pcm_t *q,const uint8_t *data,size_t bytes,bool keep_latest);
/* Caller reports successful PCM callback arrivals (not every copy chunk).
 * Monotonic microseconds, same clock as render; no allocation/RTOS calls. */
void chime_bt_pcm_arrival(chime_bt_pcm_t *q,uint64_t now_us);
bool chime_bt_pcm_render(chime_bt_pcm_t *q,uint8_t *stereo,size_t frames,uint64_t now_us);

bool chime_bt_pcm_prepare(chime_bt_pcm_t *q,chime_bt_src_job_t *job,size_t frames,uint64_t now_us);

/* Decoupled pipeline: controller at output consumption cadence using all
 * unplayed INPUT frames (PCM + in-flight + ready), not producer burst timing.
 * update_clock=false keeps the original preparation/FIR math with existing ppm. */
void chime_bt_pcm_control(chime_bt_pcm_t *q,unsigned queued_frames);
bool chime_bt_pcm_prepare_clocked(chime_bt_pcm_t *q,chime_bt_src_job_t *job,size_t frames,uint64_t now_us,bool update_clock);

#if CONFIG_CHIME_LDAC_PIPELINE_TEST
void chime_bt_pcm_attach_tail(chime_bt_pcm_t *q,int16_t tail[6144][2]);
#endif

/* LDAC48k hardware-clock path only: exact PCM16 -> Q27, one-for-one.
 * Keeps prefill, short-input and accounting; NEVER runs the software servo. */
bool chime_bt_pcm_direct_q27(chime_bt_pcm_t *q,int32_t *pcm,size_t frames,uint64_t now_us);

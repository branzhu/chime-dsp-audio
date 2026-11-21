#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Bounded Bluetooth PCM16 queue + streaming linear upsampler, independent of IDF.
 * Output is 48k PCM16. Initial quality baseline, NOT a high-quality music SRC.
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
    int16_t pcm[CHIME_BT_QUEUE_FRAMES][2];
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

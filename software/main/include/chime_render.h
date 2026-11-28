#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
typedef struct {
    bool clock_follow;
    int clock_command_ppm;
    uint32_t clock_rate_q16; /* SDM model, not measured or DMA-submission rate */
    uint32_t blocks,usb_short,bt_short,clips,peak,max_compute_us,deadline_misses;
    /* Wall times include preemption; write time includes normal DMA pacing.
     * src timings mean lane rendering: direct copy OR FIR, not always FIR. */
    uint32_t last_compute_us,last_src_us,max_src_us,last_write_us,max_write_us;
    uint32_t last_period_us,max_period_us,unpaced_yields;
    uint32_t src_blocks,src_inflight_us;uint64_t src_total_us;bool src_active;
} chime_render_stats_t;
esp_err_t chime_render_start(void);
void chime_render_stop(void);
esp_err_t chime_render_begin(bool usb_only);
void chime_render_end(void);
esp_err_t chime_render_poll(bool *finished);
void chime_render_snapshot(chime_render_stats_t *stats);

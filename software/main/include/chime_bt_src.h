#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CHIME_SRC_WINDOW 322U
#define CHIME_SRC_HISTORY 192U
typedef struct {
    int16_t pcm[CHIME_SRC_WINDOW][2];uint64_t step;uint32_t phase;unsigned rate;
} chime_bt_src_job_t;
void chime_bt_src_process(const chime_bt_src_job_t *job,int32_t *out,size_t frames);

/* Startup probe with CHIME_SRC_PIE_BENCHMARK=1; run on an explicitly pinned
 * CPU1 task (standalone or gated LDAC pipeline), never the CPU0 PCM worker. S31 PIE use migrates/pins tasks
 * to CPU1 even when the final benchmark decision is scalar. */
void chime_bt_src_accel_init(void);

/* Startup-only actual-chip test/complete-kernel medians, not playback timing.
 * Snapshot may report incomplete during boot. Values immutable after completion. */
typedef struct {
    bool complete,enabled;
    unsigned exact_cases,scalar_us,pie_us,reason;
    unsigned error_bound_q27,max_error_q27;
    bool bit_exact;
} chime_bt_src_accel_stats_t;
void chime_bt_src_accel_snapshot(chime_bt_src_accel_stats_t *out);

void chime_bt_src_accel_deinit(void); /* same CPU1 owner, after callbacks quiesce */

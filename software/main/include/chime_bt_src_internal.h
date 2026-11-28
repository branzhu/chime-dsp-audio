#pragma once
#include "chime_bt_src.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
/* S31 PIE exists ONLY on CPU1. Even probing from a CPU0 task migrates and
 * pins that task to CPU1 permanently. Production playback must remain scalar
 * on CPU0, opposite the CPU1 codec worker. Compile the candidate only for an
 * explicit CPU1 benchmark/pipeline experiment or the host mathematical model. */
#if defined(CHIME_SRC_PIE_MODEL) || (defined(ESP_PLATFORM) && defined(CONFIG_IDF_TARGET_ESP32S31) && CONFIG_IDF_TARGET_ESP32S31 && defined(CHIME_SRC_PIE_BENCHMARK) && CHIME_SRC_PIE_BENCHMARK)
#define CHIME_SRC_HAS_PIE 1
#else
#define CHIME_SRC_HAS_PIE 0
#endif
#if defined(CHIME_SRC_DOT_INTERPOLATION) && CHIME_SRC_DOT_INTERPOLATION
#define CHIME_SRC_ERROR_BOUND_Q27 26U
#else
#define CHIME_SRC_ERROR_BOUND_Q27 0U
#endif
/* Internal comparison/dispatch interfaces. Selection is fixed before the
 * renderer starts consuming PCM; there is no cross-task mutable scratch. */
void chime_bt_src_scalar(const chime_bt_src_job_t *job,int32_t *out,size_t frames);
#if CHIME_SRC_HAS_PIE
void chime_bt_src_candidate(const chime_bt_src_job_t *job,int32_t *out,size_t frames);
bool chime_bt_src_pie_enabled(void);
bool chime_bt_src_workspace_begin(void);
void chime_bt_src_workspace_end(void);
/* Each 192-element plane is 16-byte aligned. Parts: hi-L, hi-R, lo-L,
 * lo-R, exact sign-extended 40-bit sums in int64_t, not shifted/rounded. */
void chime_bt_src_dot_pie_planar(const int16_t *left,const int16_t *right,
    const int16_t halves[2][192],int64_t parts[4]);
void chime_bt_src_dot_pie(const int16_t pcm[2][192],const int16_t halves[2][192],int64_t parts[4]);
#endif

#pragma once
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>
/* One existing decoder-worker owner. Supported product formats only. */
void *chime_reference_open(int lhdc,unsigned rate,unsigned channels);
void chime_reference_close(void *handle);
size_t chime_reference_decode(void *handle,const uint8_t *data,size_t length,uint8_t *pcm,size_t capacity);

/* Upstream quality events, not a certification/bitrate/physical-output metric.
 * Boot-cumulative counters; metadata belongs to the last exceptional attempt.
 * Synthetic wrapper RTP identifiers are deliberately not exposed. */
typedef struct {
    uint32_t attempts,rejected,channel0_errors,channel1_silenced,peak_concealed;
    uint32_t nonfinite_frames,clipped_frames,clipped_samples;
    uint32_t event_attempt,frame_bytes,flags,peak_f32_bits;
    int32_t result,channel0_result,channel1_result;
} chime_reference_quality_t;
/* At most three atomic read attempts; no allocation/wait/decoder pointer access.
 * False when unavailable/not started/contended, with output unchanged. */
bool chime_reference_quality_snapshot(chime_reference_quality_t *out);

#pragma once
#include <stddef.h>
#include <stdint.h>
/* LE signed PCM16 stereo -> signed 24-valid-in-32 I2S.
 * Unity at master 0dB; applies only master attenuation [-60,0]dB.
 * No fixed attenuation or speaker/power limiter. */
int32_t chime_pcm_gain(int volume_db256);
void chime_pcm_convert(const uint8_t *pcm,int32_t *out,size_t frames,int32_t gain);

/* Windowed sample magnitudes, before DSP/amp gain. Output units are signed
 * 24-bit sample magnitudes (I2S slot /256), NOT acoustic/pin measurements. */
typedef struct {
    uint32_t input_peak[2],output_peak[2];
    uint32_t input_nonzero[2],output_nonzero[2];
} chime_pcm_levels_t;
/* Accumulate from padded PCM16 and the exact converted/ramped I2S buffer.
 * Does not modify samples; caller zero-initializes/resets the window. */
void chime_pcm_measure(chime_pcm_levels_t *levels,const uint8_t *pcm,
    const int32_t *out,size_t frames);

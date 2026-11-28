#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "bench_tone.h"
/* Default is silence. Tone is manual, bounded level/duration, bridge-owner only.
 * Callers must shut down amplifiers BEFORE tone_end/stop on every exit. */
esp_err_t bench_audio_start(void);
esp_err_t bench_audio_stop(void);
esp_err_t bench_audio_tone_begin(unsigned channels);
esp_err_t bench_audio_tone_configure(bench_tone_config_t config);
esp_err_t bench_audio_tone_pump(bool *finished);
void bench_audio_tone_end(void);

/* Single writer: USB worker only after tone owner is stopped. */
esp_err_t bench_audio_pcm_write(const int32_t *stereo,size_t frames);

/* Worker must be stopped and amps OFF before clearing stale DMA. */
esp_err_t bench_audio_pcm_clear(void);

/* Single render/bridge owner, serialized with I2S/tone lifecycle. No standard
 * tune_rate/reconfiguration or other APLL clients while this lease exists. */
bool bench_audio_clock_available(void);
esp_err_t bench_audio_clock_set_ppm(int ppm);
esp_err_t bench_audio_clock_reset(void);
uint32_t bench_audio_clock_rate_q16(void); /* atomic MODEL, not a pin measurement */

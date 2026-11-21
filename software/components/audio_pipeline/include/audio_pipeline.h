#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef struct {
    uint32_t sample_rate_hz;
    uint8_t slot_count;
    uint8_t slot_width_bits;
    bool esp32_provides_bclk;
    bool esp32_provides_fs;
    bool format_locked;
} audio_pipeline_config_t;

typedef struct {
    audio_pipeline_config_t config;
    bool initialized;
    bool running;
} audio_pipeline_t;

esp_err_t audio_pipeline_init(audio_pipeline_t *pipeline, uint32_t core_sample_rate_hz);
esp_err_t audio_pipeline_start(audio_pipeline_t *pipeline);
void audio_pipeline_stop(audio_pipeline_t *pipeline);

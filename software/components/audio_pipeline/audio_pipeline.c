#include "audio_pipeline.h"

#include "esp_log.h"

static const char *TAG = "audio";

esp_err_t audio_pipeline_init(audio_pipeline_t *pipeline, uint32_t core_sample_rate_hz)
{
    if (pipeline == NULL || core_sample_rate_hz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    pipeline->config.sample_rate_hz = core_sample_rate_hz;
    pipeline->config.slot_count = 0;
    pipeline->config.slot_width_bits = 0;
    pipeline->config.esp32_provides_bclk = true;
    pipeline->config.esp32_provides_fs = true;
    pipeline->config.format_locked = false;
    pipeline->initialized = true;
    pipeline->running = false;
    return ESP_OK;
}

esp_err_t audio_pipeline_start(audio_pipeline_t *pipeline)
{
    if (pipeline == NULL || !pipeline->initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!pipeline->config.format_locked) {
        ESP_LOGW(TAG, "TDM slot width and channel map are not locked");
        return ESP_ERR_NOT_SUPPORTED;
    }

    pipeline->running = true;
    return ESP_OK;
}

void audio_pipeline_stop(audio_pipeline_t *pipeline)
{
    if (pipeline != NULL) {
        pipeline->running = false;
    }
}

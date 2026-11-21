#pragma once
#include "codec_test_types.h"
typedef struct {uint32_t sample_rate;uint8_t channel;int frame_duration;bool self_delimited;} esp_opus_dec_cfg_t;
#define ESP_OPUS_DEC_FRAME_DURATION_20_MS 3
#define ESP_OPUS_DEC_CONFIG_DEFAULT() {8000,2,-1,false}
esp_audio_err_t esp_opus_dec_open(void *,uint32_t,void **);
esp_audio_err_t esp_opus_dec_decode(void *,esp_audio_dec_in_raw_t *,esp_audio_dec_out_frame_t *,esp_audio_dec_info_t *);
esp_audio_err_t esp_opus_dec_close(void *);

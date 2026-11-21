#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef int esp_audio_err_t;
#define ESP_AUDIO_ERR_OK 0
#define ESP_AUDIO_ERR_BUFF_NOT_ENOUGH -1
#define ESP_SBC_MODE_STD 0
typedef struct {uint8_t *buffer;uint32_t len,consumed;int frame_recover;} esp_audio_dec_in_raw_t;
typedef struct {uint8_t *buffer;uint32_t len,needed_size,decoded_size;} esp_audio_dec_out_frame_t;
typedef struct {uint32_t sample_rate;uint8_t bits_per_sample,channel;uint32_t bitrate,frame_size;} esp_audio_dec_info_t;
typedef struct {int32_t sample_rate;uint8_t channel,bits_per_sample;bool no_adts_header,aac_plus_enable;} esp_aac_dec_cfg_t;
typedef struct {int sbc_mode;uint8_t ch_num;bool enable_plc;} esp_sbc_dec_cfg_t;
#define ESP_AAC_DEC_CONFIG_DEFAULT() {48000,1,16,false,false}
#define ESP_SBC_DEC_CONFIG_DEFAULT() {0,2,true}
esp_audio_err_t esp_aac_dec_open(void *,uint32_t,void **);
esp_audio_err_t esp_sbc_dec_open(void *,uint32_t,void **);
esp_audio_err_t esp_aac_dec_decode(void *,esp_audio_dec_in_raw_t *,esp_audio_dec_out_frame_t *,esp_audio_dec_info_t *);
esp_audio_err_t esp_sbc_dec_decode(void *,esp_audio_dec_in_raw_t *,esp_audio_dec_out_frame_t *,esp_audio_dec_info_t *);
esp_audio_err_t esp_aac_dec_close(void *);
esp_audio_err_t esp_sbc_dec_close(void *);

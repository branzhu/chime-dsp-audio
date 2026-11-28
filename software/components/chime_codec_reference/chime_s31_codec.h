#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Codec-only boundary. The Bluetooth worker remains the sole lifecycle owner.
 * Input is a complete codec frame, except AAC (one complete LATM element).
 * No RTP, payload header, transport callback, renderer or hidden gain here. */
typedef enum {
    CHIME_S31_SBC, CHIME_S31_AAC, CHIME_S31_APTX, CHIME_S31_APTX_HD,
    CHIME_S31_OPUS, CHIME_S31_LDAC, CHIME_S31_LHDC_V5
} chime_s31_codec_t;
typedef struct { uint8_t *buffer; uint32_t len,consumed; } chime_codec_input_t;
typedef struct { uint8_t *buffer; uint32_t len,decoded_size; } chime_codec_output_t;
typedef struct { unsigned sample_rate,channel,bits_per_sample; } chime_codec_info_t;
enum { CHIME_CODEC_OK=0, CHIME_CODEC_ERROR=-1 };
void *chime_s31_open(chime_s31_codec_t codec,unsigned rate,unsigned channels);
void chime_s31_close(void *handle);
int chime_s31_decode(void *handle,chime_codec_input_t *in,chime_codec_output_t *out,chime_codec_info_t *info);

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Wire: LOSC, media, ff, vendorLE32, codecLE16, config; max 14 bytes. */
#define CHIME_VENDOR_WIRE_MAX 14
#define CHIME_VENDOR_APTX 3
#define CHIME_VENDOR_APTX_HD 4
#define CHIME_VENDOR_OPUS 5
#define CHIME_VENDOR_LDAC 6
#define CHIME_VENDOR_LHDC_V5 7
int chime_vendor_codec(const uint8_t *wire);
bool chime_vendor_capability(unsigned codec,uint8_t *wire);
bool chime_vendor_select(const uint8_t *caps,uint8_t *config);
bool chime_vendor_supports(const uint8_t *caps,const uint8_t *config);
bool chime_vendor_format(const uint8_t *config,unsigned *rate,unsigned *channels);

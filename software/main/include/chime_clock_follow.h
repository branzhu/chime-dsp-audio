#pragma once
#include <stdbool.h>
#include <stdint.h>
#define CHIME_CLOCK_MAX_PPM 300
#define CHIME_CLOCK_NOMINAL_Q16 (48000U * 65536U)
/* Pure, bounded controller. Called by the single render owner, never decoder.
 * Watermark is in INPUT frames, sampled before consuming the current block. */
typedef struct {
    uint64_t sampled_us;
    int32_t error_q8,ppm;
    bool seen;
} chime_clock_servo_t;
void chime_clock_servo_reset(chime_clock_servo_t *s);
int chime_clock_servo_update(chime_clock_servo_t *s,uint64_t now_us,
                            unsigned queued,unsigned target,bool valid);
/* Validated 40MHz XTAL -> VCO -> total /8192 clock plan. SDM includes all
 * three packed fields. No SDK cached frequency or DMA enqueue counts used. */
uint32_t chime_clock_sdm_for_ppm(uint32_t nominal,int ppm);
uint32_t chime_clock_rate_q16(uint32_t sdm);
/* Same 1/64 FIFO filter and proportional gains as TinyUSB FIFO_COUNT, with
 * the physical clock model as feedforward. FIFO closes crystal/USB drift. */
uint32_t chime_clock_usb_feedback(uint32_t *level_q16,unsigned bytes,
                                uint32_t rate_q16,bool high_speed);

#pragma once
#include "bench_dsp_status.h"
typedef struct {
    void *context;
    int (*read)(void *,uint16_t,uint8_t *,size_t,uint32_t);
    int (*write)(void *,uint16_t,const uint8_t *,size_t,uint32_t);
    uint64_t (*now_us)(void *);
    void (*wait_ms)(void *,unsigned);
} chime_dsp_boot_io_t;
/* Caller holds DSP ownership, amps OFF, hardware reset completed and I2S silent.
 * Bounded, fixed image only; no EEPROM. First error stops, caller resets DSP. */
int chime_dsp_boot(chime_dsp_boot_io_t io,uint16_t *failed_reg);

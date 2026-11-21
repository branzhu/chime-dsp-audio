#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

#define BENCH_DSP_STATUS_COUNT 19U
/* Fixed, read-only ADAU1451 registers. No arbitrary address/device API. */
typedef struct {uint16_t address;const char *name;} bench_dsp_status_register_t;
extern const bench_dsp_status_register_t bench_dsp_status_registers[BENCH_DSP_STATUS_COUNT];
typedef struct {uint16_t value;int error;bool valid;} bench_dsp_status_value_t;
typedef struct {
    bench_dsp_status_value_t registers[BENCH_DSP_STATUS_COUNT];
    uint32_t captured_ms;
    int result;
    bool complete;
} bench_dsp_status_t;
typedef struct {
    void *context;
    int (*read)(void *,uint16_t,uint8_t *,size_t,uint32_t);
    uint64_t (*now_us)(void *);
    bool (*cancelled)(void *);
} bench_dsp_status_io_t;
/* Owner only, amps off. <=200ms budget, <=20ms per transaction, no retries.
 * Read PLL_LOCK first; never access core/clock-dependent registers unlocked. */
void bench_dsp_status_collect(bench_dsp_status_t *,bench_dsp_status_io_t);

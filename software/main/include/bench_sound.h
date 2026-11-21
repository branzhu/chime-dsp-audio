#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#include "bench_output.h"
/* Target 0 = ADAU1451, 1 = LR TAS5825M, 2 = SUB TAS5825M.
 * I/O/power/tone callbacks run only in the bridge owner, never HTTP task. */
typedef struct {
    void *context;
    esp_err_t (*read)(void *,unsigned,uint16_t,uint8_t *,size_t);
    esp_err_t (*write)(void *,unsigned,uint16_t,const uint8_t *,size_t);
    esp_err_t (*power)(void *,bool lr,bool sub);
    void (*wait_ms)(void *,unsigned);
    uint64_t (*now_us)(void *);
    bool (*cancelled)(void *);
    esp_err_t (*tone_begin)(unsigned);
    esp_err_t (*tone_pump)(bool *);
    void (*tone_end)(void);
} bench_sound_io_t;
typedef struct {
    bench_sound_io_t io;
    unsigned outputs,amp_target; /* selected physical outputs / current diagnostic target */
    bool completed; /* True only after the complete finite PCM sequence. */
    bool active,attempted,continuous;
    uint64_t deadline,next_check;
    const char *phase; /* startup stage retained on failure for serial diagnosis */
    uint32_t hiz_clocks,play_clocks; /* 37/38/39 snapshots; zero if not reached */
    uint8_t power_state; /* actual 0x68, 0xff if not reached */
    /* Per selected TAS amp: index0=LR, index1=SUB. Raw readbacks, not PCM
     * meters; a clear automute flag does not prove analog speaker output. */
    uint8_t amp_state[2],amp_automute[2];
    uint32_t amp_clocks[2];
    int result;
    uint16_t failed_reg;
    uint32_t observed; /* raw failed value, or packed 37/38/39 clock monitor bytes */
} bench_sound_t;
void bench_sound_init(bench_sound_t *,bench_sound_io_t);
esp_err_t bench_sound_start(bench_sound_t *,unsigned outputs);
void bench_sound_poll(bench_sound_t *);
void bench_sound_stop(bench_sound_t *,int reason);
/* Session teardown: always safe off; only an active test is cancelled. */
void bench_sound_cancel(bench_sound_t *,int reason);

/* Same guards/amp config as sine, no fixed-duration deadline. Owner must cancel
 * on USB stop/error; callbacks feed USB instead of tone. */
esp_err_t bench_sound_start_stream(bench_sound_t *,unsigned outputs);
/* Built-in frozen DSP only; owner must exclude SS+. All amps stay OFF. */
esp_err_t bench_sound_route(bench_sound_t *,unsigned outputs);

/* External SS project: amp-only selection 3/4/7, NO parameter writes.
 * Requires compatible 48k I2S/ASRC registers; caller manually authorizes. */
esp_err_t bench_sound_start_external(bench_sound_t *,unsigned outputs);
esp_err_t bench_sound_check_external(bench_sound_t *,unsigned outputs);

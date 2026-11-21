#pragma once

#include <stdint.h>

#include "board_support.h"

typedef struct {
    esp_err_t result;
    uint16_t failed_register;
    uint16_t registers[8];
    uint32_t read_count;
    int fault_gpio_level;
    bool warning;
} device_diagnostic_t;

/* Post-enable evidence; initial reset values remain in dsp.registers. */
typedef struct {
    uint16_t registers[4]; /* F000..F003 after PLL lock. */
    uint32_t read_count;
    uint16_t lock_value;
    uint32_t lock_read_count; /* Successful F004 reads, including zero values. */
    bool locked; /* Lock observed within the window; not overall DSP PASS. */
} dsp_pll_diagnostic_t;

#define DSP_RAM_DIAGNOSTIC_BYTES 16U

/* Diagnostic-only DM0 words 0000..0003; never a running-project memory map. */
typedef struct {
    esp_err_t result;
    esp_err_t restore_result;
    uint16_t failed_address;
    uint16_t core_status;
    uint16_t kill_core;
    uint8_t original[DSP_RAM_DIAGNOSTIC_BYTES];
    uint8_t last_expected[DSP_RAM_DIAGNOSTIC_BYTES];
    uint8_t last_read[DSP_RAM_DIAGNOSTIC_BYTES];
    uint32_t patterns_passed;
    bool attempted;
    bool original_valid;
    bool last_read_valid;
    bool restore_attempted;
    bool restored;
} dsp_ram_diagnostic_t;

typedef struct {
    uint32_t complete;
    esp_err_t board_result;
    device_diagnostic_t dsp;
    device_diagnostic_t lr;
    device_diagnostic_t sub;
    dsp_pll_diagnostic_t dsp_pll;
    dsp_ram_diagnostic_t dsp_ram;
} hardware_diagnostic_report_t;

/* One pass only. Leaves both amplifiers powered down and the DSP in reset.
 * DSP briefly enables PLL for a 12.288 MHz MCLK and verifies lock/config.
 * After PLL PASS, checks stopped core, holds KILL_CORE and tests/restores
 * four 32-bit DM0 words. No program download or START_CORE write. DSP initial registers: F000..F004. Amp registers: 03, 67, 68, 70, 71, 72, 73, 60.
 * read_count distinguishes captured values from unread entries after an error.
 */
esp_err_t hardware_diagnostics_run(board_context_t *board, hardware_diagnostic_report_t *report);

#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "hardware_diagnostics.h"
#include "esp_timer.h"

static int handles[4];
static board_context_t board = {&handles[0], &handles[1], &handles[2], &handles[3]};
static uint16_t pll[5], shadow[3];
static uint8_t amps[2][128];
static bool reset_held, lr_power, sub_power, selfboot;
static unsigned calls, fail_at, second_fail_at, dsp_writes;
static bool corrupt_amp, nondefault_dsp, fail_selfboot, never_lock, slow_lock_read;
static unsigned lr_probes, sub_probes, lock_reads, lock_after_reads, ignore_dsp_mask;
static int corrupt_config;
static int64_t now_us;
static uint8_t dm0[DSP_RAM_DIAGNOSTIC_BYTES];
static uint16_t kill_core;
static bool active_core, ignore_kill, core_not_stopped;
static unsigned ram_reads, ram_writes, ram_read_fail_at, ram_write_fail_at, corrupt_ram_read_at;
static unsigned corrupt_ram_byte, ignore_ram_write_at, ram_initial_call, ram_pattern_call;

static void init(void)
{
    memset(pll, 0, sizeof(pll));
    memset(shadow, 0, sizeof(shadow));
    pll[0] = shadow[0] = 0x60;
    memset(amps, 0, sizeof(amps));
    for (size_t i = 0; i < 2; ++i) {
        amps[i][3] = 0x10;
        amps[i][0x67] = 0x95;
    }
    reset_held = true;
    lr_power = sub_power = false;
    selfboot = true;
    calls = fail_at = second_fail_at = dsp_writes = lr_probes = sub_probes = 0;
    lock_reads = ignore_dsp_mask = 0;
    lock_after_reads = 3;
    corrupt_config = -1;
    kill_core = 0;
    active_core = ignore_kill = core_not_stopped = false;
    ram_reads = ram_writes = ram_read_fail_at = ram_write_fail_at = corrupt_ram_read_at = 0;
    corrupt_ram_byte = ignore_ram_write_at = ram_initial_call = ram_pattern_call = 0;
    for (size_t i = 0; i < sizeof(dm0); ++i) dm0[i] = (uint8_t)(0x80U + i);
    now_us = 0;
    corrupt_amp = nondefault_dsp = fail_selfboot = never_lock = slow_lock_read = false;
}

static esp_err_t transaction(int timeout)
{
    assert(timeout == 100);
    ++calls;
    now_us += 1000;
    if (calls == fail_at) return ESP_ERR_TIMEOUT;
    if (calls == second_fail_at) return ESP_ERR_NOT_FOUND;
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level)
{
    assert(gpio == BOARD_GPIO_DSP_SELFBOOT && reset_held && level == 0);
    if (fail_selfboot) return ESP_ERR_INVALID_STATE;
    selfboot = false;
    return ESP_OK;
}

void board_support_set_dsp_reset(bool asserted)
{
    if (!asserted) assert(!selfboot && !lr_power && !sub_power);
    reset_held = asserted;
    if (asserted) kill_core = 0;
    if (asserted) {
        memset(pll, 0, sizeof(pll));
        memset(shadow, 0, sizeof(shadow));
        pll[0] = shadow[0] = 0x60;
    }
}

void board_support_set_amp_power(bool lr, bool sub)
{
    assert(!(lr && sub));
    if (lr || sub) assert(reset_held && pll[3] == 0);
    lr_power = lr;
    sub_power = sub;
}

void board_support_set_rgb(bool red, bool green, bool blue)
{
    (void)red; (void)green; (void)blue;
}

void board_support_safe_shutdown(const board_context_t *context)
{
    assert(context == &board);
    board_support_set_amp_power(false, false);
    board_support_set_dsp_reset(true);
}

bool board_support_read_gpio(gpio_num_t gpio)
{
    assert((gpio == BOARD_GPIO_LR_FAULT && lr_power) ||
           (gpio == BOARD_GPIO_SUB_FAULT && sub_power));
    return true;
}

void vTaskDelay(unsigned ticks) { assert(ticks >= 10); now_us += (int64_t)ticks * 1000; }
int64_t esp_timer_get_time(void) { return now_us; }
const char *esp_err_to_name(esp_err_t err) { (void)err; return "mock"; }

esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout)
{
    assert(bus == board.i2c_bus);
    if (address == 0x38) assert(!reset_held);
    else if (address == 0x4c) { assert(lr_power && !sub_power); ++lr_probes; }
    else { assert(address == 0x4d && sub_power && !lr_power); ++sub_probes; }
    return transaction(timeout);
}

esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t size, int timeout)
{
    esp_err_t err = transaction(timeout);
    if (device == board.adau1451) {
        assert(!reset_held && !selfboot && !lr_power && !sub_power);
        if (data[0] == 0 && data[1] == 0) {
            assert(size == 2U + DSP_RAM_DIAGNOSTIC_BYTES && pll[4] == 1 && kill_core == 1);
            ++ram_writes;
            if (ram_writes == 1) ram_pattern_call = calls;
            /* Writes may have reached RAM even if API reports failure. */
            if (ram_writes != ignore_ram_write_at) {
                const size_t delivered = (err != ESP_OK || ram_writes == ram_write_fail_at) ? 7U : sizeof(dm0);
                memcpy(dm0, data + 2, delivered);
            }
            if (ram_writes == ram_write_fail_at) return ESP_ERR_TIMEOUT;
        } else if (data[0] == 0xf4 && data[1] == 3) {
            assert(size == 4 && pll[4] == 1 && data[2] == 0 && data[3] == 1);
            if (!ignore_kill) kill_core = 1;
        } else {
            static const uint8_t addresses[] = {3, 0, 1, 2, 3};
            static const uint8_t values[] = {0, 0x60, 2, 1, 1};
            assert(!reset_held && !selfboot && !lr_power && !sub_power);
            assert(size == 4 && data[0] == 0xf0 && data[2] == 0 && dsp_writes < 5);
            assert(data[1] == addresses[dsp_writes] && data[3] == values[dsp_writes]);
            ++dsp_writes;
            /* Successful configuration writes only affect shadow until enable.
             * A failed write can still reach hardware; caller must reset anyway.
             */
            if ((ignore_dsp_mask & (1U << data[1])) == 0) {
                if (data[1] < 3) {
                    shadow[data[1]] = data[3];
                } else {
                    if (pll[3] == 0 && data[3] == 1) {
                        memcpy(pll, shadow, sizeof(shadow));
                    }
                    pll[3] = data[3];
                }
            }
        }
    } else {
        assert(size == 2 && reset_held);
        size_t i = device == board.tas5825m_lr ? 0U : 1U;
        assert(i == 0 ? lr_power && !sub_power : sub_power && !lr_power);
        assert((data[0] == 0 && data[1] == 0) || (data[0] == 0x7f && data[1] == 0) ||
               (data[0] == 3 && data[1] == 0x18));
        if (err == ESP_OK) amps[i][data[0]] = data[1];
    }
    return err;
}

esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *write_data,
                                      size_t write_size, uint8_t *data, size_t size, int timeout)
{
    esp_err_t err = transaction(timeout);
    if (err != ESP_OK) return err;
    if (device == board.adau1451) {
        assert(!reset_held && !lr_power && !sub_power && write_size == 2);
        if (write_data[0] == 0 && write_data[1] == 0) {
            assert(size == DSP_RAM_DIAGNOSTIC_BYTES && pll[4] == 1 && kill_core == 1);
            ++ram_reads;
            if (ram_reads == 1) ram_initial_call = calls;
            if (ram_reads == ram_read_fail_at) return ESP_ERR_TIMEOUT;
            memcpy(data, dm0, sizeof(dm0));
            if (ram_reads == corrupt_ram_read_at) data[corrupt_ram_byte] ^= 1;
        } else if (write_data[0] == 0xf4) {
            assert(size == 2 && pll[4] == 1);
            assert(write_data[1] == 3 || write_data[1] == 5);
            data[0] = 0;
            data[1] = write_data[1] == 3 ? (uint8_t)kill_core :
                      (uint8_t)((active_core || (kill_core && core_not_stopped)) ? 1 : 0);
        } else {
            assert(!reset_held && !lr_power && !sub_power);
            assert(size == 2 && write_size == 2 && write_data[0] == 0xf0 && write_data[1] <= 4);
            const unsigned reg = write_data[1];
            if (reg == 4 && dsp_writes == 5) {
                ++lock_reads;
                if (slow_lock_read) now_us += 100000;
                if (!never_lock && lock_reads >= lock_after_reads && pll[0] == 0x60 &&
                    pll[1] == 2 && pll[2] == 1 && pll[3] == 1) pll[4] = 1;
            }
            uint16_t value = pll[reg];
            if (nondefault_dsp && reg == 0) value = 0;
            if (pll[4] == 1 && (int)reg == corrupt_config) value ^= 1;
            data[0] = (uint8_t)(value >> 8);
            data[1] = (uint8_t)value;
        }
    } else {
        assert(size == 1 && write_size == 1 && reset_held);
        size_t i = device == board.tas5825m_lr ? 0U : 1U;
        assert(i == 0 ? lr_power && !sub_power : sub_power && !lr_power);
        data[0] = amps[i][write_data[0]];
        if (corrupt_amp && i == 0 && write_data[0] == 3) data[0] = 0x13;
    }
    return ESP_OK;
}

static esp_err_t run(hardware_diagnostic_report_t *report)
{
    esp_err_t err = hardware_diagnostics_run(&board, report);
    assert(report->complete == 1 && reset_held && !lr_power && !sub_power && pll[3] == 0);
    assert(now_us < 1000000);
    assert(lr_probes == 1 && sub_probes == 1);
    return err;
}

int main(void)
{
    hardware_diagnostic_report_t report;
    init();
    assert(run(&report) == ESP_OK);
    assert(report.dsp.read_count == 5 && report.lr.read_count == 8 && report.sub.read_count == 8);
    assert(report.dsp.registers[0] == 0x60 && report.dsp.registers[1] == 0);
    assert(report.lr.registers[1] == 0x95 && report.sub.registers[0] == 0x18);
    assert(dsp_writes == 5 && report.dsp_pll.locked && report.dsp_pll.lock_read_count == 3);
    assert(report.dsp_pll.read_count == 4 && report.dsp_pll.lock_value == 1);
    const uint16_t expected[] = {0x60, 2, 1, 1};
    assert(memcmp(report.dsp_pll.registers, expected, sizeof(expected)) == 0);
    assert(report.dsp_ram.result == ESP_OK && report.dsp_ram.patterns_passed == 2);
    assert(report.dsp_ram.original_valid && report.dsp_ram.restored && ram_writes == 3 && ram_reads == 4);
    assert(memcmp(dm0, report.dsp_ram.original, sizeof(dm0)) == 0);
    const unsigned initial_call = ram_initial_call, pattern_call = ram_pattern_call;
    const unsigned total = calls;
    puts("PASS shadow configuration latched on enable, delayed PLL lock, post-lock readback, no core/program writes");

    for (unsigned i = 1; i <= total; ++i) {
        init(); fail_at = i;
        assert(run(&report) == ESP_ERR_TIMEOUT);
        assert(report.dsp.result == ESP_ERR_TIMEOUT || report.lr.result == ESP_ERR_TIMEOUT ||
               report.sub.result == ESP_ERR_TIMEOUT || report.dsp_ram.result == ESP_ERR_TIMEOUT);
    }
    puts("PASS every I2C operation fails in turn, uncertain writes reset, remaining devices tested, final shutdown");

    init(); never_lock = true;
    assert(run(&report) == ESP_ERR_TIMEOUT && report.dsp.failed_register == 0xf004);
    assert(!report.dsp_pll.locked && report.dsp_pll.read_count == 0);
    assert(report.dsp_pll.lock_read_count > 1 && report.dsp_pll.lock_read_count <= 10);
    init(); slow_lock_read = true; lock_after_reads = 1;
    assert(run(&report) == ESP_ERR_TIMEOUT && !report.dsp_pll.locked);
    assert(report.dsp_pll.lock_value == 1 && report.dsp_pll.lock_read_count == 1);
    init(); lock_after_reads = 1;
    assert(run(&report) == ESP_OK && report.dsp_pll.lock_read_count == 1);
    for (unsigned reg = 1; reg <= 3; ++reg) {
        init(); ignore_dsp_mask = 1U << reg;
        assert(run(&report) == ESP_ERR_TIMEOUT && !report.dsp_pll.locked);
        assert(report.dsp.failed_register == 0xf004 && report.sub.result == ESP_OK);
    }
    puts("PASS immediate/delayed/missing/late lock and ignored writes; polling bounded, no false PASS");

    for (int reg = 0; reg < 4; ++reg) {
        init(); corrupt_config = reg;
        assert(run(&report) == ESP_ERR_INVALID_RESPONSE && report.dsp_pll.locked);
        assert(report.dsp.failed_register == (uint16_t)(0xf000 + reg));
        assert(report.dsp_pll.read_count == (uint32_t)(reg + 1) && report.sub.result == ESP_OK);
    }
    init(); fail_at = 7; second_fail_at = 8;
    assert(run(&report) == ESP_ERR_TIMEOUT && report.dsp.result == ESP_ERR_TIMEOUT);
    assert(report.lr.result == ESP_ERR_NOT_FOUND && dsp_writes == 1);
    init(); nondefault_dsp = true;
    assert(run(&report) == ESP_ERR_INVALID_RESPONSE && dsp_writes == 0);
    init(); fail_selfboot = true;
    assert(run(&report) == ESP_ERR_INVALID_STATE && dsp_writes == 0);
    puts("PASS post-lock mismatches, first error retained, unsafe reset values rejected, GPIO failure safe");

    init(); amps[0][0x71] = 4;
    assert(run(&report) == ESP_OK && !report.lr.warning);
    init(); amps[0][0x71] = 1; amps[1][0x70] = 2;
    assert(run(&report) == ESP_OK && report.lr.warning && report.sub.warning);
    init(); amps[0][0x67] = 0; amps[1][0x68] = 3;
    assert(run(&report) == ESP_ERR_INVALID_RESPONSE);
    assert(report.lr.result == ESP_ERR_INVALID_RESPONSE && report.sub.result == ESP_ERR_INVALID_RESPONSE);
    puts("PASS amp identity/deep sleep/fault checks preserved, amps wake only after DSP reset");

    init(); never_lock = true;
    assert(run(&report) == ESP_ERR_TIMEOUT && !report.dsp_ram.attempted && ram_writes == 0);
    init(); active_core = true;
    assert(run(&report) == ESP_ERR_INVALID_STATE && report.dsp_ram.failed_address == 0xf405);
    assert(ram_reads == 0 && ram_writes == 0);
    init(); ignore_kill = true;
    assert(run(&report) == ESP_ERR_INVALID_RESPONSE && report.dsp_ram.failed_address == 0xf403);
    assert(ram_reads == 0 && ram_writes == 0);
    init(); core_not_stopped = true;
    assert(run(&report) == ESP_ERR_INVALID_STATE && ram_writes == 0);
    init(); ram_read_fail_at = 1;
    assert(run(&report) == ESP_ERR_TIMEOUT && ram_writes == 0 && !report.dsp_ram.original_valid);
    assert(!report.dsp_ram.restore_attempted);
    puts("PASS RAM guarded by PLL, stopped core and KILL readback; no write without captured original");

    for (unsigned pass = 2; pass <= 3; ++pass) {
        for (unsigned byte = 0; byte < DSP_RAM_DIAGNOSTIC_BYTES; ++byte) {
            init(); corrupt_ram_read_at = pass; corrupt_ram_byte = byte;
            assert(run(&report) == ESP_ERR_INVALID_RESPONSE && report.dsp_ram.restored);
            assert(report.dsp_ram.failed_address == byte / 4U && report.dsp_ram.last_read_valid);
            assert(report.dsp_ram.patterns_passed == pass - 2U);
            assert(memcmp(dm0, report.dsp_ram.original, sizeof(dm0)) == 0);
        }
    }
    init(); ignore_ram_write_at = 1;
    assert(run(&report) == ESP_ERR_INVALID_RESPONSE && report.dsp_ram.restored);
    init(); ram_write_fail_at = 1;
    assert(run(&report) == ESP_ERR_TIMEOUT && report.dsp_ram.restored && !report.dsp_ram.last_read_valid);
    init(); ram_read_fail_at = 2;
    assert(run(&report) == ESP_ERR_TIMEOUT && report.dsp_ram.restored && !report.dsp_ram.last_read_valid);
    init(); ignore_ram_write_at = 3;
    assert(run(&report) == ESP_ERR_INVALID_RESPONSE && !report.dsp_ram.restored);
    init(); corrupt_ram_read_at = 2; corrupt_ram_byte = 9; ram_write_fail_at = 2;
    assert(run(&report) == ESP_ERR_INVALID_RESPONSE && report.dsp_ram.failed_address == 2);
    assert(report.dsp_ram.restore_result == ESP_ERR_TIMEOUT && report.dsp_ram.last_read_valid);
    assert(report.dsp_ram.last_expected[9] != report.dsp_ram.last_read[9]);
    init(); ram_write_fail_at = 3;
    assert(run(&report) == ESP_ERR_TIMEOUT && !report.dsp_ram.restored);
    init(); corrupt_ram_read_at = 4; corrupt_ram_byte = 15;
    assert(run(&report) == ESP_ERR_INVALID_RESPONSE && report.dsp_ram.failed_address == 3);
    assert(!report.dsp_ram.restored && report.dsp_ram.patterns_passed == 2);
    init(); fail_at = pattern_call; second_fail_at = pattern_call + 1U;
    assert(run(&report) == ESP_ERR_TIMEOUT && report.dsp_ram.result == ESP_ERR_TIMEOUT);
    assert(report.dsp_ram.restore_result == ESP_ERR_NOT_FOUND && !report.dsp_ram.restored);
    init(); fail_at = initial_call;
    assert(run(&report) == ESP_ERR_TIMEOUT && !report.dsp_ram.original_valid && ram_writes == 0);
    puts("PASS RAM byte order/word offsets, both patterns, uncertain writes, restore errors and first-error retention");
    return 0;
}

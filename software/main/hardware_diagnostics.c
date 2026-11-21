#include "hardware_diagnostics.h"

#include <string.h>

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define DIAGNOSTIC_I2C_TIMEOUT_MS 100
#define DSP_PLL_LOCK_TIMEOUT_US 100000
#define DSP_PLL_POLL_INTERVAL_MS 10

static const char *TAG = "diagnostic";

static esp_err_t read_register(i2c_master_dev_handle_t device, const char *name,
                               uint16_t address, size_t width, uint16_t *value)
{
    const uint8_t subaddress[] = {(uint8_t)(address >> 8), (uint8_t)address};
    uint8_t data[2] = {0};
    esp_err_t err = i2c_master_transmit_receive(device, subaddress + (2 - width), width,
                                               data, width, DIAGNOSTIC_I2C_TIMEOUT_MS);
    if (err == ESP_OK) {
        *value = width == 2 ? (uint16_t)(((uint16_t)data[0] << 8) | data[1]) : data[0];
        ESP_LOGI(TAG, "%s reg=0x%04x value=0x%04x", name, address, *value);
    } else {
        ESP_LOGE(TAG, "%s read reg=0x%04x: %s", name, address, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t write_register(i2c_master_dev_handle_t device, const char *name,
                                uint16_t address, size_t width, uint16_t value)
{
    const uint8_t data[] = {(uint8_t)(address >> 8), (uint8_t)address,
                            (uint8_t)(value >> 8), (uint8_t)value};
    const uint8_t byte_data[] = {(uint8_t)address, (uint8_t)value};
    esp_err_t err = i2c_master_transmit(device, width == 2 ? data : byte_data,
                                       width * 2, DIAGNOSTIC_I2C_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "%s write reg=0x%04x value=0x%04x: %s",
                 name, address, value, esp_err_to_name(err));
    }
    return err;
}

static esp_err_t probe(board_context_t *board, const char *name, uint16_t address)
{
    esp_err_t err = i2c_master_probe(board->i2c_bus, address, DIAGNOSTIC_I2C_TIMEOUT_MS);
    ESP_LOGI(TAG, "%s address=0x%02x ACK=%s", name, address, esp_err_to_name(err));
    return err;
}

static esp_err_t diagnose_dsp(board_context_t *board, device_diagnostic_t *result,
                               dsp_pll_diagnostic_t *pll)
{
    /* ADI Rev. D: only F000..F004 are accessible before PLL lock. Disable
     * self-boot under reset so an unknown EEPROM cannot start a program.
     */
    board_support_set_dsp_reset(true);
    esp_err_t err = gpio_set_level(BOARD_GPIO_DSP_SELFBOOT, 0);
    if (err != ESP_OK) {
        return err;
    }
    vTaskDelay(pdMS_TO_TICKS(10));
    board_support_set_dsp_reset(false);
    vTaskDelay(pdMS_TO_TICKS(20));
    err = probe(board, "DSP", BOARD_I2C_ADDR_ADAU1451);
    if (err != ESP_OK) {
        return err;
    }
    for (uint16_t i = 0; i < 5; ++i) {
        result->failed_register = (uint16_t)(0xf000U + i);
        err = read_register(board->adau1451, "DSP", result->failed_register, 2, &result->registers[i]);
        if (err != ESP_OK) {
            return err;
        }
        ++result->read_count;
    }
    const uint16_t defaults[] = {0x0060, 0, 0, 0, 0};
    for (size_t i = 0; i < 5; ++i) {
        if (result->registers[i] != defaults[i]) {
            result->failed_register = (uint16_t)(0xf000U + i);
            ESP_LOGE(TAG, "DSP unexpected reset value at 0x%04x; no PLL writes attempted",
                     result->failed_register);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }

    /* ADI Rev. D, Table 27 and Tables 69..73: board MCLK is 12.288 MHz.
     * 12.288 MHz / 4 * 96 = 294.912 MHz. Activate the shadow configuration
     * with PLL_ENABLE 0->1; do not demand pre-enable readback equality.
     * No MCLK_OUT, core-start, power-routing, or program/RAM writes here.
     * The caller hardware-resets the DSP on every exit, before waking amps.
     */
    const uint16_t setup[][2] = {
        {0xf003, 0x0000}, {0xf000, 0x0060}, {0xf001, 0x0002},
        {0xf002, 0x0001}, {0xf003, 0x0001},
    };
    ESP_LOGI(TAG, "DSP PLL INIT v2: MCLK=12288000Hz /4 *96 =294912000Hz; amps off, AUDIO=NOT_TESTED");
    for (size_t i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        result->failed_register = setup[i][0];
        err = write_register(board->adau1451, "DSP", setup[i][0], 2, setup[i][1]);
        ESP_LOGI(TAG, "DSP PLL write reg=0x%04x value=0x%04x result=%s",
                 setup[i][0], setup[i][1], esp_err_to_name(err));
        if (err != ESP_OK) {
            return err;
        }
    }

    result->failed_register = 0xf004;
    const int64_t started = esp_timer_get_time();
    while (esp_timer_get_time() - started < DSP_PLL_LOCK_TIMEOUT_US) {
        err = read_register(board->adau1451, "DSP", 0xf004, 2, &pll->lock_value);
        if (err != ESP_OK) {
            return err;
        }
        ++pll->lock_read_count;
        /* A slow in-flight I2C call can finish outside the polling window. */
        if (esp_timer_get_time() - started >= DSP_PLL_LOCK_TIMEOUT_US) {
            break;
        }
        if ((pll->lock_value & 1U) != 0) {
            pll->locked = true;
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(DSP_PLL_POLL_INTERVAL_MS));
    }
    if (!pll->locked) {
        ESP_LOGE(TAG, "DSP PLL LOCK TIMEOUT window=100ms reads=%lu last=0x%04x; check 12.288MHz MCLK/power/reset",
                 (unsigned long)pll->lock_read_count, pll->lock_value);
        return ESP_ERR_TIMEOUT;
    }
    ESP_LOGI(TAG, "DSP PLL LOCKED reg=0xf004 value=0x%04x reads=%lu",
             pll->lock_value, (unsigned long)pll->lock_read_count);

    const uint16_t expected[] = {0x0060, 0x0002, 0x0001, 0x0001};
    for (uint16_t i = 0; i < 4; ++i) {
        result->failed_register = (uint16_t)(0xf000U + i);
        err = read_register(board->adau1451, "DSP", result->failed_register, 2, &pll->registers[i]);
        if (err != ESP_OK) {
            return err;
        }
        ++pll->read_count;
        if (pll->registers[i] != expected[i]) {
            ESP_LOGE(TAG, "DSP PLL verify reg=0x%04x expected=0x%04x actual=0x%04x",
                     result->failed_register, expected[i], pll->registers[i]);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    ESP_LOGI(TAG, "DSP PLL CONFIG=PASS; no program loaded or START_CORE write; hardware reset follows");
    return ESP_OK;
}

/* Unlike 16-bit control registers, each DM0 address holds four data bytes.
 * ADI Rev. D Table 66: 0000..0003 are valid ADAU1451 DM0 word addresses.
 * This fixed diagnostic window is only used after reset, SELFBOOT=0, PLL
 * verification and a stopped-core guard. Never use it over a loaded project.
 */
static esp_err_t read_dsp_ram(board_context_t *board, uint8_t *data)
{
    const uint8_t address[] = {0, 0};
    return i2c_master_transmit_receive(board->adau1451, address, sizeof(address),
                                      data, DSP_RAM_DIAGNOSTIC_BYTES, DIAGNOSTIC_I2C_TIMEOUT_MS);
}

static esp_err_t write_dsp_ram(board_context_t *board, const uint8_t *data)
{
    uint8_t frame[2U + DSP_RAM_DIAGNOSTIC_BYTES] = {0};
    memcpy(frame + 2, data, DSP_RAM_DIAGNOSTIC_BYTES);
    return i2c_master_transmit(board->adau1451, frame, sizeof(frame), DIAGNOSTIC_I2C_TIMEOUT_MS);
}

static esp_err_t verify_dsp_ram(board_context_t *board, dsp_ram_diagnostic_t *ram)
{
    uint8_t data[DSP_RAM_DIAGNOSTIC_BYTES] = {0};
    ram->last_read_valid = false;
    esp_err_t err = read_dsp_ram(board, data);
    if (err != ESP_OK) {
        return err;
    }
    memcpy(ram->last_read, data, sizeof(data));
    ram->last_read_valid = true;
    for (size_t i = 0; i < sizeof(data); ++i) {
        if (data[i] != ram->last_expected[i]) {
            ram->failed_address = (uint16_t)(i / 4U);
            ESP_LOGE(TAG, "DSP RAM verify word=0x%04x byte=%u expected=0x%02x actual=0x%02x",
                     ram->failed_address, (unsigned)(i % 4U), ram->last_expected[i], data[i]);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    return ESP_OK;
}

static esp_err_t diagnose_dsp_ram(board_context_t *board, dsp_ram_diagnostic_t *ram)
{
    ram->attempted = true;
    ram->failed_address = 0xf405;
    ESP_LOGI(TAG, "DSP RAM INIT v3: DM0=0000..0003, 4x32bit; amps off, no program/EEPROM write");
    esp_err_t err = read_register(board->adau1451, "DSP", 0xf405, 2, &ram->core_status);
    if (err != ESP_OK) {
        return err;
    }
    /* Refuse to disturb an unexpectedly active core. KILL_CORE is for this
     * fresh-reset debug session only (ADI Tables 97/99), never live playback.
     */
    if ((ram->core_status & 7U) != 0) {
        ESP_LOGE(TAG, "DSP RAM refused: core not stopped, CORE_STATUS=0x%04x", ram->core_status);
        return ESP_ERR_INVALID_STATE;
    }
    ram->failed_address = 0xf403;
    err = write_register(board->adau1451, "DSP", 0xf403, 2, 1);
    if (err == ESP_OK) {
        err = read_register(board->adau1451, "DSP", 0xf403, 2, &ram->kill_core);
    }
    if (err != ESP_OK) {
        return err;
    }
    if ((ram->kill_core & 1U) == 0) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    ram->failed_address = 0xf405;
    err = read_register(board->adau1451, "DSP", 0xf405, 2, &ram->core_status);
    if (err != ESP_OK) {
        return err;
    }
    if ((ram->core_status & 7U) != 0) {
        return ESP_ERR_INVALID_STATE;
    }
    ram->failed_address = 0;
    err = read_dsp_ram(board, ram->original);
    if (err != ESP_OK) {
        return err; /* No captured original => no RAM write or restore. */
    }
    ram->original_valid = true;
    const uint8_t pattern[DSP_RAM_DIAGNOSTIC_BYTES] = {
        0x12, 0x34, 0x56, 0x78, 0xa5, 0xa5, 0x5a, 0x5a,
        0x55, 0xaa, 0x00, 0xff, 0x87, 0x65, 0x43, 0x21,
    };
    for (unsigned pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < sizeof(pattern); ++i) {
            ram->last_expected[i] = pass == 0 ? pattern[i] : (uint8_t)(pattern[i] ^ 0xffU);
        }
        ram->last_read_valid = false;
        err = write_dsp_ram(board, ram->last_expected);
        if (err == ESP_OK) {
            err = verify_dsp_ram(board, ram);
        }
        if (err != ESP_OK) {
            break;
        }
        ++ram->patterns_passed;
        ESP_LOGI(TAG, "DSP RAM pattern=%u/2 PASS words=4 width=32bit", pass + 1U);
    }
    /* An API failure may follow a partial write. Always attempt the captured
     * original once, preserving the first failure and its evidence/address.
     */
    const uint16_t first_failed_address = ram->failed_address;
    const bool first_read_valid = ram->last_read_valid;
    uint8_t failed_expected[DSP_RAM_DIAGNOSTIC_BYTES], failed_read[DSP_RAM_DIAGNOSTIC_BYTES];
    memcpy(failed_expected, ram->last_expected, sizeof(failed_expected));
    memcpy(failed_read, ram->last_read, sizeof(failed_read));
    ram->restore_attempted = true;
    memcpy(ram->last_expected, ram->original, sizeof(ram->last_expected));
    ram->last_read_valid = false;
    ram->restore_result = write_dsp_ram(board, ram->original);
    if (ram->restore_result == ESP_OK) {
        ram->restore_result = verify_dsp_ram(board, ram);
    }
    ram->restored = ram->restore_result == ESP_OK;
    ESP_LOGI(TAG, "DSP RAM restore=%s", esp_err_to_name(ram->restore_result));
    if (err != ESP_OK) {
        ram->failed_address = first_failed_address;
        ram->last_read_valid = first_read_valid;
        memcpy(ram->last_expected, failed_expected, sizeof(failed_expected));
        memcpy(ram->last_read, failed_read, sizeof(failed_read));
    } else {
        err = ram->restore_result;
    }
    return err;
}

static esp_err_t diagnose_amp(board_context_t *board, bool sub, device_diagnostic_t *result)
{
    const char *name = sub ? "SUB" : "LR";
    const uint16_t address = sub ? BOARD_I2C_ADDR_TAS5825M_SUB : BOARD_I2C_ADDR_TAS5825M_LR;
    const i2c_master_dev_handle_t device = sub ? board->tas5825m_sub : board->tas5825m_lr;
    /* TI SLASEH7H 9.4.5: I2C works in Deep Sleep, not PDN shutdown.
     * Wake only one device. Do not start clocks, release its DSP or enter PLAY.
     */
    board_support_set_amp_power(!sub, sub);
    vTaskDelay(pdMS_TO_TICKS(10));
    esp_err_t err = probe(board, name, address);
    if (err != ESP_OK) {
        return err;
    }
    const uint16_t addresses[] = {0x03, 0x67, 0x68, 0x70, 0x71, 0x72, 0x73, 0x60};
    /* Select page 0, book 0, and explicitly keep DSP reset + mute + deep sleep. */
    const uint16_t setup[][2] = {{0x00, 0}, {0x7f, 0}, {0x03, 0x18}};
    for (size_t i = 0; i < sizeof(setup) / sizeof(setup[0]); ++i) {
        result->failed_register = setup[i][0];
        err = write_register(device, name, setup[i][0], 1, setup[i][1]);
        if (err != ESP_OK) {
            return err;
        }
    }
    for (size_t i = 0; i < sizeof(addresses) / sizeof(addresses[0]); ++i) {
        result->failed_register = addresses[i];
        err = read_register(device, name, addresses[i], 1, &result->registers[i]);
        if (err != ESP_OK) {
            return err;
        }
        ++result->read_count;
        if ((i == 0 && result->registers[i] != 0x18) ||
            (i == 1 && result->registers[i] != 0x95) ||
            (i == 2 && result->registers[i] != 0)) {
            ESP_LOGE(TAG, "%s unexpected control/identity/power state at 0x%02x", name, addresses[i]);
            return ESP_ERR_INVALID_RESPONSE;
        }
    }
    result->fault_gpio_level = board_support_read_gpio(sub ? BOARD_GPIO_SUB_FAULT : BOARD_GPIO_LR_FAULT);
    result->warning = (result->registers[3] & 0x0fU) != 0 ||
                      (result->registers[4] & 0xe3U) != 0 ||
                      (result->registers[5] & 0x07U) != 0 || result->registers[6] != 0;
    ESP_LOGI(TAG, "%s FAULT GPIO raw=%d GPIO_CTRL=0x%02x; pin routing not validated",
             name, result->fault_gpio_level, result->registers[7]);
    if ((result->registers[4] & 0x04U) != 0) {
        ESP_LOGW(TAG, "%s CLK_FAULT observed with audio clocks intentionally stopped", name);
    }
    if (result->warning) {
        ESP_LOGW(TAG, "%s power/channel/thermal warning; preserve raw registers for investigation", name);
    }
    return ESP_OK;
}

static void log_result(const char *name, device_diagnostic_t *result)
{
    if (result->result == ESP_OK) {
        result->failed_register = 0;
    }
    ESP_LOGI(TAG, "%s COMM=%s error=%s failed_reg=0x%04x reads=%lu warning=%d",
             name, result->result == ESP_OK ? "PASS" : "FAIL", esp_err_to_name(result->result),
             result->failed_register, (unsigned long)result->read_count, result->warning);
}

esp_err_t hardware_diagnostics_run(board_context_t *board, hardware_diagnostic_report_t *report)
{
    if (board == NULL || report == NULL || board->i2c_bus == NULL || board->adau1451 == NULL ||
        board->tas5825m_lr == NULL || board->tas5825m_sub == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(report, 0, sizeof(*report));
    report->dsp.result = ESP_ERR_INVALID_STATE;
    report->lr.result = ESP_ERR_INVALID_STATE;
    report->sub.result = ESP_ERR_INVALID_STATE;
    report->dsp_ram.result = ESP_ERR_INVALID_STATE;
    report->dsp_ram.restore_result = ESP_ERR_INVALID_STATE;
    report->dsp.fault_gpio_level = -1;
    report->lr.fault_gpio_level = -1;
    report->sub.fault_gpio_level = -1;
    board_support_safe_shutdown(board);
    ESP_LOGI(TAG, "START I2C SDA=0 SCL=1 400kHz; control test only, AUDIO=NOT_TESTED");
    report->dsp.result = diagnose_dsp(board, &report->dsp, &report->dsp_pll);
    if (report->dsp.result == ESP_OK) {
        report->dsp_ram.result = diagnose_dsp_ram(board, &report->dsp_ram);
        ESP_LOGI(TAG, "DSP RAM=%s error=%s failed_addr=0x%04x patterns=%lu/2 restored=%d",
                 report->dsp_ram.result == ESP_OK ? "PASS" : "FAIL",
                 esp_err_to_name(report->dsp_ram.result), report->dsp_ram.failed_address,
                 (unsigned long)report->dsp_ram.patterns_passed, report->dsp_ram.restored);
    } else {
        ESP_LOGW(TAG, "DSP RAM=SKIPPED: PLL/control check failed");
    }
    board_support_set_dsp_reset(true);
    log_result("DSP", &report->dsp);
    report->lr.result = diagnose_amp(board, false, &report->lr);
    board_support_set_amp_power(false, false);
    log_result("LR", &report->lr);
    report->sub.result = diagnose_amp(board, true, &report->sub);
    board_support_safe_shutdown(board);
    log_result("SUB", &report->sub);
    board_support_set_rgb(false, false, true);
    report->complete = 1;
    ESP_LOGI(TAG, "COMPLETE SAFE: PDN=0/0 RESET=0; AUDIO=NOT_TESTED");
    if (report->dsp.result != ESP_OK) {
        return report->dsp.result;
    }
    if (report->dsp_ram.result != ESP_OK) {
        return report->dsp_ram.result;
    }
    return report->lr.result != ESP_OK ? report->lr.result : report->sub.result;
}

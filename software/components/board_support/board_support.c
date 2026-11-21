#include "board_support.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "board";

static esp_err_t configure_outputs(void)
{
    /* Set safe output latches before enabling the pins. */
    const struct {
        gpio_num_t pin;
        uint32_t level;
    } outputs[] = {
        {BOARD_GPIO_DSP_RESET, 0}, {BOARD_GPIO_DSP_SELFBOOT, 0},
        {BOARD_GPIO_SUB_PDN, 0}, {BOARD_GPIO_LR_PDN, 0},
        {BOARD_GPIO_LED_R, 1}, {BOARD_GPIO_LED_G, 1}, {BOARD_GPIO_LED_B, 1},
    };
    for (size_t i = 0; i < sizeof(outputs) / sizeof(outputs[0]); ++i) {
        esp_err_t err = gpio_set_level(outputs[i].pin, outputs[i].level);
        if (err != ESP_OK) {
            return err;
        }
    }
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_DSP_RESET) |
                        (1ULL << BOARD_GPIO_DSP_SELFBOOT) |
                        (1ULL << BOARD_GPIO_SUB_PDN) |
                        (1ULL << BOARD_GPIO_LR_PDN) |
                        (1ULL << BOARD_GPIO_LED_R) |
                        (1ULL << BOARD_GPIO_LED_G) |
                        (1ULL << BOARD_GPIO_LED_B),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&config);
}

static esp_err_t configure_inputs(void)
{
    const gpio_config_t config = {
        .pin_bit_mask = (1ULL << BOARD_GPIO_SUB_FAULT) |
                        (1ULL << BOARD_GPIO_LR_FAULT) |
                        (1ULL << BOARD_GPIO_VBUS_DET) |
                        (1ULL << BOARD_GPIO_ENCODER_A) |
                        (1ULL << BOARD_GPIO_ENCODER_B) |
                        (1ULL << BOARD_GPIO_ENCODER_BUTTON) |
                        (1ULL << BOARD_GPIO_BUTTON) |
                        (1ULL << BOARD_GPIO_IR),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };

    return gpio_config(&config);
}

static esp_err_t create_i2c_devices(board_context_t *board)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = BOARD_I2C_PORT,
        .sda_io_num = BOARD_I2C_SDA_GPIO,
        .scl_io_num = BOARD_I2C_SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = false,
    };
    esp_err_t err = i2c_new_master_bus(&bus_config, &board->i2c_bus);
    if (err != ESP_OK) {
        return err;
    }

    const i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .scl_speed_hz = 400000,
    };
    i2c_device_config_t adau_config = device_config;
    adau_config.device_address = BOARD_I2C_ADDR_ADAU1451;
    err = i2c_master_bus_add_device(board->i2c_bus, &adau_config, &board->adau1451);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t lr_config = device_config;
    lr_config.device_address = BOARD_I2C_ADDR_TAS5825M_LR;
    err = i2c_master_bus_add_device(board->i2c_bus, &lr_config, &board->tas5825m_lr);
    if (err != ESP_OK) {
        return err;
    }

    i2c_device_config_t sub_config = device_config;
    sub_config.device_address = BOARD_I2C_ADDR_TAS5825M_SUB;
    return i2c_master_bus_add_device(board->i2c_bus, &sub_config, &board->tas5825m_sub);
}

esp_err_t board_support_init(board_context_t *board)
{
    if (board == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    memset(board, 0, sizeof(*board));
    esp_err_t err = configure_outputs();
    if (err == ESP_OK) {
        err = configure_inputs();
    }
    board_support_safe_shutdown(board);
    if (err == ESP_OK) {
        err = gpio_set_level(BOARD_GPIO_DSP_SELFBOOT, 1);
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    err = create_i2c_devices(board);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "I2C initialization failed: %s", esp_err_to_name(err));
        board_support_safe_shutdown(board);
        return err;
    }

    ESP_LOGI(TAG, "board initialized with ESP32 I2C control bus");
    return ESP_OK;
}

void board_support_safe_shutdown(const board_context_t *board)
{
    (void)board;
    board_support_set_amp_power(false, false);
    board_support_set_dsp_reset(true);
    /* LED is independent of audio shutdown: preserve the fault indication.
     * configure_outputs() already initializes the RGB pins safely off. */
}

void board_support_set_dsp_reset(bool asserted)
{
    gpio_set_level(BOARD_GPIO_DSP_RESET, asserted ? 0 : 1);
}

void board_support_set_amp_power(bool lr_enabled, bool sub_enabled)
{
    gpio_set_level(BOARD_GPIO_LR_PDN, lr_enabled ? 1 : 0);
    gpio_set_level(BOARD_GPIO_SUB_PDN, sub_enabled ? 1 : 0);
}

void board_support_set_rgb(bool red_on, bool green_on, bool blue_on)
{
    gpio_set_level(BOARD_GPIO_LED_R, red_on ? 0 : 1);
    gpio_set_level(BOARD_GPIO_LED_G, green_on ? 0 : 1);
    gpio_set_level(BOARD_GPIO_LED_B, blue_on ? 0 : 1);
}

bool board_support_read_gpio(gpio_num_t gpio)
{
    return gpio_get_level(gpio) != 0;
}

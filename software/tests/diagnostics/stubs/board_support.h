#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef int esp_err_t;
typedef int gpio_num_t;
typedef void *i2c_master_dev_handle_t;
typedef void *i2c_master_bus_handle_t;

#define ESP_OK 0
#define ESP_ERR_INVALID_ARG 0x102
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ESP_ERR_TIMEOUT 0x107
#define ESP_ERR_INVALID_RESPONSE 0x108
#define BOARD_GPIO_DSP_SELFBOOT 17
#define BOARD_GPIO_LR_FAULT 16
#define BOARD_GPIO_SUB_FAULT 14
#define BOARD_I2C_ADDR_ADAU1451 0x38U
#define BOARD_I2C_ADDR_TAS5825M_LR 0x4CU
#define BOARD_I2C_ADDR_TAS5825M_SUB 0x4DU

typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t adau1451;
    i2c_master_dev_handle_t tas5825m_lr;
    i2c_master_dev_handle_t tas5825m_sub;
} board_context_t;

esp_err_t gpio_set_level(gpio_num_t gpio, uint32_t level);
void board_support_set_dsp_reset(bool asserted);
void board_support_set_amp_power(bool lr, bool sub);
void board_support_safe_shutdown(const board_context_t *board);
void board_support_set_rgb(bool red, bool green, bool blue);
bool board_support_read_gpio(gpio_num_t gpio);
esp_err_t i2c_master_probe(i2c_master_bus_handle_t bus, uint16_t address, int timeout);
esp_err_t i2c_master_transmit(i2c_master_dev_handle_t device, const uint8_t *data, size_t size, int timeout);
esp_err_t i2c_master_transmit_receive(i2c_master_dev_handle_t device, const uint8_t *write_data,
                                      size_t write_size, uint8_t *data, size_t size, int timeout);
const char *esp_err_to_name(esp_err_t err);

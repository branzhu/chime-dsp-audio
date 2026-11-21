#pragma once

#include <stdbool.h>

#include "driver/gpio.h"
#include "driver/i2c_master.h"
#include "esp_err.h"

#define BOARD_I2C_PORT I2C_NUM_0
#define BOARD_I2C_SDA_GPIO GPIO_NUM_0
#define BOARD_I2C_SCL_GPIO GPIO_NUM_1

#define BOARD_GPIO_DSP_RESET GPIO_NUM_2
#define BOARD_GPIO_DSP_SELFBOOT GPIO_NUM_17
#define BOARD_GPIO_SUB_PDN GPIO_NUM_13
#define BOARD_GPIO_LR_PDN GPIO_NUM_15
#define BOARD_GPIO_SUB_FAULT GPIO_NUM_14
#define BOARD_GPIO_LR_FAULT GPIO_NUM_16
#define BOARD_GPIO_VBUS_DET GPIO_NUM_8
#define BOARD_GPIO_TDM_DOUT GPIO_NUM_9
#define BOARD_GPIO_TDM_BCLK GPIO_NUM_10
#define BOARD_GPIO_TDM_FS GPIO_NUM_11
#define BOARD_GPIO_TDM_RX GPIO_NUM_12
#define BOARD_GPIO_USB_DBG_DM GPIO_NUM_33
#define BOARD_GPIO_USB_DBG_DP GPIO_NUM_34
#define BOARD_GPIO_LED_R GPIO_NUM_35
#define BOARD_GPIO_LED_B GPIO_NUM_37
#define BOARD_GPIO_IR GPIO_NUM_38
#define BOARD_GPIO_LED_G GPIO_NUM_39
#define BOARD_GPIO_ENCODER_B GPIO_NUM_40
#define BOARD_GPIO_ENCODER_A GPIO_NUM_42
#define BOARD_GPIO_ENCODER_BUTTON GPIO_NUM_43
#define BOARD_GPIO_BUTTON ((gpio_num_t)61)

#define BOARD_I2C_ADDR_ADAU1451 0x38U
#define BOARD_I2C_ADDR_TAS5825M_LR 0x4CU
#define BOARD_I2C_ADDR_TAS5825M_SUB 0x4DU
#define BOARD_I2C_ADDR_DSP_MASTER_EEPROM 0x50U

typedef struct {
    i2c_master_bus_handle_t i2c_bus;
    i2c_master_dev_handle_t adau1451;
    i2c_master_dev_handle_t tas5825m_lr;
    i2c_master_dev_handle_t tas5825m_sub;
} board_context_t;

esp_err_t board_support_init(board_context_t *board);
void board_support_safe_shutdown(const board_context_t *board);
void board_support_set_dsp_reset(bool asserted);
void board_support_set_amp_power(bool lr_enabled, bool sub_enabled);
void board_support_set_rgb(bool red_on, bool green_on, bool blue_on);
bool board_support_read_gpio(gpio_num_t gpio);

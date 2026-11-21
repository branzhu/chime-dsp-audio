#pragma once
#include "esp_err.h"
#define BOARD_GPIO_VBUS_DET 8
esp_err_t gpio_install_isr_service(int flags);

#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
typedef int gpio_num_t;
#define BOARD_GPIO_LED_R 35
#define BOARD_GPIO_LED_G 39
#define BOARD_GPIO_LED_B 37
#define GPIO_MODE_OUTPUT 1
esp_err_t gpio_set_level(gpio_num_t,unsigned);
esp_err_t gpio_set_direction(gpio_num_t,int);
void board_support_set_rgb(bool,bool,bool);

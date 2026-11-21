#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#define BOARD_GPIO_ENCODER_A 42
#define BOARD_GPIO_ENCODER_B 40
#define BOARD_GPIO_ENCODER_BUTTON 43
typedef struct {uint64_t pin_bit_mask;int mode,pull_up_en,pull_down_en,intr_type;} gpio_config_t;
enum {GPIO_MODE_INPUT=1,GPIO_PULLUP_ENABLE=1,GPIO_PULLDOWN_DISABLE=0,GPIO_INTR_DISABLE=0,GPIO_INTR_ANYEDGE=3};
int gpio_get_level(int pin);
esp_err_t gpio_config(const gpio_config_t *c);
esp_err_t gpio_install_isr_service(int flags);
esp_err_t gpio_isr_handler_add(int pin,void (*handler)(void *),void *arg);
esp_err_t gpio_isr_handler_remove(int pin);
esp_err_t gpio_set_intr_type(int pin,int type);
esp_err_t gpio_intr_enable(int pin);
esp_err_t gpio_intr_disable(int pin);

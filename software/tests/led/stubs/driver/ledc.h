#pragma once
#include "board_support.h"
typedef int ledc_channel_t;
#define LEDC_LOW_SPEED_MODE 0
#define LEDC_TIMER_8_BIT 8
#define LEDC_TIMER_0 0
#define LEDC_AUTO_CLK 0
typedef struct { int speed_mode,duty_resolution,timer_num; unsigned freq_hz; int clk_cfg; } ledc_timer_config_t;
typedef struct { int gpio_num,speed_mode;ledc_channel_t channel;int timer_sel;unsigned duty;int hpoint;
    struct {unsigned output_invert:1;} flags;bool deconfigure; } ledc_channel_config_t;
esp_err_t ledc_timer_config(const ledc_timer_config_t *);
esp_err_t ledc_channel_config(const ledc_channel_config_t *);
esp_err_t ledc_set_duty(int,ledc_channel_t,unsigned);
esp_err_t ledc_update_duty(int,ledc_channel_t);

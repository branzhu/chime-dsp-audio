#pragma once

#include <stdbool.h>

#include "board_support.h"
#include "esp_err.h"

typedef struct {
    board_context_t *board;
    bool initialized;
    bool reset_released;
} dsp_controller_t;

esp_err_t dsp_controller_init(dsp_controller_t *controller, board_context_t *board);
esp_err_t dsp_controller_begin_boot(dsp_controller_t *controller);
esp_err_t dsp_controller_wait_ready(dsp_controller_t *controller);
void dsp_controller_hold_reset(dsp_controller_t *controller);

#pragma once

#include <stdbool.h>

#include "board_support.h"
#include "esp_err.h"

typedef struct {
    bool encoder_a;
    bool encoder_b;
    bool encoder_button;
    bool button;
    bool ir;
    bool usb_vbus;
} input_snapshot_t;

typedef struct {
    board_context_t *board;
    bool initialized;
} input_controller_t;

esp_err_t input_controller_init(input_controller_t *controller, board_context_t *board);
void input_controller_read(const input_controller_t *controller, input_snapshot_t *snapshot);

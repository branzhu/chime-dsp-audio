#pragma once

#include <stdbool.h>

#include "board_support.h"
#include "esp_err.h"

typedef struct {
    board_context_t *board;
    bool initialized;
    bool configured;
    bool fault_polarity_known;
    bool fault_active_level;
} amp_controller_t;

esp_err_t amp_controller_init(amp_controller_t *controller, board_context_t *board);
esp_err_t amp_controller_configure(amp_controller_t *controller);
esp_err_t amp_controller_set_fault_active_level(amp_controller_t *controller, bool active_level);
void amp_controller_set_enabled(amp_controller_t *controller, bool lr_enabled, bool sub_enabled);
bool amp_controller_has_fault(const amp_controller_t *controller);

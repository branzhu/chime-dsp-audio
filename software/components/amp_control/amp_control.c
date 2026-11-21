#include "amp_control.h"

#include "esp_log.h"

static const char *TAG = "amp";

esp_err_t amp_controller_init(amp_controller_t *controller, board_context_t *board)
{
    if (controller == NULL || board == NULL || board->tas5825m_lr == NULL || board->tas5825m_sub == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    controller->board = board;
    controller->initialized = true;
    controller->configured = false;
    controller->fault_polarity_known = false;
    controller->fault_active_level = false;
    amp_controller_set_enabled(controller, false, false);
    return ESP_OK;
}

esp_err_t amp_controller_configure(amp_controller_t *controller)
{
    if (controller == NULL || !controller->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "TAS5825M register image is not attached; amplifiers remain disabled");
    return ESP_ERR_NOT_SUPPORTED;
}

esp_err_t amp_controller_set_fault_active_level(amp_controller_t *controller, bool active_level)
{
    if (controller == NULL || !controller->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    controller->fault_active_level = active_level;
    controller->fault_polarity_known = true;
    return ESP_OK;
}

void amp_controller_set_enabled(amp_controller_t *controller, bool lr_enabled, bool sub_enabled)
{
    if (controller == NULL || !controller->initialized ||
        ((lr_enabled || sub_enabled) && !controller->configured)) {
        return;
    }

    board_support_set_amp_power(lr_enabled, sub_enabled);
}

bool amp_controller_has_fault(const amp_controller_t *controller)
{
    if (controller == NULL || !controller->initialized) {
        return true;
    }
    if (!controller->configured || !controller->fault_polarity_known) {
        return false;
    }

    return board_support_read_gpio(BOARD_GPIO_LR_FAULT) == controller->fault_active_level ||
           board_support_read_gpio(BOARD_GPIO_SUB_FAULT) == controller->fault_active_level;
}

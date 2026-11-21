#include "dsp_control.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "dsp";

esp_err_t dsp_controller_init(dsp_controller_t *controller, board_context_t *board)
{
    if (controller == NULL || board == NULL || board->adau1451 == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    controller->board = board;
    controller->initialized = true;
    controller->reset_released = false;
    dsp_controller_hold_reset(controller);
    return ESP_OK;
}

esp_err_t dsp_controller_begin_boot(dsp_controller_t *controller)
{
    if (controller == NULL || !controller->initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    board_support_set_dsp_reset(false);
    controller->reset_released = true;
    vTaskDelay(pdMS_TO_TICKS(10));
    ESP_LOGI(TAG, "DSP reset released; waiting for self-boot readiness");
    return ESP_OK;
}

esp_err_t dsp_controller_wait_ready(dsp_controller_t *controller)
{
    if (controller == NULL || !controller->initialized || !controller->reset_released) {
        return ESP_ERR_INVALID_STATE;
    }

    ESP_LOGW(TAG, "DSP ready criteria require the final SigmaStudio image and register map");
    return ESP_ERR_NOT_SUPPORTED;
}

void dsp_controller_hold_reset(dsp_controller_t *controller)
{
    if (controller == NULL) {
        return;
    }

    board_support_set_dsp_reset(true);
    controller->reset_released = false;
}

#include "input_control.h"

#include <string.h>

esp_err_t input_controller_init(input_controller_t *controller, board_context_t *board)
{
    if (controller == NULL || board == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    controller->board = board;
    controller->initialized = true;
    return ESP_OK;
}

void input_controller_read(const input_controller_t *controller, input_snapshot_t *snapshot)
{
    if (snapshot == NULL) {
        return;
    }

    memset(snapshot, 0, sizeof(*snapshot));
    if (controller == NULL || !controller->initialized) {
        return;
    }

    snapshot->encoder_a = board_support_read_gpio(BOARD_GPIO_ENCODER_A);
    snapshot->encoder_b = board_support_read_gpio(BOARD_GPIO_ENCODER_B);
    snapshot->encoder_button = board_support_read_gpio(BOARD_GPIO_ENCODER_BUTTON);
    snapshot->button = board_support_read_gpio(BOARD_GPIO_BUTTON);
    snapshot->ir = board_support_read_gpio(BOARD_GPIO_IR);
    snapshot->usb_vbus = board_support_read_gpio(BOARD_GPIO_VBUS_DET);
}

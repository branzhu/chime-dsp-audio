#pragma once
#include "esp_err.h"
#include <stdbool.h>
/* Shared worker currently lives beside the USB transport in chime_usb.c.
 * Both sources use the SAME task, mutex and TX. No second BT I2S driver. */
esp_err_t chime_stream_bt_begin(unsigned unused);
esp_err_t chime_stream_bt_poll(bool *finished);
void chime_stream_end(void);

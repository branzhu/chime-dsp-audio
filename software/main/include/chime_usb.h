#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
esp_err_t chime_usb_start(void);
void chime_usb_stop(void);
bool chime_usb_wanted(void);
esp_err_t chime_usb_adjust_volume(int steps); /* relative 1 dB steps, current UAC value */
bool chime_usb_muted(void); /* atomic host mute, no audio side effects */
uint32_t chime_usb_epoch(void);
esp_err_t chime_usb_output_begin(unsigned unused);
esp_err_t chime_usb_output_poll(bool *finished);
void chime_usb_output_end(void);
esp_err_t chime_usb_error(void);

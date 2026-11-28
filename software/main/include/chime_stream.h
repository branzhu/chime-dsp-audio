#pragma once
#include "esp_err.h"
#include <stdbool.h>
/* Shared renderer owns the only continuous I2S writer. */
esp_err_t chime_stream_mix_begin(unsigned unused);
esp_err_t chime_stream_mix_poll(bool *finished);
void chime_stream_end(void);

#pragma once
#include <stdbool.h>
#include "esp_err.h"
esp_err_t chime_controls_start(void);
void chime_controls_stop(void);
bool chime_controls_blocked(void);
bool chime_controls_muted(void);
bool chime_controls_standby(void);

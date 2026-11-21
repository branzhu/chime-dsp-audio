#pragma once
#include "chime_led.h"
#include "esp_err.h"
/* One low-priority worker owns LEDC timer0/channels0..2. No audio-side waits. */
esp_err_t chime_indicator_start(void);
/* Single DSP owner publishes coherent base state. Separate overlay setters do
 * not modify the audio state or define new button gestures. */
typedef struct {bool fault,stopped,starting,bluetooth;} chime_indicator_state_t;
void chime_indicator_publish(chime_indicator_state_t input);
void chime_indicator_set_standby(bool active);
void chime_indicator_set_pairing(bool active);
/* Fatal service failure, latched until reset. Safe even before start. */
void chime_indicator_fail(void);

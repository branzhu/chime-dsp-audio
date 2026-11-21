#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Pure policy: no driver, audio, network, allocation or timing side effects. */
typedef struct {
    bool fault,standby,starting,setup,pairing,stopped,muted,bluetooth;
} chime_led_input_t;
typedef enum {
    CHIME_LED_FAULT,CHIME_LED_STANDBY,CHIME_LED_STARTING,CHIME_LED_SETUP,
    CHIME_LED_PAIRING,CHIME_LED_MUTED,CHIME_LED_USB,CHIME_LED_BLUETOOTH
} chime_led_mode_t;
typedef struct { uint8_t r,g,b; } chime_led_rgb_t;
typedef struct { chime_led_mode_t mode; uint64_t since_ms; bool initialized; } chime_led_t;
chime_led_mode_t chime_led_mode(chime_led_input_t input);
chime_led_rgb_t chime_led_render(chime_led_t *led,chime_led_input_t input,uint64_t now_ms);
/* Explicit setup only: first use or a submitted configuration attempt, not a
 * saved-network reconnect/fallback AP. Success ends the indication immediately. */
bool chime_led_setup_requested(bool first_use,bool submitted,bool succeeded);

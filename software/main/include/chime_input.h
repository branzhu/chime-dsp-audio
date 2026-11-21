#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Hardware-free input decoder; times are monotonic milliseconds. */
typedef struct { unsigned previous; int quarter_steps; } chime_rotary_t;
void chime_rotary_init(chime_rotary_t *s,bool a,bool b);
/* Full quadrature cycle: bounce/reversal cancels, illegal two-bit jumps reset. */
int chime_rotary_sample(chime_rotary_t *s,bool a,bool b);
typedef enum { CHIME_INPUT_NONE,CHIME_INPUT_CLICK,CHIME_INPUT_HOLD } chime_input_event_t;
typedef struct {
    bool raw,stable,armed,held;
    uint64_t changed_ms,pressed_ms;
} chime_button_t;
void chime_button_init(chime_button_t *s,bool pressed,uint64_t now_ms);
chime_input_event_t chime_button_sample(chime_button_t *s,bool pressed,uint64_t now_ms);
/* Device-local mute is independent of host mute, STOP and fault latches. */
#define CHIME_CONTROL_MUTED 1U
#define CHIME_CONTROL_STANDBY 2U
unsigned chime_input_action(unsigned flags,chime_input_event_t event);

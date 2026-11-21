#pragma once
#include <stdint.h>
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void input_test_enter(portMUX_TYPE *mux);
void input_test_exit(portMUX_TYPE *mux);
#define portENTER_CRITICAL(m) input_test_enter(m)
#define portEXIT_CRITICAL(m) input_test_exit(m)
#define portENTER_CRITICAL_ISR(m) input_test_enter(m)
#define portEXIT_CRITICAL_ISR(m) input_test_exit(m)

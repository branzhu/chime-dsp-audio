#pragma once
#include <stdint.h>
#define portMAX_DELAY UINT32_MAX
#define pdMS_TO_TICKS(ms) (ms)
#define pdPASS 1
typedef int portMUX_TYPE;
#define portMUX_INITIALIZER_UNLOCKED 0
void usb_test_enter_critical(portMUX_TYPE *mux);
void usb_test_exit_critical(portMUX_TYPE *mux);
#define portENTER_CRITICAL(mux) usb_test_enter_critical(mux)
#define portEXIT_CRITICAL(mux) usb_test_exit_critical(mux)

#pragma once
#include "board_support.h"
/* Does not return while serving; on failure caller must keep the board SAFE. */
esp_err_t bench_bridge_run(board_context_t *board);

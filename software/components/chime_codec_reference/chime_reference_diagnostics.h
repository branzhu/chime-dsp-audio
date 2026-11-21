#pragma once
/* Control-time failure diagnostics only; no per-frame UART logging. */
void chime_reference_init_failed(const char *stage, int result);

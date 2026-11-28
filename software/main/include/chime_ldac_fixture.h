#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {
    const uint8_t *payload;
    size_t length;
    unsigned rate;
    uint32_t cycle_frames,packets;
} chime_ldac_fixture_t;
/* Read-only mmap + pinned SHA256 validation; call explicitly while idle, never
 * from audio/ISR. Mapping is immutable and retained until reset. No writes. */
esp_err_t chime_ldac_fixture_prepare(chime_ldac_fixture_t *out);

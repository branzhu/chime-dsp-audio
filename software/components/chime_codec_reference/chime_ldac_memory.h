#pragma once
#include <stddef.h>
#include <stdint.h>
/* Decoder-task only allocation/free; snapshots are cross-task diagnostics.
 * This allocator never falls back to RTC RAM or PSRAM. */
#define CHIME_LDAC_FAST_BYTES 16384U
void *chime_ldac_fast_malloc(size_t bytes);
void *chime_ldac_fast_calloc(size_t count, size_t bytes);
void chime_ldac_fast_free(void *pointer);
typedef struct {
    uint32_t reserved_bytes, used_bytes, peak_bytes, failures;
    uintptr_t base;
} chime_ldac_memory_stats_t;
void chime_ldac_memory_snapshot(chime_ldac_memory_stats_t *out);

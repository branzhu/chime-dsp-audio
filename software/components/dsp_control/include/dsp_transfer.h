#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DSP_TRANSFER_CHUNK_BYTES 256U
#define DSP_TRANSFER_MAX_BYTES 65536U
#define DSP_TRANSFER_MAX_REGIONS 96U
#define DSP_TRANSFER_DEADLINE_US 30000000ULL
#define DSP_SAFELOAD_INTERVAL_US 25U

enum {
    DSP_REGION_READ = 1U,
    DSP_REGION_WRITE = 2U,
    DSP_REGION_STOPPED = 4U,
    DSP_REGION_SAFELOAD_TARGET = 8U,
};

typedef enum {
    DSP_TRANSFER_OK = 0,
    DSP_TRANSFER_INVALID,
    DSP_TRANSFER_ACCESS,
    DSP_TRANSFER_IO,
    DSP_TRANSFER_TIMEOUT,
    DSP_TRANSFER_CANCELLED,
    DSP_TRANSFER_NOT_RUNNING,
} dsp_transfer_result_t;

typedef enum {
    DSP_PHASE_RAW = 0,
    DSP_PHASE_SAFELOAD_DATA,
    DSP_PHASE_SAFELOAD_TARGET,
    DSP_PHASE_SAFELOAD_TRIGGER,
    DSP_PHASE_SAFELOAD_WAIT,
} dsp_transfer_phase_t;

typedef struct {
    uint16_t first;
    uint16_t last;
    uint8_t word_bytes;
    uint8_t permissions;
} dsp_memory_region_t;

typedef struct {
    void *context;
    int (*read)(void *context, uint16_t address, uint8_t *data, size_t bytes, uint32_t timeout_ms);
    int (*write)(void *context, uint16_t address, const uint8_t *data, size_t bytes, uint32_t timeout_ms);
    uint64_t (*now_us)(void *context);
    void (*wait_us)(void *context, uint32_t duration_us);
    bool (*cancelled)(void *context);
} dsp_transfer_io_t;

typedef struct {
    uint16_t data;
    uint16_t target;
    uint16_t trigger;
} dsp_safeload_map_t;

typedef struct {
    dsp_transfer_result_t result;
    dsp_transfer_phase_t phase;
    int io_error;
    uint16_t failed_address;
    size_t completed_bytes;
    bool write_attempted;
    bool trigger_attempted;
} dsp_transfer_diagnostic_t;

/* The owning DSP service serializes calls, supplies trusted G1 regions and
 * sets core_running only after verifying ready and the 48 kHz sample clock.
 * Regions remain immutable and alive until the transfer is retired; now_us
 * must be monotonic. This layer never acquires a GPIO, authorizes a session,
 * infers a memory map or retries an I2C operation. */
typedef struct {
    dsp_transfer_io_t io;
    const dsp_memory_region_t *regions;
    size_t region_count;
    bool core_running;
    bool initialized;
    bool safeload_pending;
    uint64_t last_trigger_us;
    uint64_t started_us;
    dsp_transfer_diagnostic_t diagnostic;
} dsp_transfer_t;

bool dsp_transfer_init(dsp_transfer_t *transfer, dsp_transfer_io_t io,
                       const dsp_memory_region_t *regions, size_t region_count);
dsp_transfer_result_t dsp_transfer_read(dsp_transfer_t *transfer, uint16_t address,
                                      uint8_t *data, size_t bytes);
dsp_transfer_result_t dsp_transfer_write(dsp_transfer_t *transfer, uint16_t address,
                                       const uint8_t *data, size_t bytes);
dsp_transfer_result_t dsp_transfer_safeload(dsp_transfer_t *transfer, dsp_safeload_map_t map,
                                          uint16_t address, const uint8_t *data, size_t words);

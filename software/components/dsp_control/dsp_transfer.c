#include "dsp_transfer.h"

#include <string.h>

bool dsp_transfer_init(dsp_transfer_t *transfer, dsp_transfer_io_t io,
                       const dsp_memory_region_t *regions, size_t region_count)
{
    if (transfer == NULL) {
        return false;
    }
    *transfer = (dsp_transfer_t){0};
    if (io.read == NULL || io.write == NULL || io.now_us == NULL ||
        regions == NULL || region_count == 0 || region_count > DSP_TRANSFER_MAX_REGIONS) {
        return false;
    }
    for (size_t index = 0; index < region_count; ++index) {
        const dsp_memory_region_t *region = &regions[index];
        if (region->first > region->last ||
            (region->word_bytes != 2 && region->word_bytes != 4) ||
            (region->permissions & ~(DSP_REGION_READ | DSP_REGION_WRITE |
                                      DSP_REGION_STOPPED | DSP_REGION_SAFELOAD_TARGET)) != 0 ||
            (region->permissions & (DSP_REGION_READ | DSP_REGION_WRITE)) == 0 ||
            ((region->permissions & DSP_REGION_SAFELOAD_TARGET) &&
             (region->word_bytes != 4 || !(region->permissions & DSP_REGION_WRITE))) ||
            (index != 0 && regions[index - 1].last >= region->first)) {
            return false;
        }
    }
    *transfer = (dsp_transfer_t){
        .io = io, .regions = regions, .region_count = region_count, .initialized = true,
    };
    return true;
}

static const dsp_memory_region_t *find_region(const dsp_transfer_t *transfer,
                                             uint16_t address, size_t bytes, uint8_t access)
{
    if (bytes == 0 || bytes > DSP_TRANSFER_MAX_BYTES) {
        return NULL;
    }
    for (size_t index = 0; index < transfer->region_count; ++index) {
        const dsp_memory_region_t *region = &transfer->regions[index];
        if (address >= region->first && address <= region->last &&
            (region->permissions & access) == access && bytes % region->word_bytes == 0 &&
            bytes / region->word_bytes <= (size_t)region->last - address + 1) {
            return region;
        }
    }
    return NULL;
}

static dsp_transfer_result_t fail(dsp_transfer_t *transfer, dsp_transfer_result_t result)
{
    transfer->diagnostic.result = result;
    return result;
}

static void begin(dsp_transfer_t *transfer, uint16_t address)
{
    transfer->diagnostic = (dsp_transfer_diagnostic_t){ .failed_address = address };
    transfer->started_us = transfer->io.now_us(transfer->io.context);
}

static dsp_transfer_result_t check(dsp_transfer_t *transfer)
{
    if (transfer->io.cancelled != NULL && transfer->io.cancelled(transfer->io.context)) {
        return fail(transfer, DSP_TRANSFER_CANCELLED);
    }
    if (transfer->io.now_us(transfer->io.context) - transfer->started_us >= DSP_TRANSFER_DEADLINE_US) {
        return fail(transfer, DSP_TRANSFER_TIMEOUT);
    }
    return DSP_TRANSFER_OK;
}

static dsp_transfer_result_t execute(dsp_transfer_t *transfer, uint16_t address,
                                    uint8_t *read_data, const uint8_t *write_data, size_t bytes)
{
    const bool write = write_data != NULL;
    const dsp_memory_region_t *region = find_region(transfer, address, bytes,
                                                   write ? DSP_REGION_WRITE : DSP_REGION_READ);
    if (region == NULL || (transfer->core_running && (region->permissions & DSP_REGION_STOPPED))) {
        return fail(transfer, DSP_TRANSFER_ACCESS);
    }
    for (size_t offset = 0; offset < bytes;) {
        transfer->diagnostic.failed_address = (uint16_t)(address + offset / region->word_bytes);
        if (check(transfer) != DSP_TRANSFER_OK) {
            return transfer->diagnostic.result;
        }
        const size_t count = bytes - offset < DSP_TRANSFER_CHUNK_BYTES ?
                             bytes - offset : DSP_TRANSFER_CHUNK_BYTES;
        const uint64_t elapsed = transfer->io.now_us(transfer->io.context) - transfer->started_us;
        if (elapsed >= DSP_TRANSFER_DEADLINE_US) {
            return fail(transfer, DSP_TRANSFER_TIMEOUT);
        }
        const uint64_t remaining = DSP_TRANSFER_DEADLINE_US - elapsed;
        const uint32_t timeout_ms = remaining >= 100000 ? 100 : (uint32_t)((remaining + 999) / 1000);
        int error;
        if (write) {
            transfer->diagnostic.write_attempted = true;
            if (transfer->diagnostic.phase == DSP_PHASE_SAFELOAD_TRIGGER) {
                transfer->diagnostic.trigger_attempted = true;
            }
            error = transfer->io.write(transfer->io.context, transfer->diagnostic.failed_address,
                                       write_data + offset, count, timeout_ms);
        } else {
            error = transfer->io.read(transfer->io.context, transfer->diagnostic.failed_address,
                                      read_data + offset, count, timeout_ms);
        }
        if (error != 0) {
            transfer->diagnostic.io_error = error;
            return fail(transfer, DSP_TRANSFER_IO);
        }
        offset += count;
        transfer->diagnostic.completed_bytes += count;
        if (check(transfer) != DSP_TRANSFER_OK) {
            return transfer->diagnostic.result;
        }
    }
    return DSP_TRANSFER_OK;
}

dsp_transfer_result_t dsp_transfer_read(dsp_transfer_t *transfer, uint16_t address,
                                      uint8_t *data, size_t bytes)
{
    if (transfer == NULL || !transfer->initialized || data == NULL) {
        return DSP_TRANSFER_INVALID;
    }
    begin(transfer, address);
    return execute(transfer, address, data, NULL, bytes);
}

dsp_transfer_result_t dsp_transfer_write(dsp_transfer_t *transfer, uint16_t address,
                                       const uint8_t *data, size_t bytes)
{
    if (transfer == NULL || !transfer->initialized || data == NULL) {
        return DSP_TRANSFER_INVALID;
    }
    begin(transfer, address);
    return execute(transfer, address, NULL, data, bytes);
}

static void encode_word(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static dsp_transfer_result_t wait_consumed(dsp_transfer_t *transfer)
{
    if (transfer->safeload_pending) {
        transfer->diagnostic.phase = DSP_PHASE_SAFELOAD_WAIT;
        const uint64_t elapsed = transfer->io.now_us(transfer->io.context) - transfer->last_trigger_us;
        if (elapsed < DSP_SAFELOAD_INTERVAL_US) {
            transfer->io.wait_us(transfer->io.context, (uint32_t)(DSP_SAFELOAD_INTERVAL_US - elapsed));
        }
        if (check(transfer) != DSP_TRANSFER_OK) {
            return transfer->diagnostic.result;
        }
        if (transfer->io.now_us(transfer->io.context) - transfer->last_trigger_us < DSP_SAFELOAD_INTERVAL_US) {
            return fail(transfer, DSP_TRANSFER_TIMEOUT);
        }
        transfer->safeload_pending = false;
    }
    return DSP_TRANSFER_OK;
}

dsp_transfer_result_t dsp_transfer_safeload(dsp_transfer_t *transfer, dsp_safeload_map_t map,
                                          uint16_t address, const uint8_t *data, size_t words)
{
    if (transfer == NULL || !transfer->initialized || data == NULL) {
        return DSP_TRANSFER_INVALID;
    }
    begin(transfer, address);
    if (words == 0 || words > 5 || transfer->io.wait_us == NULL || map.data > UINT16_MAX - 4) {
        return fail(transfer, DSP_TRANSFER_INVALID);
    }
    if (!transfer->core_running) {
        return fail(transfer, DSP_TRANSFER_NOT_RUNNING);
    }
    const dsp_memory_region_t *target = find_region(transfer, address, words * 4,
                                                   DSP_REGION_WRITE | DSP_REGION_SAFELOAD_TARGET);
    const dsp_memory_region_t *slots = find_region(transfer, map.data, 20, DSP_REGION_WRITE);
    const dsp_memory_region_t *target_reg = find_region(transfer, map.target, 4, DSP_REGION_WRITE);
    const dsp_memory_region_t *trigger_reg = find_region(transfer, map.trigger, 4, DSP_REGION_WRITE);
    if (target == NULL || slots == NULL || slots->word_bytes != 4 || target_reg == NULL ||
        target_reg->word_bytes != 4 || trigger_reg == NULL || trigger_reg->word_bytes != 4 ||
        ((slots->permissions | target_reg->permissions | trigger_reg->permissions) & DSP_REGION_STOPPED) ||
        map.target == map.trigger || (map.target >= map.data && map.target <= map.data + 4) ||
        (map.trigger >= map.data && map.trigger <= map.data + 4) ||
        (address <= map.data + 4 && address + words - 1 >= map.data) ||
        (map.target >= address && map.target < address + words) ||
        (map.trigger >= address && map.trigger < address + words)) {
        return fail(transfer, DSP_TRANSFER_ACCESS);
    }
    if (wait_consumed(transfer) != DSP_TRANSFER_OK) {
        return transfer->diagnostic.result;
    }
    transfer->diagnostic.phase = DSP_PHASE_SAFELOAD_DATA;
    if (execute(transfer, map.data, NULL, data, words * 4) != DSP_TRANSFER_OK) {
        return transfer->diagnostic.result;
    }
    uint8_t value[4];
    encode_word(value, address);
    transfer->diagnostic.phase = DSP_PHASE_SAFELOAD_TARGET;
    if (execute(transfer, map.target, NULL, value, sizeof(value)) != DSP_TRANSFER_OK) {
        return transfer->diagnostic.result;
    }
    encode_word(value, (uint32_t)words);
    transfer->diagnostic.phase = DSP_PHASE_SAFELOAD_TRIGGER;
    /* A failed trigger write may still have reached the device: never replay. */
    if (check(transfer) != DSP_TRANSFER_OK) {
        return transfer->diagnostic.result;
    }
    const dsp_transfer_result_t result = execute(transfer, map.trigger, NULL, value, sizeof(value));
    if (transfer->diagnostic.trigger_attempted) {
        transfer->last_trigger_us = transfer->io.now_us(transfer->io.context);
        transfer->safeload_pending = true;
    }
    if (result != DSP_TRANSFER_OK) {
        return result;
    }
    /* Ownership must extend through consumption, before another slots write. */
    return wait_consumed(transfer);
}

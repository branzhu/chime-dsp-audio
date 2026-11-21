#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsp_transfer.h"
#include "sigma_tcp_bridge.h"
#include "native_frames.h"

/* Synthetic policy only. These ranges do not assert an ADAU1451 memory map. */
static const dsp_memory_region_t regions[] = {
    {0x1000, 0x13ff, 4, DSP_REGION_READ | DSP_REGION_WRITE | DSP_REGION_STOPPED | DSP_REGION_SAFELOAD_TARGET},
    {0x6000, 0x6006, 4, DSP_REGION_READ | DSP_REGION_WRITE},
    {0xc000, 0xc1ff, 4, DSP_REGION_READ | DSP_REGION_WRITE | DSP_REGION_STOPPED},
    {0xf000, 0xf1ff, 2, DSP_REGION_READ | DSP_REGION_WRITE},
    {0xf200, 0xf200, 2, DSP_REGION_READ},
    {0xf890, 0xf890, 2, DSP_REGION_READ | DSP_REGION_WRITE},
};
#ifdef SIGMA_TEST_CLASSIC47
#define TEST_PROFILE_KIND SIGMA_PROFILE_CLASSIC_4_7_RW
#else
#define TEST_PROFILE_KIND SIGMA_PROFILE_PLUS_3_4_0_RW
#endif
static const sigma_protocol_profile_t profile = {TEST_PROFILE_KIND, 0x70, 0};
static const dsp_safeload_map_t safeload_map = {0x6000, 0x6005, 0x6006};

typedef struct {
    uint16_t address;
    size_t bytes;
    uint32_t timeout_ms;
    bool write;
    uint8_t data[DSP_TRANSFER_CHUNK_BYTES];
} transaction_t;

typedef struct {
    dsp_transfer_t transfer;
    sigma_bridge_t bridge;
    uint8_t storage[SIGMA_MAX_FRAME_BYTES];
    uint8_t sent[SIGMA_MAX_FRAME_BYTES];
    uint8_t memory[65536 * 4];
    transaction_t transactions[512];
    size_t calls;
    size_t fail_call;
    size_t cancel_after;
    bool cancel;
    uint64_t now;
    uint64_t transaction_us;
    size_t send_calls;
    size_t send_failure;
    size_t send_limit;
    size_t sent_bytes;
    uint64_t send_us;
    size_t executions;
    bool short_wait;
} fixture_t;

static uint64_t now_us(void *context)
{
    return ((fixture_t *)context)->now;
}

static bool cancelled(void *context)
{
    const fixture_t *fixture = context;
    return fixture->cancel || (fixture->cancel_after != 0 && fixture->calls >= fixture->cancel_after);
}

static void wait_us(void *context, uint32_t duration)
{
    fixture_t *fixture = context;
    if (!fixture->short_wait) {
        fixture->now += duration;
    }
}

static int transaction(fixture_t *fixture, uint16_t address, uint8_t *read_data,
                       const uint8_t *write_data, size_t bytes, uint32_t timeout_ms)
{
    assert(fixture->calls < sizeof(fixture->transactions) / sizeof(fixture->transactions[0]));
    assert(bytes <= DSP_TRANSFER_CHUNK_BYTES);
    assert(timeout_ms >= 1 && timeout_ms <= 100);
    transaction_t *entry = &fixture->transactions[fixture->calls++];
    *entry = (transaction_t){ .address = address, .bytes = bytes, .timeout_ms = timeout_ms,
                             .write = write_data != NULL };
    const size_t base = (size_t)address * (address >= 0xf000 ? 2U : 4U);
    fixture->now += fixture->transaction_us;
    if (fixture->calls == fixture->fail_call) {
        /* A failed write can already have changed a byte at the device. */
        if (write_data != NULL) {
            fixture->memory[base] = write_data[0];
        }
        return -73;
    }
    if (write_data != NULL) {
        memcpy(entry->data, write_data, bytes);
        memcpy(fixture->memory + base, write_data, bytes);
    } else {
        memcpy(read_data, fixture->memory + base, bytes);
        memcpy(entry->data, read_data, bytes);
    }
    return 0;
}

static int read_io(void *context, uint16_t address, uint8_t *data, size_t bytes, uint32_t timeout_ms)
{
    return transaction(context, address, data, NULL, bytes, timeout_ms);
}

static int write_io(void *context, uint16_t address, const uint8_t *data, size_t bytes, uint32_t timeout_ms)
{
    return transaction(context, address, NULL, data, bytes, timeout_ms);
}

static int execute(void *context, const sigma_request_t *request, uint8_t *read_data)
{
    fixture_t *fixture = context;
    ++fixture->executions;
    /* Deliberately reject native flagged writes until an actual G0 project
     * establishes whether they represent staging or semantic requests. */
    if (request->flag != 0) {
        return -51;
    }
    if (request->control == 0x09) {
        return (int)dsp_transfer_write(&fixture->transfer, request->address, request->data, request->data_length);
    }
    return (int)dsp_transfer_read(&fixture->transfer, request->address, read_data, request->data_length);
}

static int send_io(void *context, const uint8_t *data, size_t bytes, uint32_t timeout_ms)
{
    fixture_t *fixture = context;
    assert(timeout_ms >= 1 && timeout_ms <= 30000);
    ++fixture->send_calls;
    fixture->now += fixture->send_us;
    if (fixture->send_calls == fixture->send_failure) {
        return -91;
    }
    const size_t count = bytes < fixture->send_limit ? bytes : fixture->send_limit;
    assert(fixture->sent_bytes + count <= sizeof(fixture->sent));
    memcpy(fixture->sent + fixture->sent_bytes, data, count);
    fixture->sent_bytes += count;
    return (int)count;
}

static fixture_t *create_fixture(void)
{
    fixture_t *fixture = calloc(1, sizeof(*fixture));
    assert(fixture != NULL);
    const dsp_transfer_io_t io = {
        .context = fixture, .read = read_io, .write = write_io, .now_us = now_us,
        .wait_us = wait_us, .cancelled = cancelled,
    };
    assert(dsp_transfer_init(&fixture->transfer, io, regions, sizeof(regions) / sizeof(regions[0])));
    const sigma_bridge_io_t bridge_io = { .context = fixture, .now_us = now_us, .execute = execute, .send = send_io };
    assert(sigma_bridge_init(&fixture->bridge, profile, bridge_io, fixture->storage, sizeof(fixture->storage)));
    fixture->send_limit = 3;
    return fixture;
}

static void put32(uint8_t *data, uint32_t value)
{
    data[0] = (uint8_t)(value >> 24);
    data[1] = (uint8_t)(value >> 16);
    data[2] = (uint8_t)(value >> 8);
    data[3] = (uint8_t)value;
}

static size_t make_write(uint8_t *data, uint16_t address, size_t bytes)
{
    assert(bytes <= SIGMA_MAX_PAYLOAD_BYTES);
    memcpy(data, native_write, SIGMA_HEADER_BYTES);
    put32(data + 3, (uint32_t)(SIGMA_HEADER_BYTES + bytes));
    put32(data + 8, (uint32_t)bytes);
    data[12] = (uint8_t)(address >> 8);
    data[13] = (uint8_t)address;
    for (size_t index = 0; index < bytes; ++index) {
        data[SIGMA_HEADER_BYTES + index] = (uint8_t)index;
    }
    return SIGMA_HEADER_BYTES + bytes;
}

static void test_native_frames(void)
{
    const uint8_t *frames[] = {native_write, native_flagged_write, native_read};
    const size_t sizes[] = {sizeof(native_write), sizeof(native_flagged_write), sizeof(native_read)};
    for (size_t frame = 0; frame < 3; ++frame) {
        for (size_t split = 0; split <= sizes[frame]; ++split) {
            uint8_t storage[64];
            sigma_decoder_t decoder;
            sigma_request_t request;
            assert(sigma_decoder_init(&decoder, profile, storage, sizeof(storage)));
            size_t used = 0;
            sigma_protocol_result_t result = sigma_decoder_feed(&decoder, frames[frame], split, &used, &request);
            assert(used == split);
            if (split < sizes[frame]) {
                assert(result == SIGMA_PROTOCOL_MORE);
                result = sigma_decoder_feed(&decoder, frames[frame] + split, sizes[frame] - split, &used, &request);
                assert(used == sizes[frame] - split);
            }
            assert(result == SIGMA_PROTOCOL_FRAME);
            assert(request.address == 0x1234 && request.data_length == 4);
            assert(request.control == (frame == 2 ? 0x0a : 0x09));
            assert(request.flag == (frame == 1 ? 1 : 0));
            assert(frame != 2 || request.data == NULL);
            sigma_decoder_release(&decoder);
            assert(sigma_decoder_finish(&decoder));
        }
    }
    uint8_t storage[18] = {0};
    memcpy(storage + SIGMA_HEADER_BYTES, native_write + SIGMA_HEADER_BYTES, 4);
    assert(sigma_protocol_read_response(storage, sizeof(storage), profile, 0x1234, 4));
    assert(memcmp(storage, native_response, sizeof(native_response)) == 0);
    assert(memcmp(storage, native_response_reversed, sizeof(native_response_reversed)) != 0);
    puts("PASS native driver frames: every split, flags, read without payload, BE reply");
}

static void test_stream_order(void)
{
    uint8_t stream[sizeof(native_write) * 2 + sizeof(native_read)];
    memcpy(stream, native_write, sizeof(native_write));
    memcpy(stream + sizeof(native_write), native_write, sizeof(native_write));
    stream[sizeof(native_write) + SIGMA_HEADER_BYTES] = 0x55;
    memcpy(stream + sizeof(native_write) * 2, native_read, sizeof(native_read));
    for (size_t step = 1; step <= sizeof(stream); ++step) {
        fixture_t *fixture = create_fixture();
        for (size_t offset = 0; offset < sizeof(stream);) {
            const size_t count = sizeof(stream) - offset < step ? sizeof(stream) - offset : step;
            assert(sigma_bridge_receive(&fixture->bridge, stream + offset, count));
            offset += count;
        }
        assert(fixture->bridge.diagnostic.completed_frames == 3 && fixture->calls == 3);
        assert(fixture->transactions[0].write && fixture->transactions[1].write && !fixture->transactions[2].write);
        assert(fixture->sent_bytes == sizeof(native_response));
        assert(memcmp(fixture->sent, native_response, SIGMA_HEADER_BYTES) == 0);
        assert(fixture->sent[SIGMA_HEADER_BYTES] == 0x55);
        assert(fixture->send_calls == 6);
        assert(sigma_bridge_finish(&fixture->bridge));
        free(fixture);
    }
    puts("PASS stream ordering: bytewise/coalesced two writes + read; short sends; no write ACK");
}

static void test_truncation(void)
{
    for (size_t length = 1; length < sizeof(native_write); ++length) {
        fixture_t *fixture = create_fixture();
        assert(sigma_bridge_receive(&fixture->bridge, native_write, length));
        assert(fixture->calls == 0 && fixture->executions == 0);
        assert(!sigma_bridge_finish(&fixture->bridge));
        assert(fixture->bridge.diagnostic.error == SIGMA_BRIDGE_TRUNCATED);
        assert(!sigma_bridge_receive(&fixture->bridge, native_write, sizeof(native_write)));
        assert(fixture->calls == 0);
        free(fixture);
    }
    puts("PASS truncated writes: every prefix has zero I2C side effects and terminal EOF");
}

static void reject_header(const uint8_t *header)
{
    fixture_t *fixture = create_fixture();
    assert(!sigma_bridge_receive(&fixture->bridge, header, SIGMA_HEADER_BYTES));
    assert(fixture->bridge.diagnostic.error ==
           (header[0] == SIGMA_CONTROL_WRITE || header[0] == SIGMA_CONTROL_READ
                ? SIGMA_BRIDGE_PROTOCOL : SIGMA_BRIDGE_UNSUPPORTED_CONTROL));
    assert(fixture->executions == 0 && fixture->calls == 0 && fixture->sent_bytes == 0);
    assert(!sigma_bridge_receive(&fixture->bridge, native_read, sizeof(native_read)));
    assert(fixture->executions == 0);
    free(fixture);
}

static void test_invalid_frames(void)
{
    uint8_t header[SIGMA_HEADER_BYTES];
    const uint32_t invalid[] = {0, 1, 13, 15, 65551, UINT32_MAX};
    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        memcpy(header, native_write, sizeof(header));
        put32(header + 3, invalid[index]);
        reject_header(header);
    }
    const uint32_t counts[] = {0, 65537, UINT32_MAX};
    for (size_t index = 0; index < sizeof(counts) / sizeof(counts[0]); ++index) {
        memcpy(header, native_write, sizeof(header));
        put32(header + 8, counts[index]);
        reject_header(header);
        memcpy(header, native_read, sizeof(header));
        put32(header + 6, counts[index]);
        reject_header(header);
    }
    const size_t offsets[] = {0, 1, 2, 7};
    const uint8_t values[] = {0x0b, 2, 1, 0x38};
    for (size_t index = 0; index < 4; ++index) {
        memcpy(header, native_write, sizeof(header));
        header[offsets[index]] = values[index];
        reject_header(header);
    }
    for (size_t index = 12; index <= 13; ++index) {
        memcpy(header, native_read, sizeof(header));
        header[index] = 1;
        reject_header(header);
    }
    puts("PASS invalid lengths, overflow, control, chip, channel, flags, reserved: no resync");
}

static void test_explicit_profiles_and_native_prefix(void)
{
    uint8_t storage[64];
    sigma_decoder_t decoder;
    sigma_request_t request;
    size_t consumed = 0;
    sigma_protocol_profile_t invalid = {SIGMA_PROFILE_UNSPECIFIED, 0x70, 0};
    assert(!sigma_decoder_init(&decoder, invalid, storage, sizeof(storage)));
    assert(!sigma_protocol_read_response(storage, sizeof(storage), invalid, 0x1234, 4));
    invalid.kind = (sigma_protocol_kind_t)99;
    assert(!sigma_decoder_init(&decoder, invalid, storage, sizeof(storage)));
    const uint8_t unsupported[] = {0, 0x1b, 0xff};
    for (size_t i = 0; i < sizeof(unsupported); ++i) {
        fixture_t *fixture = create_fixture();
        assert(!sigma_bridge_receive(&fixture->bridge, unsupported + i, 1));
        assert(fixture->bridge.diagnostic.error == SIGMA_BRIDGE_UNSUPPORTED_CONTROL);
        assert(fixture->bridge.diagnostic.control == unsupported[i]);
        assert(fixture->bridge.decoder.used == 1);
        assert(fixture->executions == 0 && fixture->calls == 0 && fixture->sent_bytes == 0);
        assert(!sigma_bridge_receive(&fixture->bridge, native_write, sizeof(native_write)));
        free(fixture);
    }
    uint8_t stream[sizeof(native_connection_prefix) + sizeof(native_write)];
    memcpy(stream, native_connection_prefix, sizeof(native_connection_prefix));
    memcpy(stream + sizeof(native_connection_prefix), native_write, sizeof(native_write));
    fixture_t *fixture = create_fixture();
    assert(!sigma_bridge_receive(&fixture->bridge, stream, sizeof(stream)));
    assert(fixture->bridge.diagnostic.control == 0x1b && fixture->bridge.decoder.used == 1);
    assert(fixture->executions == 0 && fixture->calls == 0 && fixture->sent_bytes == 0);
    free(fixture);
    const sigma_protocol_profile_t channel_profile = {SIGMA_PROFILE_PLUS_3_4_0_RW, 0x38, 3};
    assert(sigma_decoder_init(&decoder, channel_profile, storage, sizeof(storage)));
    assert(sigma_decoder_feed(&decoder, native_channel_write, sizeof(native_channel_write),
                              &consumed, &request) == SIGMA_PROTOCOL_FRAME);
    assert(request.address == 0x1234 && request.flag == 0 && request.data_length == 4);
    assert(sigma_protocol_read_response(storage, sizeof(storage), channel_profile, 0x1234, 4));
    memcpy(storage + SIGMA_HEADER_BYTES, native_channel_write + SIGMA_HEADER_BYTES, 4);
    assert(memcmp(storage, native_channel_response, sizeof(native_channel_response)) == 0);
    puts("PASS explicit profiles, native chip/channel, immediate 0x1b rejection without I2C/ACK/resync");
}

static void test_frame_budget(void)
{
    uint8_t *frame = malloc(SIGMA_MAX_FRAME_BYTES);
    uint8_t *storage = malloc(SIGMA_MAX_FRAME_BYTES);
    assert(frame != NULL && storage != NULL);
    const size_t length = make_write(frame, 0x1000, SIGMA_MAX_PAYLOAD_BYTES);
    sigma_decoder_t decoder;
    sigma_request_t request;
    size_t consumed;
    assert(sigma_decoder_init(&decoder, profile, storage, SIGMA_MAX_FRAME_BYTES));
    assert(sigma_decoder_feed(&decoder, frame, length, &consumed, &request) == SIGMA_PROTOCOL_FRAME);
    assert(consumed == length && request.data_length == SIGMA_MAX_PAYLOAD_BYTES);
    assert(memcmp(request.data, frame + SIGMA_HEADER_BYTES, SIGMA_MAX_PAYLOAD_BYTES) == 0);
    assert(sigma_decoder_init(&decoder, profile, storage, 17));
    assert(sigma_decoder_feed(&decoder, native_read, sizeof(native_read), &consumed, &request) == SIGMA_PROTOCOL_INVALID);
    assert(!sigma_protocol_read_response(storage, 17, profile, 0x1234, 4));
    free(frame);
    free(storage);
    puts("PASS maximum 64 KiB payload, caller capacity and read response budget");
}

static void test_chunking(void)
{
    uint8_t input[520];
    uint8_t output[520];
    for (size_t index = 0; index < sizeof(input); ++index) {
        input[index] = (uint8_t)(index * 7);
    }
    const uint16_t addresses[] = {0x1000, 0xf000};
    const uint16_t steps[] = {64, 128};
    for (size_t width = 0; width < 2; ++width) {
        fixture_t *fixture = create_fixture();
        assert(dsp_transfer_write(&fixture->transfer, addresses[width], input, sizeof(input)) == DSP_TRANSFER_OK);
        assert(fixture->calls == 3);
        assert(fixture->transactions[1].address == addresses[width] + steps[width]);
        assert(fixture->transactions[2].address == addresses[width] + steps[width] * 2);
        assert(fixture->transactions[2].bytes == 8);
        assert(dsp_transfer_read(&fixture->transfer, addresses[width], output, sizeof(output)) == DSP_TRANSFER_OK);
        assert(fixture->calls == 6 && memcmp(input, output, sizeof(input)) == 0);
        assert(fixture->transfer.diagnostic.completed_bytes == sizeof(input));
        free(fixture);
    }
    puts("PASS 16/32-bit word addressing: 256-byte chunks, aligned tail and exact readback");
}

static void test_access_policy(void)
{
    fixture_t *fixture = create_fixture();
    uint8_t data[512] = {0};
    const uint16_t addresses[] = {0x0fff, 0x13ff, 0xffff, 0xc200, 0xf200};
    const size_t counts[] = {4, 8, 4, 4, 2};
    for (size_t index = 0; index < 5; ++index) {
        assert(dsp_transfer_write(&fixture->transfer, addresses[index], data, counts[index]) == DSP_TRANSFER_ACCESS);
    }
    assert(dsp_transfer_write(&fixture->transfer, 0x1000, data, 3) == DSP_TRANSFER_ACCESS);
    assert(dsp_transfer_write(&fixture->transfer, 0xf000, data, 3) == DSP_TRANSFER_ACCESS);
    assert(dsp_transfer_write(&fixture->transfer, 0x1000, data, 0) == DSP_TRANSFER_ACCESS);
    assert(dsp_transfer_write(&fixture->transfer, 0x1000, data, DSP_TRANSFER_MAX_BYTES + 1) == DSP_TRANSFER_ACCESS);
    fixture->transfer.core_running = true;
    assert(dsp_transfer_write(&fixture->transfer, 0x1000, data, 4) == DSP_TRANSFER_ACCESS);
    assert(dsp_transfer_read(&fixture->transfer, 0xc000, data, 4) == DSP_TRANSFER_ACCESS);
    assert(fixture->calls == 0);
    free(fixture);
    puts("PASS permissions, holes, cross-region, alignment, wrap and stopped-only program RAM");
}

static void test_region_definitions(void)
{
    fixture_t *fixture = create_fixture();
    const dsp_transfer_io_t io = fixture->transfer.io;
    dsp_memory_region_t invalid[] = {
        {0x1001, 0x1000, 4, DSP_REGION_READ},
        {0x1000, 0x1001, 3, DSP_REGION_READ},
        {0x1000, 0x1001, 4, 0},
        {0x1000, 0x1001, 4, 0xff},
        {0x1000, 0x1001, 2, DSP_REGION_READ | DSP_REGION_WRITE | DSP_REGION_SAFELOAD_TARGET},
        {0x1000, 0x1001, 4, DSP_REGION_READ | DSP_REGION_SAFELOAD_TARGET},
    };
    for (size_t index = 0; index < sizeof(invalid) / sizeof(invalid[0]); ++index) {
        assert(!dsp_transfer_init(&fixture->transfer, io, &invalid[index], 1));
        assert(!fixture->transfer.initialized);
    }
    const dsp_memory_region_t overlapping[] = {
        {0x1000, 0x1010, 4, DSP_REGION_READ}, {0x1010, 0x1020, 4, DSP_REGION_READ},
    };
    assert(!dsp_transfer_init(&fixture->transfer, io, overlapping, 2));
    assert(!dsp_transfer_init(&fixture->transfer, io, regions, 0));
    assert(!dsp_transfer_init(&fixture->transfer, io, regions, DSP_TRANSFER_MAX_REGIONS + 1));
    assert(fixture->calls == 0);
    free(fixture);
    puts("PASS malformed/overlapping region policies and read-only Safeload target rejected");
}

static void test_io_failures(void)
{
    for (size_t fail_call = 1; fail_call <= 3; ++fail_call) {
        fixture_t *fixture = create_fixture();
        uint8_t frame[SIGMA_HEADER_BYTES + 520];
        const size_t length = make_write(frame, 0x1000, 520);
        fixture->fail_call = fail_call;
        assert(!sigma_bridge_receive(&fixture->bridge, frame, length));
        assert(fixture->calls == fail_call && fixture->sent_bytes == 0);
        assert(fixture->transfer.diagnostic.io_error == -73);
        assert(fixture->transfer.diagnostic.completed_bytes == (fail_call - 1) * 256);
        assert(fixture->transfer.diagnostic.failed_address == 0x1000 + (fail_call - 1) * 64);
        const sigma_bridge_diagnostic_t first = fixture->bridge.diagnostic;
        assert(!sigma_bridge_receive(&fixture->bridge, native_read, sizeof(native_read)));
        assert(!sigma_bridge_finish(&fixture->bridge));
        assert(fixture->calls == fail_call && fixture->bridge.diagnostic.error == first.error);
        assert(fixture->bridge.diagnostic.operation_error == first.operation_error);
        free(fixture);
    }
    fixture_t *fixture = create_fixture();
    fixture->fail_call = 1;
    assert(!sigma_bridge_receive(&fixture->bridge, native_read, sizeof(native_read)));
    assert(fixture->sent_bytes == 0 && fixture->bridge.diagnostic.error == SIGMA_BRIDGE_EXECUTION);
    free(fixture);
    puts("PASS NACK/partial write at every chunk, first error retained, no replay or fake read success");
}

static void test_safeload(void)
{
    const uint8_t values[20] = {1, 2, 3, 4, 5, 6, 7, 8};
    for (size_t words = 1; words <= 5; ++words) {
        fixture_t *fixture = create_fixture();
        fixture->transfer.core_running = true;
        for (size_t update = 0; update < 2; ++update) {
            assert(dsp_transfer_safeload(&fixture->transfer, safeload_map, 0x1234, values, words) == DSP_TRANSFER_OK);
        }
        assert(fixture->calls == 6 && fixture->now >= 50);
        for (size_t update = 0; update < 2; ++update) {
            const transaction_t *entries = fixture->transactions + update * 3;
            assert(entries[0].address == 0x6000 && entries[0].bytes == words * 4);
            assert(memcmp(entries[0].data, values, words * 4) == 0);
            assert(entries[1].address == 0x6005 && entries[1].bytes == 4);
            assert(memcmp(entries[1].data, (uint8_t[]){0, 0, 0x12, 0x34}, 4) == 0);
            assert(entries[2].address == 0x6006 && entries[2].data[3] == words);
        }
        assert(!fixture->transfer.safeload_pending);
        free(fixture);
    }
    puts("PASS 1..5-word software Safeload ordering, encoded target/count and consume interval");
}

static void test_safeload_failures(void)
{
    const uint8_t data[20] = {1};
    const dsp_transfer_phase_t phases[] = {DSP_PHASE_SAFELOAD_DATA, DSP_PHASE_SAFELOAD_TARGET, DSP_PHASE_SAFELOAD_TRIGGER};
    for (size_t stage = 1; stage <= 3; ++stage) {
        fixture_t *fixture = create_fixture();
        fixture->transfer.core_running = true;
        fixture->fail_call = stage;
        assert(dsp_transfer_safeload(&fixture->transfer, safeload_map, 0x1234, data, 5) == DSP_TRANSFER_IO);
        assert(fixture->calls == stage && fixture->transfer.diagnostic.phase == phases[stage - 1]);
        assert(fixture->transfer.diagnostic.io_error == -73);
        assert(fixture->transfer.diagnostic.trigger_attempted == (stage == 3));
        assert(fixture->transfer.safeload_pending == (stage == 3));
        free(fixture);
    }
    fixture_t *fixture = create_fixture();
    assert(dsp_transfer_safeload(&fixture->transfer, safeload_map, 0x1234, data, 5) == DSP_TRANSFER_NOT_RUNNING);
    fixture->transfer.core_running = true;
    assert(dsp_transfer_safeload(&fixture->transfer, safeload_map, 0x1234, data, 0) == DSP_TRANSFER_INVALID);
    assert(dsp_transfer_safeload(&fixture->transfer, safeload_map, 0x1234, data, 6) == DSP_TRANSFER_INVALID);
    assert(dsp_transfer_safeload(&fixture->transfer, safeload_map, 0x13ff, data, 2) == DSP_TRANSFER_ACCESS);
    assert(dsp_transfer_safeload(&fixture->transfer, (dsp_safeload_map_t){0x6000, 0x6004, 0x6006}, 0x1234, data, 5) == DSP_TRANSFER_ACCESS);
    assert(dsp_transfer_safeload(&fixture->transfer, (dsp_safeload_map_t){0x6000, 0x6005, 0x6005}, 0x1234, data, 5) == DSP_TRANSFER_ACCESS);
    assert(dsp_transfer_safeload(&fixture->transfer, (dsp_safeload_map_t){0x6004, 0x6005, 0x6006}, 0x1234, data, 5) == DSP_TRANSFER_ACCESS);
    assert(fixture->calls == 0);
    fixture->short_wait = true;
    assert(dsp_transfer_safeload(&fixture->transfer, safeload_map, 0x1234, data, 5) == DSP_TRANSFER_TIMEOUT);
    assert(fixture->calls == 3 && fixture->transfer.diagnostic.trigger_attempted);
    assert(fixture->transfer.diagnostic.phase == DSP_PHASE_SAFELOAD_WAIT);
    free(fixture);
    fixture = create_fixture();
    fixture->transfer.core_running = true;
    fixture->cancel_after = 2;
    assert(dsp_transfer_safeload(&fixture->transfer, safeload_map, 0x1234, data, 5) == DSP_TRANSFER_CANCELLED);
    assert(fixture->calls == 2 && !fixture->transfer.diagnostic.trigger_attempted);
    free(fixture);
    fixture = create_fixture();
    fixture->transfer.core_running = true;
    fixture->cancel_after = 3;
    assert(dsp_transfer_safeload(&fixture->transfer, safeload_map, 0x1234, data, 5) == DSP_TRANSFER_CANCELLED);
    assert(fixture->calls == 3 && fixture->transfer.diagnostic.trigger_attempted);
    assert(fixture->transfer.safeload_pending);
    free(fixture);
    puts("PASS Safeload faults at data/target/trigger, unknown consumption, bad maps/count/core state");
}

static void test_timeouts(void)
{
    fixture_t *fixture = create_fixture();
    fixture->now = 999999999;
    assert(sigma_bridge_poll(&fixture->bridge));
    assert(sigma_bridge_receive(&fixture->bridge, native_write, 1));
    fixture->now += SIGMA_FRAME_STALL_US;
    assert(!sigma_bridge_poll(&fixture->bridge));
    assert(fixture->bridge.diagnostic.error == SIGMA_BRIDGE_RECEIVE_TIMEOUT && fixture->calls == 0);
    free(fixture);
    fixture = create_fixture();
    for (size_t index = 0; index < 6; ++index) {
        assert(sigma_bridge_receive(&fixture->bridge, native_write + index, 1));
        fixture->now += 5000000;
    }
    assert(!sigma_bridge_poll(&fixture->bridge));
    assert(fixture->bridge.diagnostic.error == SIGMA_BRIDGE_RECEIVE_TIMEOUT);
    free(fixture);
    fixture = create_fixture();
    uint8_t data[520] = {0};
    fixture->transaction_us = DSP_TRANSFER_DEADLINE_US;
    assert(dsp_transfer_write(&fixture->transfer, 0x1000, data, sizeof(data)) == DSP_TRANSFER_TIMEOUT);
    assert(fixture->calls == 1 && fixture->transfer.diagnostic.completed_bytes == 256);
    free(fixture);
    fixture = create_fixture();
    fixture->transaction_us = DSP_TRANSFER_DEADLINE_US - 999;
    assert(dsp_transfer_write(&fixture->transfer, 0x1000, data, sizeof(data)) == DSP_TRANSFER_TIMEOUT);
    assert(fixture->calls == 2 && fixture->transactions[1].timeout_ms == 1);
    free(fixture);
    puts("PASS idle between frames, stalled/slow frames, request deadline and remaining I2C timeout");
}

static void test_cancel_and_send(void)
{
    uint8_t data[520] = {0};
    fixture_t *fixture = create_fixture();
    fixture->cancel = true;
    assert(dsp_transfer_write(&fixture->transfer, 0x1000, data, sizeof(data)) == DSP_TRANSFER_CANCELLED);
    assert(fixture->calls == 0);
    fixture->cancel = false;
    fixture->cancel_after = 1;
    assert(dsp_transfer_write(&fixture->transfer, 0x1000, data, sizeof(data)) == DSP_TRANSFER_CANCELLED);
    assert(fixture->calls == 1);
    free(fixture);
    fixture = create_fixture();
    fixture->send_failure = 2;
    assert(!sigma_bridge_receive(&fixture->bridge, native_read, sizeof(native_read)));
    assert(fixture->bridge.diagnostic.error == SIGMA_BRIDGE_SEND && fixture->sent_bytes == 3);
    assert(fixture->bridge.diagnostic.response_bytes_sent == 3);
    assert(!sigma_bridge_receive(&fixture->bridge, native_read, sizeof(native_read)) && fixture->calls == 1);
    free(fixture);
    fixture = create_fixture();
    fixture->send_us = SIGMA_FRAME_DEADLINE_US;
    assert(!sigma_bridge_receive(&fixture->bridge, native_read, sizeof(native_read)));
    assert(fixture->bridge.diagnostic.error == SIGMA_BRIDGE_SEND_TIMEOUT && fixture->send_calls == 1);
    free(fixture);
    fixture = create_fixture();
    assert(!sigma_bridge_receive(&fixture->bridge, native_flagged_write, sizeof(native_flagged_write)));
    assert(fixture->bridge.diagnostic.operation_error == -51 && fixture->calls == 0);
    free(fixture);
    puts("PASS cancellation before/between I2C, failed/deadline send, flagged-write policy isolation");
}


static fixture_t *create_lab_fixture(void)
{
    fixture_t *f=create_fixture();
    const sigma_protocol_profile_t lab={SIGMA_PROFILE_PLUS_3_4_0_LAB,0x70,0};
    const sigma_bridge_io_t io=f->bridge.io;
    assert(sigma_bridge_init(&f->bridge,lab,io,f->storage,sizeof(f->storage)));
    return f;
}
static void test_lab_prelude(void)
{
    for(size_t split=0;split<=sizeof(native_connection_prefix);++split) {
        fixture_t *f=create_lab_fixture();
        assert(sigma_bridge_receive(&f->bridge,native_connection_prefix,split));
        assert(sigma_bridge_receive(&f->bridge,native_connection_prefix+split,sizeof(native_connection_prefix)-split));
        assert(f->bridge.diagnostic.lab_init_frames==1 && f->calls==0 && f->send_calls==0 && f->executions==0);
        assert(sigma_bridge_receive(&f->bridge,native_write,sizeof(native_write)));
        assert(sigma_bridge_receive(&f->bridge,native_read,sizeof(native_read)));
        assert(f->executions==2 && f->bridge.diagnostic.completed_frames==2);
        assert(sigma_bridge_finish(&f->bridge));
        free(f);
    }
    uint8_t combined[sizeof(native_connection_prefix)+sizeof(native_write)+sizeof(native_read)];
    memcpy(combined,native_connection_prefix,sizeof(native_connection_prefix));
    memcpy(combined+sizeof(native_connection_prefix),native_write,sizeof(native_write));
    memcpy(combined+sizeof(native_connection_prefix)+sizeof(native_write),native_read,sizeof(native_read));
    fixture_t *f=create_lab_fixture();
    for(size_t i=0;i<sizeof(combined);++i) assert(sigma_bridge_receive(&f->bridge,combined+i,1));
    assert(f->bridge.diagnostic.lab_init_frames==1 && f->executions==2 && f->sent_bytes==sizeof(native_response));
    free(f);
    f=create_lab_fixture();
    assert(sigma_bridge_receive(&f->bridge,combined,sizeof(combined)));
    assert(f->bridge.diagnostic.lab_init_frames==1 && f->executions==2 && f->sent_bytes==sizeof(native_response));
    free(f);
    for(size_t i=0;i<sizeof(native_connection_prefix);++i) {
        uint8_t bad[sizeof(native_connection_prefix)];memcpy(bad,native_connection_prefix,sizeof(bad));bad[i]^=0x80;
        f=create_lab_fixture();
        assert(!sigma_bridge_receive(&f->bridge,bad,sizeof(bad)));
        assert(f->calls==0 && f->send_calls==0 && f->executions==0);
        free(f);
    }
    for(size_t n=1;n<sizeof(native_connection_prefix);++n) {
        f=create_lab_fixture();
        assert(sigma_bridge_receive(&f->bridge,native_connection_prefix,n));
        assert(!sigma_bridge_finish(&f->bridge));
        assert(f->bridge.diagnostic.error==SIGMA_BRIDGE_TRUNCATED && f->calls==0 && f->send_calls==0);
        free(f);
    }
    puts("PASS LAB prelude: every split, byte stream/coalesced R/W, every byte mutation, every truncation; no IO/ACK");
}


static void test_lab_zero_chip_register_read(void)
{
    /* Exact GUI capture: chip 00, 2-byte F000 read. Reply must echo 00, not 70. */
    const uint8_t read_zero[]={0x0a,0,0,0,0x0e,0,0,0,0,2,0xf0,0,0,0};
    const uint8_t reply_zero[]={0x0b,0,0,0,0x10,0,0,0,0,2,0xf0,0,0,0,0,0x60};
    for(size_t split=0;split<=sizeof(read_zero);++split) {
        fixture_t *f=create_lab_fixture();
        f->memory[0xf000U*2U+1U]=0x60;
        assert(sigma_bridge_receive(&f->bridge,read_zero,split));
        assert(sigma_bridge_receive(&f->bridge,read_zero+split,sizeof(read_zero)-split));
        assert(f->calls==1 && f->executions==1 && !f->transactions[0].write);
        assert(f->transactions[0].address==0xf000 && f->transactions[0].bytes==2);
        assert(f->sent_bytes==sizeof(reply_zero) && memcmp(f->sent,reply_zero,sizeof(reply_zero))==0);
        assert(f->bridge.diagnostic.chip_address==0);
        assert(sigma_bridge_finish(&f->bridge));
        free(f);
    }
    /* Mixed wire IDs share only the same fixed DSP executor, not a sticky ID. */
    uint8_t mixed[sizeof(read_zero)*2];
    memcpy(mixed,read_zero,sizeof(read_zero));
    memcpy(mixed+sizeof(read_zero),read_zero,sizeof(read_zero));mixed[sizeof(read_zero)+5]=0x70;
    fixture_t *f=create_lab_fixture();f->send_limit=1;
    assert(sigma_bridge_receive(&f->bridge,mixed,sizeof(mixed)));
    assert(f->sent_bytes==32 && f->sent[5]==0 && f->sent[21]==0x70 && f->calls==2);
    free(f);
    /* Every other ID (including EEPROM A0 and 7-bit 38) remains rejected. */
    for(unsigned chip=1;chip<256;++chip) {
        if(chip==0x70) continue;
        uint8_t bad[sizeof(read_zero)];memcpy(bad,read_zero,sizeof(bad));bad[5]=(uint8_t)chip;
        f=create_lab_fixture();assert(!sigma_bridge_receive(&f->bridge,bad,sizeof(bad)));
        assert(f->calls==0 && f->executions==0 && f->send_calls==0);free(f);
    }
    f=create_fixture();assert(!sigma_bridge_receive(&f->bridge,read_zero,sizeof(read_zero)));
    assert(f->calls==0 && f->executions==0);free(f); /* Both strict profiles. */
    const sigma_protocol_profile_t other_bindings[]={
        {SIGMA_PROFILE_PLUS_3_4_0_LAB,0x38,0}, {SIGMA_PROFILE_PLUS_3_4_0_LAB,0x70,1}};
    for(size_t i=0;i<sizeof(other_bindings)/sizeof(other_bindings[0]);++i) {
        f=create_lab_fixture();const sigma_bridge_io_t io=f->bridge.io;
        assert(sigma_bridge_init(&f->bridge,other_bindings[i],io,f->storage,sizeof(f->storage)));
        assert(!sigma_bridge_receive(&f->bridge,read_zero,sizeof(read_zero)) && f->calls==0);free(f);
    }
    const uint8_t invalid[][14]={
        {0x0a,0,0,0,0x0e,0,0,0,0,2,0xf0,0,0,1}, /* reserved */
        {0x09,0,1,0,0,0,0x10,0,0,0,0,2,0xf0,0}, /* wrong channel */
        {0x09,2,0,0,0,0,0x12,0,0,0,0,4,0,0x14}, /* invalid flag */
    };
    for(size_t i=0;i<sizeof(invalid)/sizeof(invalid[0]);++i) {
        f=create_lab_fixture();assert(!sigma_bridge_receive(&f->bridge,invalid[i],sizeof(invalid[i])));
        assert(f->calls==0 && f->executions==0 && f->send_calls==0);free(f);
    }
    /* A policy read failure must still close without a successful reply. */
    f=create_lab_fixture();f->fail_call=1;
    assert(!sigma_bridge_receive(&f->bridge,read_zero,sizeof(read_zero)));
    assert(f->bridge.diagnostic.error==SIGMA_BRIDGE_EXECUTION && f->sent_bytes==0);free(f);
    puts("PASS LAB chip00: exact GUI read/reply, splits/mixed IDs/short sends; rejects bad channel/flag/reserved/other chips/strict profiles/IO failure");
}


static void test_lab_zero_chip_register_write(void)
{
    /* Actual GUI download capture: F890=0001 exits active-low soft reset. */
    const uint8_t frame[]={9,0,0,0,0,0,16,0,0,0,0,2,0xf8,0x90,0,1};
    for(size_t split=0;split<=sizeof(frame);++split) {
        fixture_t *f=create_lab_fixture();
        assert(sigma_bridge_receive(&f->bridge,frame,split));
        if(split<sizeof(frame)) assert(f->calls==0 && f->executions==0);
        assert(sigma_bridge_receive(&f->bridge,frame+split,sizeof(frame)-split));
        assert(f->calls==1 && f->executions==1 && f->transactions[0].write);
        assert(f->transactions[0].address==0xf890 && f->transactions[0].bytes==2);
        assert(f->transactions[0].data[0]==0 && f->transactions[0].data[1]==1);
        assert(f->sent_bytes==0 && f->send_calls==0); /* No invented write ACK. */
        assert(sigma_bridge_finish(&f->bridge));free(f);
    }
    for(size_t n=1;n<sizeof(frame);++n) {
        fixture_t *f=create_lab_fixture();
        assert(sigma_bridge_receive(&f->bridge,frame,n));assert(!sigma_bridge_finish(&f->bridge));
        assert(f->calls==0 && f->executions==0 && f->send_calls==0);free(f);
    }
    fixture_t *f=create_fixture();
    assert(!sigma_bridge_receive(&f->bridge,frame,sizeof(frame)) && f->calls==0);free(f);
    /* Mutate chip, channel, flag or lengths; no IO on invalid header. */
    const size_t offsets[]={7,7,2,1,11,6};
    const uint8_t values[]={0xa0,0x38,1,2,1,0};
    for(size_t i=0;i<sizeof(offsets)/sizeof(offsets[0]);++i) {
        uint8_t bad[sizeof(frame)];memcpy(bad,frame,sizeof(frame));bad[offsets[i]]=values[i];
        f=create_lab_fixture();assert(!sigma_bridge_receive(&f->bridge,bad,sizeof(bad)));
        assert(f->calls==0 && f->executions==0 && f->send_calls==0);free(f);
    }
    /* Coalesced write + read; writes generate no response and order is kept. */
    const uint8_t readback[]={10,0,0,0,14,0,0,0,0,2,0xf8,0x90,0,0};
    uint8_t stream[sizeof(frame)+sizeof(readback)];
    memcpy(stream,frame,sizeof(frame));memcpy(stream+sizeof(frame),readback,sizeof(readback));
    f=create_lab_fixture();
    for(size_t i=0;i<sizeof(stream);++i) assert(sigma_bridge_receive(&f->bridge,stream+i,1));
    assert(f->calls==2 && f->transactions[0].write && !f->transactions[1].write);
    assert(f->sent_bytes==16 && f->sent[5]==0 && f->sent[14]==0 && f->sent[15]==1);free(f);
    f=create_lab_fixture();f->fail_call=1;
    assert(!sigma_bridge_receive(&f->bridge,frame,sizeof(frame)));
    assert(f->bridge.diagnostic.error==SIGMA_BRIDGE_EXECUTION && f->sent_bytes==0);free(f);
    puts("PASS LAB chip00 F890 write: exact capture/splits/truncation/order/no ACK; strict/chip/channel/flag/length/IO failures remain closed");
}

int main(void)
{
    test_lab_zero_chip_register_write();
    test_lab_zero_chip_register_read();
    test_lab_prelude();
    test_native_frames();
    test_stream_order();
    test_truncation();
    test_invalid_frames();
    test_explicit_profiles_and_native_prefix();
    test_frame_budget();
    test_chunking();
    test_access_policy();
    test_region_definitions();
    test_io_failures();
    test_safeload();
    test_safeload_failures();
    test_timeouts();
    test_cancel_and_send();
    puts("All 17 offline bridge test groups passed.");
    return 0;
}

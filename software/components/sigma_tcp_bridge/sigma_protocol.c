#include "sigma_protocol.h"

#include <string.h>

static uint32_t read_be32(const uint8_t *bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) | bytes[3];
}

static void write_be32(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)(value >> 24);
    bytes[1] = (uint8_t)(value >> 16);
    bytes[2] = (uint8_t)(value >> 8);
    bytes[3] = (uint8_t)value;
}

static bool supported_profile(sigma_protocol_profile_t profile)
{
    return profile.kind == SIGMA_PROFILE_PLUS_3_4_0_RW ||
           profile.kind == SIGMA_PROFILE_CLASSIC_4_7_RW ||
           profile.kind == SIGMA_PROFILE_PLUS_3_4_0_LAB;
}

bool sigma_decoder_init(sigma_decoder_t *decoder, sigma_protocol_profile_t profile,
                        uint8_t *storage, size_t capacity)
{
    if (decoder == NULL || storage == NULL || capacity < SIGMA_HEADER_BYTES ||
        capacity > SIGMA_MAX_FRAME_BYTES || !supported_profile(profile)) {
        return false;
    }
    *decoder = (sigma_decoder_t){
        .profile = profile, .buffer = storage, .capacity = capacity,
        .expected = SIGMA_HEADER_BYTES,
    };
    return true;
}

/* The 3.4.0 plugin Connect path stores wire chip 00. On the explicit
 * single-DSP LAB binding, 00 and 70 identify the same executor for ALL supported
 * operations. This is not a physical I2C address or a broadcast. Address, width,
 * core-state and Safeload authorization belong to the executor, equally for
 * either wire ID. Strict profiles retain exact chip matching. */
static bool accepted_chip(const sigma_decoder_t *decoder, bool write)
{
    const uint8_t chip = decoder->buffer[write ? 7 : 5];
    return chip == decoder->profile.chip_address ||
           (chip == 0 && decoder->profile.kind == SIGMA_PROFILE_PLUS_3_4_0_LAB &&
            decoder->profile.chip_address == 0x70 && decoder->profile.channel == 0);
}

static bool validate_header(sigma_decoder_t *decoder)
{
    const uint8_t *header = decoder->buffer;
    const bool write = header[0] == SIGMA_CONTROL_WRITE;
    if (!write && header[0] != SIGMA_CONTROL_READ) {
        return false;
    }
    const uint32_t total = read_be32(header + (write ? 3 : 1));
    const uint32_t count = read_be32(header + (write ? 8 : 6));
    if (count == 0 || count > SIGMA_MAX_PAYLOAD_BYTES ||
        count > decoder->capacity - SIGMA_HEADER_BYTES ||
        total != (write ? SIGMA_HEADER_BYTES + count : SIGMA_HEADER_BYTES) ||
        !accepted_chip(decoder, write)) {
        return false;
    }
    if (write) {
        if (header[1] > 1 || header[2] != decoder->profile.channel) {
            return false;
        }
    } else if (header[12] != 0 || header[13] != 0) {
        return false;
    }
    decoder->expected = total;
    return true;
}

sigma_protocol_result_t sigma_decoder_feed(sigma_decoder_t *decoder, const uint8_t *bytes,
                                          size_t length, size_t *consumed,
                                          sigma_request_t *request)
{
    if (consumed != NULL) {
        *consumed = 0;
    }
    if (decoder == NULL || consumed == NULL || request == NULL ||
        (bytes == NULL && length != 0) || decoder->buffer == NULL ||
        decoder->failed || decoder->complete) {
        return SIGMA_PROTOCOL_INVALID;
    }
    while (*consumed < length) {
        /* Reject unsupported controls at the FIRST byte. In particular, the
         * native 0x1b connection prefix is shorter than our 14-byte R/W header;
         * waiting for 14 would absorb bytes from the next request or stall. */
        if (decoder->used == 0 && bytes[*consumed] == 0x1b &&
            decoder->profile.kind == SIGMA_PROFILE_PLUS_3_4_0_LAB) {
            decoder->expected = 11;
        }
        if (decoder->used == 0 && bytes[*consumed] != SIGMA_CONTROL_WRITE &&
            bytes[*consumed] != SIGMA_CONTROL_READ &&
            !(bytes[*consumed] == 0x1b && decoder->profile.kind == SIGMA_PROFILE_PLUS_3_4_0_LAB)) {
            decoder->buffer[0] = bytes[*consumed];
            decoder->used = 1;
            ++*consumed;
            decoder->failed = true;
            return SIGMA_PROTOCOL_INVALID;
        }
        size_t copy = decoder->expected - decoder->used;
        if (copy > length - *consumed) {
            copy = length - *consumed;
        }
        memcpy(decoder->buffer + decoder->used, bytes + *consumed, copy);
        decoder->used += copy;
        *consumed += copy;
        if (decoder->used < decoder->expected) {
            continue;
        }
        if (decoder->expected == 11 && decoder->used == 11) {
            static const uint8_t captured[] = {0x1b,0,0x40,6,0,1,1,1,0,0,1};
            if (memcmp(decoder->buffer, captured, sizeof(captured)) != 0) {
                decoder->failed = true;
                return SIGMA_PROTOCOL_INVALID;
            }
            *request = (sigma_request_t){.control = 0x1b};
            decoder->complete = true;
            return SIGMA_PROTOCOL_LAB_INIT;
        }
        if (decoder->used == SIGMA_HEADER_BYTES && !validate_header(decoder)) {
            decoder->failed = true;
            return SIGMA_PROTOCOL_INVALID;
        }
        if (decoder->used == decoder->expected) {
            const bool write = decoder->buffer[0] == SIGMA_CONTROL_WRITE;
            const size_t address_offset = write ? 12 : 10;
            *request = (sigma_request_t){
                .control = decoder->buffer[0],
                .flag = write ? decoder->buffer[1] : 0,
                .chip_address = decoder->buffer[write ? 7 : 5],
                .address = (uint16_t)(((uint16_t)decoder->buffer[address_offset] << 8) |
                                     decoder->buffer[address_offset + 1]),
                .data_length = read_be32(decoder->buffer + (write ? 8 : 6)),
                .data = write ? decoder->buffer + SIGMA_HEADER_BYTES : NULL,
            };
            decoder->complete = true;
            return SIGMA_PROTOCOL_FRAME;
        }
    }
    return SIGMA_PROTOCOL_MORE;
}

void sigma_decoder_release(sigma_decoder_t *decoder)
{
    if (decoder != NULL && !decoder->failed) {
        decoder->used = 0;
        decoder->expected = SIGMA_HEADER_BYTES;
        decoder->complete = false;
    }
}

bool sigma_decoder_finish(sigma_decoder_t *decoder)
{
    if (decoder == NULL || decoder->failed || decoder->used != 0) {
        if (decoder != NULL) {
            decoder->failed = true;
        }
        return false;
    }
    return true;
}

bool sigma_protocol_read_response(uint8_t *buffer, size_t capacity,
                                  sigma_protocol_profile_t profile,
                                  uint16_t address, uint32_t data_length)
{
    if (!supported_profile(profile) || buffer == NULL || data_length == 0 || data_length > SIGMA_MAX_PAYLOAD_BYTES ||
        capacity < SIGMA_HEADER_BYTES || data_length > capacity - SIGMA_HEADER_BYTES) {
        return false;
    }
    memset(buffer, 0, SIGMA_HEADER_BYTES);
    buffer[0] = SIGMA_CONTROL_READ_RESPONSE;
    write_be32(buffer + 1, SIGMA_HEADER_BYTES + data_length);
    buffer[5] = profile.chip_address;
    write_be32(buffer + 6, data_length);
    buffer[10] = (uint8_t)(address >> 8);
    buffer[11] = (uint8_t)address;
    return true;
}

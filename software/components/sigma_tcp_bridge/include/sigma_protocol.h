#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ADI SigmaStudio+ TCP/IP documented R/W subset. Native 3.4.0 loopback
 * confirms this layout and BE reply addresses, not complete GUI compatibility.
 * Strict profiles reject undocumented controls. The LAB profile recognizes
 * one captured 0x1b prelude as a no-op and chip-00 as an alias for the single
 * DSP on all supported R/W operations, NOT general configuration/address routing. */
#define SIGMA_CONTROL_WRITE 0x09U
#define SIGMA_CONTROL_READ 0x0aU
#define SIGMA_CONTROL_READ_RESPONSE 0x0bU
#define SIGMA_HEADER_BYTES 14U
#define SIGMA_MAX_PAYLOAD_BYTES 65536U
#define SIGMA_MAX_FRAME_BYTES (SIGMA_HEADER_BYTES + SIGMA_MAX_PAYLOAD_BYTES)

typedef enum {
    SIGMA_PROTOCOL_MORE = 0,
    SIGMA_PROTOCOL_FRAME,
    SIGMA_PROTOCOL_LAB_INIT,
    SIGMA_PROTOCOL_INVALID,
} sigma_protocol_result_t;

/* Explicit caller-selected evidence profile, never inferred from incoming data.
 * Both pinned versions share the R/W codec; neither enables arbitrary opcodes. */
typedef enum {
    SIGMA_PROFILE_UNSPECIFIED = 0,
    SIGMA_PROFILE_PLUS_3_4_0_RW,
    SIGMA_PROFILE_CLASSIC_4_7_RW,
    /* Bench-only captured prelude plus chip-00 alias for ALL supported R/W
     * (including RAM and Safeload) when bound to chip 70/channel 0. No EEPROM routing. */
    SIGMA_PROFILE_PLUS_3_4_0_LAB,
} sigma_protocol_kind_t;

typedef struct {
    sigma_protocol_kind_t kind;
    uint8_t chip_address;
    uint8_t channel;
} sigma_protocol_profile_t;

typedef struct {
    uint8_t control;
    uint8_t flag;
    uint8_t chip_address; /* Original wire byte; never an I2C device selector. */
    uint16_t address;
    uint32_t data_length;
    const uint8_t *data;
} sigma_request_t;

typedef struct {
    sigma_protocol_profile_t profile;
    uint8_t *buffer;
    size_t capacity;
    size_t used;
    size_t expected;
    bool failed;
    bool complete;
} sigma_decoder_t;

/* The profile comes from verified project configuration, never auto-detection.
 * Storage belongs to the caller. A completed request is valid until release. */
bool sigma_decoder_init(sigma_decoder_t *decoder, sigma_protocol_profile_t profile,
                        uint8_t *storage, size_t capacity);
sigma_protocol_result_t sigma_decoder_feed(sigma_decoder_t *decoder, const uint8_t *bytes,
                                          size_t length, size_t *consumed,
                                          sigma_request_t *request);
void sigma_decoder_release(sigma_decoder_t *decoder);
bool sigma_decoder_finish(sigma_decoder_t *decoder);

/* Successful responses only; I/O errors terminate the session. Pinned 3.4.0
 * async native callback accepts BE address, rejects reversed address. No guessed
 * error response, write ACK, connection/configuration ACK, or status=1 UI path. */
bool sigma_protocol_read_response(uint8_t *buffer, size_t capacity,
                                  sigma_protocol_profile_t profile,
                                  uint16_t address, uint32_t data_length);

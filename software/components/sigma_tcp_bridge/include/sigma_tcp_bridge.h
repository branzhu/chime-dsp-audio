#pragma once

#include "sigma_protocol.h"

#define SIGMA_FRAME_STALL_US 10000000ULL
#define SIGMA_FRAME_DEADLINE_US 30000000ULL

typedef enum {
    SIGMA_BRIDGE_OK = 0,
    SIGMA_BRIDGE_PROTOCOL,
    SIGMA_BRIDGE_UNSUPPORTED_CONTROL,
    SIGMA_BRIDGE_TRUNCATED,
    SIGMA_BRIDGE_RECEIVE_TIMEOUT,
    SIGMA_BRIDGE_EXECUTION,
    SIGMA_BRIDGE_EXECUTION_TIMEOUT,
    SIGMA_BRIDGE_SEND,
    SIGMA_BRIDGE_SEND_TIMEOUT,
} sigma_bridge_error_t;

typedef struct {
    void *context;
    uint64_t (*now_us)(void *context);
    /* Synchronous, bounded DSP service entry. It must authorize flag/address/
     * length against the admitted session. Flag=1 is NOT automatically treated
     * as a semantic coefficient write: G0 must establish its actual sequence. */
    int (*execute)(void *context, const sigma_request_t *request, uint8_t *read_data);
    /* Return bytes sent (including short writes), or <=0 on error. The adapter
     * must honor timeout_ms; there is no retry/reconnect of failed requests. */
    int (*send)(void *context, const uint8_t *data, size_t bytes, uint32_t timeout_ms);
} sigma_bridge_io_t;

typedef struct {
    sigma_bridge_error_t error;
    int operation_error;
    uint32_t completed_frames;
    uint32_t lab_init_frames;
    uint8_t control;
    uint8_t chip_address;
    uint16_t address;
    uint32_t data_length;
    uint64_t max_execution_us;
    size_t response_bytes_sent;
} sigma_bridge_diagnostic_t;

/* Transport-independent, single admitted connection. One caller owns each
 * instance; socket receive/send and admission belong to the G3 adapter.
 * A single fixed caller buffer holds a full request or successful read reply.
 * Returning false is terminal: close the socket and revoke its DSP lease. */
typedef struct {
    sigma_decoder_t decoder;
    sigma_bridge_io_t io;
    sigma_bridge_diagnostic_t diagnostic;
    uint64_t frame_started_us;
    uint64_t last_progress_us;
    bool closed;
} sigma_bridge_t;

bool sigma_bridge_init(sigma_bridge_t *bridge, sigma_protocol_profile_t profile,
                       sigma_bridge_io_t io, uint8_t *storage, size_t capacity);
bool sigma_bridge_receive(sigma_bridge_t *bridge, const uint8_t *bytes, size_t length);
bool sigma_bridge_poll(sigma_bridge_t *bridge);
bool sigma_bridge_finish(sigma_bridge_t *bridge);

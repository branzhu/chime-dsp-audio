#include "sigma_tcp_bridge.h"

bool sigma_bridge_init(sigma_bridge_t *bridge, sigma_protocol_profile_t profile,
                       sigma_bridge_io_t io, uint8_t *storage, size_t capacity)
{
    if (bridge == NULL) {
        return false;
    }
    *bridge = (sigma_bridge_t){ .io = io, .closed = true };
    if (io.now_us == NULL || io.execute == NULL || io.send == NULL ||
        !sigma_decoder_init(&bridge->decoder, profile, storage, capacity)) {
        return false;
    }
    bridge->closed = false;
    return true;
}

static bool fail(sigma_bridge_t *bridge, sigma_bridge_error_t error, int operation_error)
{
    if (!bridge->closed) {
        bridge->diagnostic.error = error;
        bridge->diagnostic.operation_error = operation_error;
        bridge->closed = true;
    }
    return false;
}

bool sigma_bridge_poll(sigma_bridge_t *bridge)
{
    if (bridge == NULL || bridge->closed) {
        return false;
    }
    if (bridge->decoder.used != 0) {
        const uint64_t now = bridge->io.now_us(bridge->io.context);
        if (now - bridge->frame_started_us >= SIGMA_FRAME_DEADLINE_US ||
            now - bridge->last_progress_us >= SIGMA_FRAME_STALL_US) {
            return fail(bridge, SIGMA_BRIDGE_RECEIVE_TIMEOUT, 0);
        }
    }
    return true;
}

static bool execute_request(sigma_bridge_t *bridge, const sigma_request_t *request)
{
    bridge->diagnostic.control = request->control;
    bridge->diagnostic.chip_address = request->chip_address;
    bridge->diagnostic.address = request->address;
    bridge->diagnostic.data_length = request->data_length;
    bridge->diagnostic.response_bytes_sent = 0;
    const uint64_t started = bridge->io.now_us(bridge->io.context);
    const int result = bridge->io.execute(bridge->io.context, request,
                                         bridge->decoder.buffer + SIGMA_HEADER_BYTES);
    const uint64_t elapsed = bridge->io.now_us(bridge->io.context) - started;
    if (elapsed > bridge->diagnostic.max_execution_us) {
        bridge->diagnostic.max_execution_us = elapsed;
    }
    if (result != 0) {
        return fail(bridge, SIGMA_BRIDGE_EXECUTION, result);
    }
    if (elapsed >= SIGMA_FRAME_DEADLINE_US) {
        return fail(bridge, SIGMA_BRIDGE_EXECUTION_TIMEOUT, 0);
    }
    if (request->control == SIGMA_CONTROL_READ) {
        sigma_protocol_profile_t reply_profile = bridge->decoder.profile;
        reply_profile.chip_address = request->chip_address;
        if (!sigma_protocol_read_response(bridge->decoder.buffer, bridge->decoder.capacity,
                                          reply_profile, request->address,
                                          request->data_length)) {
            return fail(bridge, SIGMA_BRIDGE_PROTOCOL, 0);
        }
        const size_t bytes = SIGMA_HEADER_BYTES + request->data_length;
        const uint64_t send_started = bridge->io.now_us(bridge->io.context);
        while (bridge->diagnostic.response_bytes_sent < bytes) {
            const uint64_t sent_elapsed = bridge->io.now_us(bridge->io.context) - send_started;
            if (sent_elapsed >= SIGMA_FRAME_DEADLINE_US) {
                return fail(bridge, SIGMA_BRIDGE_SEND_TIMEOUT, 0);
            }
            const size_t offset = bridge->diagnostic.response_bytes_sent;
            const uint32_t timeout_ms = (uint32_t)((SIGMA_FRAME_DEADLINE_US - sent_elapsed + 999) / 1000);
            const int sent = bridge->io.send(bridge->io.context, bridge->decoder.buffer + offset,
                                             bytes - offset, timeout_ms);
            if (sent <= 0 || (size_t)sent > bytes - offset) {
                return fail(bridge, SIGMA_BRIDGE_SEND, sent);
            }
            bridge->diagnostic.response_bytes_sent += (size_t)sent;
            if (bridge->io.now_us(bridge->io.context) - send_started >= SIGMA_FRAME_DEADLINE_US) {
                return fail(bridge, SIGMA_BRIDGE_SEND_TIMEOUT, 0);
            }
        }
    }
    ++bridge->diagnostic.completed_frames;
    return true;
}

bool sigma_bridge_receive(sigma_bridge_t *bridge, const uint8_t *bytes, size_t length)
{
    if (!sigma_bridge_poll(bridge)) {
        return false;
    }
    if (bytes == NULL && length != 0) {
        return fail(bridge, SIGMA_BRIDGE_PROTOCOL, 0);
    }
    size_t offset = 0;
    while (offset < length) {
        const uint64_t now = bridge->io.now_us(bridge->io.context);
        if (bridge->decoder.used == 0) {
            bridge->frame_started_us = now;
        }
        size_t consumed = 0;
        sigma_request_t request;
        const sigma_protocol_result_t result = sigma_decoder_feed(&bridge->decoder, bytes + offset,
                                                                   length - offset, &consumed, &request);
        offset += consumed;
        if (consumed != 0) {
            bridge->last_progress_us = now;
        }
        if (result == SIGMA_PROTOCOL_INVALID) {
            if (bridge->decoder.used != 0) {
                bridge->diagnostic.control = bridge->decoder.buffer[0];
                if (bridge->decoder.used >= SIGMA_HEADER_BYTES) {
                    const bool write = bridge->decoder.buffer[0] == SIGMA_CONTROL_WRITE;
                    bridge->diagnostic.chip_address = bridge->decoder.buffer[write ? 7 : 5];
                }
                if (bridge->diagnostic.control != SIGMA_CONTROL_WRITE &&
                    bridge->diagnostic.control != SIGMA_CONTROL_READ) {
                    return fail(bridge, SIGMA_BRIDGE_UNSUPPORTED_CONTROL, 0);
                }
            }
            return fail(bridge, SIGMA_BRIDGE_PROTOCOL, 0);
        }
        if (result == SIGMA_PROTOCOL_LAB_INIT) {
            /* Explicit bench compatibility quirk, NOT a general 0x1b handler.
             * No DSP callback, no ACK, and never discard an unmatched prefix. */
            ++bridge->diagnostic.lab_init_frames;
            sigma_decoder_release(&bridge->decoder);
        }
        if (result == SIGMA_PROTOCOL_FRAME) {
            if (!execute_request(bridge, &request)) {
                return false;
            }
            sigma_decoder_release(&bridge->decoder);
        }
    }
    return true;
}

bool sigma_bridge_finish(sigma_bridge_t *bridge)
{
    if (!sigma_bridge_poll(bridge)) {
        return false;
    }
    if (!sigma_decoder_finish(&bridge->decoder)) {
        return fail(bridge, SIGMA_BRIDGE_TRUNCATED, 0);
    }
    bridge->closed = true;
    return true;
}

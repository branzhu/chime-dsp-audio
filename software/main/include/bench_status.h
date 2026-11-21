#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    char ip[16];
    char hostname[64];
    uint16_t tcp_port;
    bool station_connected;
    bool bridge_connected;
    bool dsp_default_ready;
    bool dsp_external;
    unsigned tuning_armed,tuning_pending;
    int tuning_result;
    bool amplifiers_enabled;
    bool sound_attempted;
    int sound_result;
    int sound_completed_command;
    unsigned output_mode,output_pending;
    int output_result;
    uint16_t sound_failed_reg;
    uint32_t requests;
    int last_result;
    uint32_t remaining_seconds;
    unsigned selected_source,rendering_source;
    bool source_stopped,bt_available,bt_connected,bt_playing;
    unsigned bt_codec,bt_rate,bt_queued; /* codec: 0=none, 1=SBC, 2=AAC, 3=aptX, 4=aptX HD, 5=Opus, 6=LDAC */
    int bt_error,bt_drift_ppm;
    uint32_t bt_overflows,bt_underruns;
    unsigned bt_stream_state,bt_rebuffer_events,bt_rebuffer_resumes,bt_rebuffer_wait_ms;
} bench_status_t;

/* Only IPv4 and a single DNS hostname label are accepted: no HTML/JSON injection.
 * Returns false on invalid inputs/truncation; output is empty on failure. */
bool bench_status_json(const bench_status_t *status, char *output, size_t capacity);

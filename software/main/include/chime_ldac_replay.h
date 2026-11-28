#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef enum {
    CHIME_REPLAY_FAILURE_NONE, CHIME_REPLAY_FAILURE_INPUT_STARVED,
    CHIME_REPLAY_FAILURE_FEEDER_LATE, CHIME_REPLAY_FAILURE_OWNER_REVOKED,
    CHIME_REPLAY_FAILURE_BAD_BATCH, CHIME_REPLAY_FAILURE_MEMORY
} chime_ldac_replay_failure_t;
typedef struct {
    bool active,preloaded,flash_ready;
    uint32_t epoch,uploaded_frames,submitted_frames,packets;
    uint32_t fixture_frames,completed_cycles;
    unsigned rate,duration_s,max_late_us,queued_packets;
    chime_ldac_replay_failure_t failure_reason;
    int ppm,error,fixture_error;
    uint64_t start_us;
} chime_ldac_replay_status_t;
/* Explicit test mode; all APIs called by the single HTTP control task except
 * snapshot. Start wire: rate u32 LE, duration u16 LE (70/610), ppm i16 LE.
 * STOP takes effect before returning; old packets cannot publish PCM. */
esp_err_t chime_ldac_replay_begin(unsigned rate,unsigned duration_s,int ppm);
esp_err_t chime_ldac_replay_push(const uint8_t *body,size_t length);
void chime_ldac_replay_stop(void);
void chime_ldac_replay_snapshot(chime_ldac_replay_status_t *out);

#if CONFIG_CHIME_LDAC_REPLAY_FLASH
esp_err_t chime_ldac_replay_prepare_flash(void);
esp_err_t chime_ldac_replay_begin_flash(unsigned duration_s,int ppm);
#endif

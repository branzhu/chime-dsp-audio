#pragma once
#include "chime_bluetooth.h"
#include "esp_a2dp_api.h"
typedef struct {
    bool (*valid)(uint32_t epoch);
    void (*pcm)(const uint8_t *data, uint32_t bytes, uint32_t epoch);
    void (*fail)(uint32_t epoch, esp_err_t error);
    /* Optional: before FIRST PCM output of a packet, after validation/decode.
     * Submission timestamp includes raw queue wait/open/decode, not air time.
     * Fragmented packets currently use the final fragment submission time. */
    void (*timing)(uint32_t epoch, uint64_t submitted_us);
} chime_bt_decoder_callbacks_t;
typedef struct {
    uint32_t epoch,packets,slow_packets;
    unsigned packet_us,max_packet_us,decode_us,pcm_us,open_us,audio_us;
    /* Submission -> dequeue latency, separate from packet processing time.
     * Queue depths are sampled AFTER dequeue, not live at a PCM gap. */
    unsigned queue_wait_us,max_queue_wait_us,raw_after_dequeue,raw_max_after_dequeue;
} chime_bt_decoder_stats_t;
/* Bounded, atomic snapshot of completed packet WALL times (includes preemption).
 * No access to decoder/queue pointers. Unchanged output on contention/NULL. */
bool chime_bt_decoder_snapshot(chime_bt_decoder_stats_t *out);
esp_err_t chime_bt_decoder_start(const chime_bt_decoder_callbacks_t *callbacks);
/* Always consumes buffer ownership, including rejected/full/stale packets.
 * LDAC submission samples a bounded full-buffer CRC32 (max4096 bytes) plus
 * metadata; the worker compares snapshots only when reporting an input fault.
 * No successful-packet logging, retained payload copy or CRC acceptance gate. */
void chime_bt_decoder_submit(esp_a2d_audio_buff_t *buffer, uint32_t epoch,
                            chime_bt_codec_t codec, unsigned rate, unsigned channels);
/* Call only after A2DP callbacks have quiesced. False retains live resources. */
bool chime_bt_decoder_stop(void);

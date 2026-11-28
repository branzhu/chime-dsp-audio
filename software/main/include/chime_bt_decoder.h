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
    /* Optional nonblocking free INPUT-frame count. Production LDAC checks
     * before each 128-frame decode; single producer means no reservation token
     * is needed. Waits outside audio/BT locks with a 100ms timeout per frame,
     * checked when scheduled (not a hard wall-time response guarantee).
     * NULL retains legacy callers; validity still gates every publish. */
    unsigned (*pcm_capacity)(uint32_t epoch);
    /* Optional bounded compute service, worker context only, never a raw/ISR
     * callback. Runs between LDAC frames and while idle/capacity-waiting.
     * Must not wait for I2S or input, and must validate output generation. */
    void (*service)(uint32_t epoch);
} chime_bt_decoder_callbacks_t;
typedef struct {
    uint32_t epoch,packets,slow_packets;
    unsigned packet_us,max_packet_us,decode_us,pcm_us,open_us,audio_us;
    unsigned max_decode_us,max_pcm_us,open_free_dma_bytes,open_free_rtc_bytes;
    unsigned ldac_fast_reserved_bytes,ldac_fast_used_bytes,ldac_fast_peak_bytes,ldac_fast_failures;
    /* Submission -> dequeue latency, separate from packet processing time.
     * Queue depths are published immediately AFTER dequeue, including the
     * current packet; not a live raw-queue query at a PCM gap. */
    unsigned queue_wait_us,max_queue_wait_us,raw_after_dequeue,raw_max_after_dequeue;
    /* Diagnostic only, including the packet/frame currently in cb.pcm.
     * Same timestamp/CRC is not proof of duplicate: fragments, source silence
     * and CRC collisions exist. Timestamp deltas are raw SDK units. */
    uint32_t ldac_frames,ldac_pcm_frames,ldac_same_input,ldac_ts_same;
    uint32_t ldac_ts_delta,ldac_previous_frames;
    /* Per epoch: last/max packet capacity wait wall time, wait events and
     * timeouts. packet_us includes this wait; decode_us does not. */
    unsigned capacity_wait_us,max_capacity_wait_us;
    uint32_t capacity_waits,capacity_timeouts;
} chime_bt_decoder_stats_t;
/* Bounded atomic snapshot: completed packet WALL times (includes preemption),
 * current dequeue timing and in-flight LDAC progress; see individual fields.
 * No access to decoder/queue pointers. Unchanged output on contention/NULL. */
bool chime_bt_decoder_snapshot(chime_bt_decoder_stats_t *out);
esp_err_t chime_bt_decoder_start(const chime_bt_decoder_callbacks_t *callbacks);
/* Always consumes buffer ownership, including rejected/full/stale packets.
 * LDAC submission samples a bounded full-buffer CRC32 (max4096 bytes) plus
 * metadata; the worker compares stages when reporting an input fault and
 * records diagnostic same-timestamp/CRC observations for consecutive inputs.
 * No successful-packet logging, retained payload copy or CRC acceptance gate. */
void chime_bt_decoder_submit(esp_a2d_audio_buff_t *buffer, uint32_t epoch,
                            chime_bt_codec_t codec, unsigned rate, unsigned channels);
/* Call only after A2DP callbacks have quiesced. False retains live resources. */
bool chime_bt_decoder_stop(void);

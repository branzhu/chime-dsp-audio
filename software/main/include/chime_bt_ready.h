#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Fixed 10.67ms bridge, not a replacement for the input jitter buffer.
 * One CPU1 producer / CPU0 consumer; caller serializes metadata + copy.
 * PCM is immutable between push/pop. Generation pair cancels in-flight FIR
 * across STOP, re-admission and Bluetooth epoch changes. No allocation. */
#define CHIME_BT_READY_BLOCKS 4U
#define CHIME_BT_READY_SAMPLES 256U
#define CHIME_BT_READY_PREFILL 2U
typedef struct {
    int32_t pcm[CHIME_BT_READY_BLOCKS][CHIME_BT_READY_SAMPLES];
    uint32_t epoch,output_id,produced,consumed,underruns;
    unsigned head,count,high_water;
    bool started;
} chime_bt_ready_t;
void chime_bt_ready_reset(chime_bt_ready_t *q,uint32_t epoch,uint32_t output_id);
bool chime_bt_ready_push(chime_bt_ready_t *q,uint32_t epoch,uint32_t output_id,const int32_t *pcm);
bool chime_bt_ready_pop(chime_bt_ready_t *q,uint32_t epoch,uint32_t output_id,int32_t *pcm);

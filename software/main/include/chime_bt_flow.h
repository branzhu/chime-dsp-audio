#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
/* Diagnostics only; caller serializes. Fixed wall-time bins distinguish a
 * production burst from sustained consumption deficit. Never controls audio. */
#define CHIME_BT_FLOW_BINS 16U
#define CHIME_BT_FLOW_BIN_US 20000U
#define CHIME_BT_FLOW_UNSEEN UINT32_MAX
typedef struct {
    uint32_t index,pushed,consumed,packets,queue_end,queue_max;
    bool valid;
} chime_bt_flow_bin_t;
typedef struct {
    uint64_t started_us;uint32_t output_id;
    uint32_t first_pcm_us,first_render_us,prime_queue,prime_target;
    chime_bt_flow_bin_t bins[CHIME_BT_FLOW_BINS];
} chime_bt_flow_t;
static inline void chime_bt_flow_reset(chime_bt_flow_t *f,uint64_t now)
{
    memset(f,0,sizeof(*f));f->started_us=now;
    f->first_pcm_us=f->first_render_us=CHIME_BT_FLOW_UNSEEN;
}
static inline void chime_bt_flow_record(chime_bt_flow_t *f,uint64_t now,
    uint32_t pushed,uint32_t consumed,uint32_t packets,uint32_t queued,uint32_t target)
{
    if(now<f->started_us)return;
    uint64_t elapsed=now-f->started_us;
    uint32_t age=elapsed>=UINT32_MAX?UINT32_MAX-1U:(uint32_t)elapsed;
    if(pushed&&f->first_pcm_us==CHIME_BT_FLOW_UNSEEN)f->first_pcm_us=age;
    if(consumed&&f->first_render_us==CHIME_BT_FLOW_UNSEEN){
        f->first_render_us=age;f->prime_queue=queued+consumed;f->prime_target=target;
    }
    uint32_t index=(uint32_t)(elapsed/CHIME_BT_FLOW_BIN_US);
    chime_bt_flow_bin_t *b=&f->bins[index%CHIME_BT_FLOW_BINS];
    if(!b->valid||b->index!=index){memset(b,0,sizeof(*b));b->valid=true;b->index=index;}
    b->pushed+=pushed;b->consumed+=consumed;b->packets+=packets;b->queue_end=queued;
    if(queued>b->queue_max)b->queue_max=queued;
}

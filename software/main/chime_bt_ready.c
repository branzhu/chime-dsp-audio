#include "chime_bt_ready.h"
#include <string.h>
void chime_bt_ready_reset(chime_bt_ready_t *q,uint32_t epoch,uint32_t output_id)
{
    /* No need to clear 4KiB payload while holding the caller's spinlock. */
    q->epoch=epoch;q->output_id=output_id;q->head=q->count=q->high_water=0;
    q->produced=q->consumed=q->underruns=0;q->started=false;
}
bool chime_bt_ready_push(chime_bt_ready_t *q,uint32_t epoch,uint32_t output_id,const int32_t *pcm)
{
    if(!pcm||!epoch||!output_id||epoch!=q->epoch||output_id!=q->output_id||q->count==CHIME_BT_READY_BLOCKS)return false;
    memcpy(q->pcm[(q->head+q->count)%CHIME_BT_READY_BLOCKS],pcm,sizeof(q->pcm[0]));
    ++q->count;++q->produced;if(q->count>q->high_water)q->high_water=q->count;return true;
}
bool chime_bt_ready_pop(chime_bt_ready_t *q,uint32_t epoch,uint32_t output_id,int32_t *pcm)
{
    if(!pcm)return false;
    memset(pcm,0,sizeof(q->pcm[0]));
    if(!epoch||!output_id||epoch!=q->epoch||output_id!=q->output_id)return false;
    if(!q->started){if(q->count<CHIME_BT_READY_PREFILL)return false;q->started=true;}
    if(!q->count){++q->underruns;q->started=false;return false;}
    memcpy(pcm,q->pcm[q->head],sizeof(q->pcm[0]));q->head=(q->head+1)%CHIME_BT_READY_BLOCKS;
    --q->count;++q->consumed;return true;
}

#include "chime_bt_delay.h"
#include <stddef.h>
void chime_bt_delay_reset(chime_bt_delay_t *d){*d=(chime_bt_delay_t){0};}
void chime_bt_delay_sample(chime_bt_delay_t *d,uint64_t now,unsigned frames,
    unsigned rate,unsigned residence,unsigned fixed)
{
    if(!rate||(d->valid&&now<=d->last_us))return;
    uint64_t queued=((uint64_t)frames*1000000U+rate-1U)/rate;
    uint64_t total=queued+residence+fixed;
    if(total>CHIME_BT_DELAY_MAX_US)return; /* reject, do not invent a capped delay */
    d->queue_us=(unsigned)queued;d->residence_us=residence;++d->samples;
    if(!d->valid||now-d->last_us>CHIME_BT_DELAY_FRESH_US){
        d->filtered_us=(unsigned)total;d->first_us=now;d->valid=true;
    }else{
        uint64_t dt=now-d->last_us;
        int64_t delta=(int64_t)total-d->filtered_us;
        d->filtered_us=(unsigned)((int64_t)d->filtered_us+delta*(int64_t)dt/(int64_t)(1000000ULL+dt));
    }
    d->last_us=now;
}
bool chime_bt_delay_candidate(const chime_bt_delay_t *d,uint64_t now,uint16_t *value)
{
    if(!value||!d->valid||now<d->last_us||now-d->last_us>CHIME_BT_DELAY_FRESH_US||
       now-d->first_us<CHIME_BT_DELAY_WARM_US||
       (d->attempted&&(now<d->attempt_us||now-d->attempt_us<CHIME_BT_DELAY_REPORT_US)))return false;
    unsigned v=(d->filtered_us+99U)/100U;
    if(!v)v=1;
    unsigned diff=v>d->accepted_100us?v-d->accepted_100us:d->accepted_100us-v;
    if(d->accepted_100us&&diff<CHIME_BT_DELAY_CHANGE_100US)return false;
    *value=(uint16_t)v;return true;
}

#include "chime_source.h"
void chime_source_init(chime_source_t *s)
{
    *s=(chime_source_t){.selected=CHIME_SOURCE_USB,.generation=1};
}
void chime_source_update(chime_source_t *s,bool connected,uint32_t usb,uint32_t bt)
{
    chime_source_id_t next=connected?CHIME_SOURCE_BLUETOOTH:CHIME_SOURCE_USB;
    if(next!=s->selected||(next==CHIME_SOURCE_USB?usb!=s->usb_epoch:bt!=s->bt_epoch)){
        ++s->generation;
        if(!s->generation){s->generation=1;s->attempted=0;}
    }
    s->selected=next;s->usb_epoch=usb;s->bt_epoch=bt;
}
void chime_source_stop(chime_source_t *s){s->stopped=true;}
void chime_source_resume(chime_source_t *s)
{
    s->stopped=false;chime_source_retry(s);
}
void chime_source_retry(chime_source_t *s)
{
    ++s->generation;
    if(!s->generation){s->generation=1;s->attempted=0;}
}
bool chime_source_admit(chime_source_t *s,bool allowed,bool wanted)
{
    if(!allowed||!wanted||s->stopped||s->attempted==s->generation)return false;
    s->attempted=s->generation;return true;
}
const char *chime_source_name(chime_source_id_t source)
{
    return source==CHIME_SOURCE_USB?"usb":source==CHIME_SOURCE_BLUETOOTH?"bluetooth":"none";
}

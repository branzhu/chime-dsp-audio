#include "chime_hid.h"
void chime_hid_reset(chime_hid_t *s){s->pending=0;s->release=true;}
void chime_hid_adjust(chime_hid_t *s,int steps)
{
    if(steps>32)steps=32;
    if(steps< -32)steps= -32;
    s->pending+=steps;
    if(s->pending>32)s->pending=32;
    if(s->pending< -32)s->pending= -32;
}
bool chime_hid_peek(const chime_hid_t *s,uint16_t *usage)
{
    if(!s->release&&!s->pending)return false;
    *usage=s->release?0:(s->pending>0?0xe9:0xea);return true;
}
void chime_hid_sent(chime_hid_t *s)
{
    if(s->release)s->release=false;
    else {s->pending+=s->pending>0?-1:1;s->release=true;}
}

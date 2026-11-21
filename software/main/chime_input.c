#include "chime_input.h"
void chime_rotary_init(chime_rotary_t *s,bool a,bool b)
{
    *s=(chime_rotary_t){.previous=((unsigned)a<<1)|(unsigned)b};
}
int chime_rotary_sample(chime_rotary_t *s,bool a,bool b)
{
    static const int8_t transitions[16]={0,-1,1,0,1,0,0,-1,-1,0,0,1,0,1,-1,0};
    unsigned next=((unsigned)a<<1)|(unsigned)b,old=s->previous;
    s->previous=next;
    if((old^next)==3U){s->quarter_steps=0;return 0;}
    s->quarter_steps+=transitions[old*4U+next];
    /* Emit only on a complete electrical cycle, not on a bouncing edge. */
    if(s->quarter_steps>=4){s->quarter_steps=0;return 1;}
    if(s->quarter_steps<=-4){s->quarter_steps=0;return -1;}
    return 0;
}
void chime_button_init(chime_button_t *s,bool pressed,uint64_t now_ms)
{
    *s=(chime_button_t){.raw=pressed,.stable=pressed,.armed=!pressed,.changed_ms=now_ms};
}
chime_input_event_t chime_button_sample(chime_button_t *s,bool pressed,uint64_t now_ms)
{
    if(pressed!=s->raw){s->raw=pressed;s->changed_ms=now_ms;}
    if(s->raw!=s->stable&&now_ms-s->changed_ms>=20U){
        s->stable=s->raw;
        if(s->stable){s->pressed_ms=now_ms;s->held=false;}
        else {
            bool click=s->armed&&!s->held;
            s->armed=true;s->held=false;
            return click?CHIME_INPUT_CLICK:CHIME_INPUT_NONE;
        }
    }
    /* A key held at boot must be released before it can produce any action.
     * Once raw release begins, do not turn a short press into a hold. */
    if(s->armed&&s->stable&&s->raw&&!s->held&&now_ms-s->pressed_ms>=1500U){
        s->held=true;return CHIME_INPUT_HOLD;
    }
    return CHIME_INPUT_NONE;
}
unsigned chime_input_action(unsigned flags,chime_input_event_t event)
{
    if(event==CHIME_INPUT_HOLD)return flags^CHIME_CONTROL_STANDBY;
    if(event==CHIME_INPUT_CLICK&&!(flags&CHIME_CONTROL_STANDBY))return flags^CHIME_CONTROL_MUTED;
    return flags;
}

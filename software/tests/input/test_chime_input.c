#include "chime_input.h"
#include <assert.h>
#include <stdio.h>
static int sample(chime_rotary_t *s,unsigned state){return chime_rotary_sample(s,(state&2U)!=0,(state&1U)!=0);}
int main(void)
{
    chime_rotary_t r;chime_rotary_init(&r,true,true);
    assert(!sample(&r,1));assert(!sample(&r,3)); /* bounce */
    assert(!sample(&r,1));assert(!sample(&r,0));assert(!sample(&r,2));assert(sample(&r,3)==1);
    assert(!sample(&r,2));assert(!sample(&r,0));assert(!sample(&r,1));assert(sample(&r,3)==-1);
    assert(!sample(&r,0)); /* invalid two-bit jump clears partial cycle */
    assert(!sample(&r,3));assert(!r.quarter_steps);
    for(unsigned start=0;start<4;++start){
        const unsigned seq[]={3,1,0,2};
        chime_rotary_init(&r,(seq[start]&2U)!=0,(seq[start]&1U)!=0);
        for(unsigned n=1;n<=4;++n)assert(sample(&r,seq[(start+n)%4])==(n==4?1:0));
    }
    chime_button_t b;chime_button_init(&b,false,0);
    assert(chime_button_sample(&b,true,1)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,false,9)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,true,12)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,true,32)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,false,80)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,false,100)==CHIME_INPUT_CLICK); /* no double-click wait */
    assert(chime_button_sample(&b,true,120)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,true,140)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,true,1639)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,true,1640)==CHIME_INPUT_HOLD);
    assert(chime_button_sample(&b,true,4000)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,false,4001)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,false,4021)==CHIME_INPUT_NONE); /* hold never emits click */
    chime_button_init(&b,true,0);
    assert(chime_button_sample(&b,true,5000)==CHIME_INPUT_NONE); /* boot-held suppression */
    assert(chime_button_sample(&b,false,5001)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,false,5021)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,true,5030)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,true,5050)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,false,5060)==CHIME_INPUT_NONE);
    assert(chime_button_sample(&b,false,5080)==CHIME_INPUT_CLICK);
    assert(chime_input_action(0,CHIME_INPUT_HOLD)==CHIME_CONTROL_STANDBY);
    assert(chime_input_action(CHIME_CONTROL_STANDBY,CHIME_INPUT_HOLD)==0);
    unsigned flags=chime_input_action(0,CHIME_INPUT_CLICK);assert(flags==CHIME_CONTROL_MUTED);
    flags=chime_input_action(flags,CHIME_INPUT_HOLD);assert(flags==(CHIME_CONTROL_MUTED|CHIME_CONTROL_STANDBY));
    assert(chime_input_action(flags,CHIME_INPUT_CLICK)==flags); /* standby click does not wake */
    flags=chime_input_action(flags,CHIME_INPUT_HOLD);assert(flags==CHIME_CONTROL_MUTED);
    assert(chime_input_action(flags,CHIME_INPUT_CLICK)==0);
    puts("PASS quadrature directions/bounce/reversal/invalid edges; button debounce/click/hold/boot-held; standby preserves mute");
    return 0;
}

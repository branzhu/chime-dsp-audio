#include "chime_led.h"
#include <assert.h>
#include <stdio.h>
static void rgb(chime_led_rgb_t v,unsigned r,unsigned g,unsigned b){assert(v.r==r&&v.g==g&&v.b==b);}
int main(void)
{
    chime_led_t led={0};chime_led_input_t s={0};
    rgb(chime_led_render(&led,s,0),24,24,24);
    s.bluetooth=true;rgb(chime_led_render(&led,s,100),0,0,24);
    s.muted=true;rgb(chime_led_render(&led,s,200),0,0,0);
    rgb(chime_led_render(&led,s,360),0,0,24);
    s.bluetooth=false;rgb(chime_led_render(&led,s,361),24,24,24);
    rgb(chime_led_render(&led,s,2600),0,0,0);
    s.stopped=true;s.muted=false;assert(chime_led_mode(s)==CHIME_LED_MUTED);
    s.pairing=true;assert(chime_led_mode(s)==CHIME_LED_PAIRING);
    rgb(chime_led_render(&led,s,3000),0,0,24);rgb(chime_led_render(&led,s,3400),0,0,0);
    s.setup=true;assert(chime_led_mode(s)==CHIME_LED_SETUP);
    rgb(chime_led_render(&led,s,4000),24,24,0);rgb(chime_led_render(&led,s,4800),0,0,0);
    s.starting=true;assert(chime_led_mode(s)==CHIME_LED_STARTING);
    rgb(chime_led_render(&led,s,5000),3,3,3);rgb(chime_led_render(&led,s,6200),32,32,32);
    rgb(chime_led_render(&led,s,7400),3,3,3);
    s.standby=true;rgb(chime_led_render(&led,s,7500),0,0,0);
    s.fault=true;rgb(chime_led_render(&led,s,7600),48,0,0);
    rgb(chime_led_render(&led,s,8100),0,0,0);rgb(chime_led_render(&led,s,8600),48,0,0);
    /* Exhaustively check all competing flags; connection/overlay events cannot
     * override fault or standby, and all nominal output is low duty. */
    for(unsigned mask=0;mask<256;++mask){
        s=(chime_led_input_t){.fault=(mask&1)!=0,.standby=(mask&2)!=0,.starting=(mask&4)!=0,
            .setup=(mask&8)!=0,.pairing=(mask&16)!=0,.stopped=(mask&32)!=0,
            .muted=(mask&64)!=0,.bluetooth=(mask&128)!=0};
        chime_led_mode_t expected=s.fault?CHIME_LED_FAULT:s.standby?CHIME_LED_STANDBY:
            s.starting?CHIME_LED_STARTING:s.setup?CHIME_LED_SETUP:s.pairing?CHIME_LED_PAIRING:
            (s.stopped||s.muted)?CHIME_LED_MUTED:s.bluetooth?CHIME_LED_BLUETOOTH:CHIME_LED_USB;
        assert(chime_led_mode(s)==expected);
        chime_led_rgb_t v=chime_led_render(&led,s,9000+mask);assert(v.r<=48&&v.g<=32&&v.b<=32);
    }
    s=(chime_led_input_t){.setup=true,.bluetooth=true};chime_led_render(&led,s,10000);
    s.bluetooth=false;s.setup=false;rgb(chime_led_render(&led,s,10001),24,24,24);
    s.bluetooth=true;rgb(chime_led_render(&led,s,10002),0,0,24); /* paused BT remains BT */
    /* 64-bit uptime, not the 49-day millisecond wrap. */
    s.fault=true;rgb(chime_led_render(&led,s,UINT64_C(4294967290)),48,0,0);
    rgb(chime_led_render(&led,s,UINT64_C(4294967790)),0,0,0);
    assert(chime_led_setup_requested(true,false,false));
    assert(chime_led_setup_requested(false,true,false));
    assert(!chime_led_setup_requested(false,false,false)); /* reconnect/fallback AP */
    assert(!chime_led_setup_requested(true,true,true)); /* DHCP + committed credentials */
    puts("PASS LED policy, all 256 priorities, animation timing, live-source restoration, provisioning intent");
}

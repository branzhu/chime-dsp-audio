#include "chime_led.h"

chime_led_mode_t chime_led_mode(chime_led_input_t s)
{
    if(s.fault)return CHIME_LED_FAULT;
    if(s.standby)return CHIME_LED_STANDBY;
    if(s.starting)return CHIME_LED_STARTING;
    if(s.setup)return CHIME_LED_SETUP;
    if(s.pairing)return CHIME_LED_PAIRING;
    if(s.stopped||s.muted)return CHIME_LED_MUTED;
    return s.bluetooth?CHIME_LED_BLUETOOTH:CHIME_LED_USB;
}
bool chime_led_setup_requested(bool first_use,bool submitted,bool succeeded)
{
    return !succeeded&&(first_use||submitted);
}
chime_led_rgb_t chime_led_render(chime_led_t *led,chime_led_input_t s,uint64_t now_ms)
{
    chime_led_mode_t mode=chime_led_mode(s);
    if(!led->initialized||led->mode!=mode){led->initialized=true;led->mode=mode;led->since_ms=now_ms;}
    uint64_t elapsed=now_ms-led->since_ms;
    /* Logical 8-bit duty. Deliberately dim defaults, to be tuned on the board. */
    uint8_t level=24;
    switch(mode){
    case CHIME_LED_FAULT: return (chime_led_rgb_t){.r=elapsed%1000U<500U?48:0};
    case CHIME_LED_STANDBY: return (chime_led_rgb_t){0};
    case CHIME_LED_STARTING: {
        unsigned phase=(unsigned)(elapsed%2400U);
        unsigned triangle=phase<1200U?phase:2400U-phase;
        /* Quadratic brightness envelope avoids a harsh linear ramp. */
        level=(uint8_t)(3U+29U*triangle*triangle/1440000U);
        return (chime_led_rgb_t){level,level,level};
    }
    case CHIME_LED_SETUP: return (chime_led_rgb_t){.r=elapsed%1600U<800U?24:0,.g=elapsed%1600U<800U?24:0};
    case CHIME_LED_PAIRING: return (chime_led_rgb_t){.b=elapsed%800U<400U?24:0};
    case CHIME_LED_MUTED: if(elapsed%2400U<160U)level=0;break;
    default: break;
    }
    /* Resolve from CURRENT source on every frame, including under an overlay. */
    return s.bluetooth?(chime_led_rgb_t){.b=level}:(chime_led_rgb_t){level,level,level};
}

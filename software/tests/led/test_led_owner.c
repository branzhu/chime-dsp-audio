#include "chime_indicator.h"
#include "chime_source.h"
#include "bench_sound.h"
#include <assert.h>
#include <stdio.h>
typedef struct {bool external;} bench_session_t;
static chime_indicator_state_t published;
void chime_indicator_publish(chime_indicator_state_t s){published=s;}
#include "led_owner.inc"
int main(void){
    bool fault=false;bench_sound_t sound={0};bench_session_t session={0};
    chime_source_t source={.selected=CHIME_SOURCE_BLUETOOTH};
    indicator_publish(&fault,true,&sound,&session,&source);assert(!fault&&published.bluetooth&&!published.stopped&&!published.starting);
    source.stopped=true;indicator_publish(&fault,true,&sound,&session,&source);assert(published.stopped&&!fault);
    sound.attempted=true;sound.result=ESP_ERR_INVALID_STATE;indicator_publish(&fault,true,&sound,&session,&source);assert(!fault); /* deliberate cancel */
    sound.failed_reg=0x70;indicator_publish(&fault,true,&sound,&session,&source);assert(fault&&published.fault);
    sound.result=ESP_OK;indicator_publish(&fault,true,&sound,&session,&source);assert(fault); /* STOP cannot hide fault */
    sound.active=true;indicator_publish(&fault,true,&sound,&session,&source);assert(!fault&&!published.stopped);
    sound.active=false;source.stopped=false;sound.failed_reg=0;sound.result=ESP_FAIL;
    indicator_publish(&fault,true,&sound,&session,&source);assert(fault); /* PCM failure */
    sound=(bench_sound_t){0};fault=false;indicator_publish(&fault,false,&sound,&session,&source);assert(fault); /* DSP boot failure */
    fault=false;session.external=true;source.stopped=true;
    indicator_publish(&fault,false,&sound,&session,&source);assert(!fault&&!published.bluetooth&&published.stopped); /* SS != boot fault, USB only */
    puts("PASS executed owner mapping: selected/paused BT, STOP, guard/PCM fault latch, recovery and external USB");
}

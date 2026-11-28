#include "chime_bt_src_internal.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
void chime_usb_test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
static bool no_memory,slow;
bool chime_src_test_bad_dot;
static unsigned ticks,timer_calls;
static int64_t now;
void *heap_caps_aligned_calloc(size_t alignment,size_t n,size_t size,uint32_t caps)
{
    assert(alignment==16&&caps==3);
    if(no_memory)return NULL;
    void *p=calloc(n,size);assert(p&&((uintptr_t)p&15U)==0);return p;
}
void vTaskDelay(uint32_t n){assert(n==1);ticks+=n;}
/* Scripted measurements exercise policy; these are NOT performance numbers. */
int64_t esp_timer_get_time(void)
{
    unsigned call=timer_calls++,pair=call/2,sample=pair/2;
    bool pie=(pair%2)==((sample&1U)?0U:1U);
    if(call&1U)now+=pie?(slow?3000:1000):2000;
    return now;
}
int main(void)
{
    no_memory=true;chime_bt_src_accel_init();assert(!chime_bt_src_pie_enabled());
    no_memory=false;chime_src_test_bad_dot=true;
    chime_bt_src_accel_init();assert(!chime_bt_src_pie_enabled());assert(timer_calls==0);
    chime_src_test_bad_dot=false;slow=true;
    chime_bt_src_accel_init();assert(!chime_bt_src_pie_enabled());assert(timer_calls==28);
    timer_calls=0;slow=false;
    chime_bt_src_accel_init();assert(chime_bt_src_pie_enabled());assert(timer_calls==28&&ticks>0);
    puts("PASS SRC acceleration gate: no-memory/bad-raw-dot/slow candidate stay scalar; exact faster candidate enables; host policy test NOT hardware timing");
    return 0;
}

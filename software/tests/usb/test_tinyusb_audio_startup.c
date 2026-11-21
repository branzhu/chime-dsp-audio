/* Real driver function bodies are extracted at test time, not reimplemented.
 * Surrounding types/API mocks isolate FIFO feedback and OUT rearming only. */
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define CFG_TUD_EDPT_DEDICATED_HWFIFO 0
#define CFG_TUD_AUDIO_ENABLE_FEEDBACK_EP 1
#define AUDIO_FEEDBACK_METHOD_FREQUENCY_FIXED 1
#define AUDIO_FEEDBACK_METHOD_FREQUENCY_FLOAT 2
#define AUDIO_FEEDBACK_METHOD_FREQUENCY_POWER_OF_2 3
#define AUDIO_FEEDBACK_METHOD_FIFO_COUNT 4
#define TUSB_SPEED_FULL 1
#define TUSB_SPEED_HIGH 2
#define TU_VERIFY(c) do {if(!(c))return false;}while(0)
#define TU_LOG1(...) ((void)0)
#define TU_BREAKPOINT() assert(0)
typedef struct {uint16_t depth,count;} tu_fifo_t;
typedef struct {uint8_t method;uint32_t sample_freq;union {struct {uint32_t mclk_freq;} frequency;struct {uint16_t fifo_threshold;} fifo_count;};} audio_feedback_params_t;
static uint8_t speed;static uint16_t packet_size;static unsigned rearms,callbacks,zeros;static bool rearm_ok=true;
static uint8_t tud_speed_get(void){return speed;}
static uint16_t tu_fifo_depth(tu_fifo_t *f){return f->depth;}
static uint16_t tu_fifo_count(tu_fifo_t *f){return f->count;}
static uint16_t tu_fifo_write_n(tu_fifo_t *f,const void *b,uint16_t n){assert(b&&f->count+n<=f->depth);f->count+=n;return n;}
static bool tu_is_power_of_two(uint32_t n){return n&&(n&(n-1))==0;}
static uint8_t tu_log2(uint32_t n){uint8_t v=0;while(n>>=1)++v;return v;}
static void tud_audio_feedback_params_cb(uint8_t f,uint8_t a,audio_feedback_params_t *p){assert(f==0&&a==1);p->method=AUDIO_FEEDBACK_METHOD_FIFO_COUNT;p->sample_freq=48000;}
static bool usbd_edpt_xfer(uint8_t p,uint8_t ep,uint8_t *b,uint16_t n,bool isr){assert(p==0&&ep==1&&b&&n==packet_size&&isr);++rearms;return rearm_ok;}
static bool tud_audio_rx_done_isr(uint8_t p,uint16_t n,uint8_t f,uint8_t ep,uint8_t a){assert(p==0&&f==0&&ep==1&&a==1);++callbacks;if(!n)++zeros;return true;}
#include "driver-under-test.inc"
int main(void)
{
    for(unsigned hs=0;hs<2;++hs)for(unsigned round=0;round<3;++round){
        speed=hs?TUSB_SPEED_HIGH:TUSB_SPEED_FULL;memset(_audiod_fct,0,sizeof(_audiod_fct));
        audiod_function_t *a=&_audiod_fct[0];a->ep_fb=0x81;a->ep_out=1;a->ep_out_alt=1;a->ep_out_sz=packet_size=hs?28:196;a->ep_out_ff.depth=4096;
        assert(audiod_fb_params_prepare(0,1));assert(a->feedback.value==((hs?6u:48u)<<16));
        rearms=callbacks=zeros=0;
        for(unsigned i=0;i<4;++i)assert(audiod_rx_xfer_isr(0,a,0));
        assert(rearms==4&&callbacks==4&&zeros==4&&a->ep_out_ff.count==0);
        unsigned packets=hs?48:6;
        for(unsigned i=0;i<packets;++i)assert(audiod_rx_xfer_isr(0,a,hs?24:192));
        assert(rearms==4+packets&&callbacks==4+packets&&a->ep_out_ff.count==1152);
        assert(audiod_rx_xfer_isr(0,a,packet_size));
        assert(a->ep_out_ff.count==1152+packet_size);
        assert(a->feedback.value>=a->feedback.min_value&&a->feedback.value<=a->feedback.max_value);
        assert(audiod_rx_xfer_isr(0,a,0)&&rearms==6+packets&&zeros==5);
        rearm_ok=false;assert(!audiod_rx_xfer_isr(0,a,0)&&rearms==7+packets&&callbacks==6+packets);rearm_ok=true;
    }
    puts("PASS extracted TinyUSB driver FS/HS: seed 48/6 in 16.16, ZLP rearm/callback before and after data, FS192/196B and HS24/28B packets, 1152-byte FIFO fill, rearm error path x3");
    return 0;
}

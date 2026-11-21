#include "chime_usb.h"
#include "chime_stream.h"
#include "chime_bluetooth.h"
#include "usb_test_types.h"
#include "esp_private/usb_phy.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>
#include <stdarg.h>
static void (*task_fn)(void *);static tinyusb_config_t cfg;static unsigned installs,uninstalls,clears,writes;
/* Inject a ready high-priority PCM worker immediately before the low-
 * priority owner takes output_lock. It must reach a blocking idle delay,
 * not make another I2S submission and immediately re-take the mutex. */
static jmp_buf stop_handoff;
static bool preempt_stop_owner,in_stop_handoff;
static unsigned stop_handoffs;
static jmp_buf pump_return;static bool pump_once;static int pcm_error,uninstall_error;static bool malformed;
static uint16_t available=4096;
static unsigned lock_depth,yield_at_write,flow_reports;
static char flow_log[512];static bool host_signal;static unsigned critical_depth;
void usb_test_enter_critical(portMUX_TYPE *mux){assert(*mux==0&&critical_depth==0);*mux=1;++critical_depth;}
void usb_test_exit_critical(portMUX_TYPE *mux){assert(*mux==1&&critical_depth==1);*mux=0;--critical_depth;}
void chime_usb_test_log(const char *tag,const char *format,...)
{
    (void)tag;if(strncmp(format,"PCM FLOW ",9))return;
    assert(lock_depth==0&&critical_depth==0); /* Logging holds neither lock. */
    ++flow_reports;va_list args;va_start(args,format);
    vsnprintf(flow_log,sizeof(flow_log),format,args);va_end(args);
}
static bool bt_wanted,bt_rendering;
static uint32_t bt_epoch=1;
static unsigned bt_reads,bt_ends;
bool chime_bluetooth_wanted(void){return bt_wanted;}
uint32_t chime_bluetooth_epoch(void){return bt_epoch;}
int chime_bluetooth_volume(void){return -30*256;}
void chime_bluetooth_fail(esp_err_t error){assert(error);bt_wanted=false;++bt_epoch;}
esp_err_t chime_bluetooth_output_begin(void){assert(lock_depth==1);bt_rendering=true;return 0;}
void chime_bluetooth_output_end(void){assert(lock_depth==1);bt_rendering=false;++bt_ends;}
esp_err_t chime_bluetooth_render(uint8_t *pcm,size_t frames,bool *ready){
    assert(lock_depth==1&&bt_rendering&&frames==128);memset(pcm,0,frames*4U);*ready=true;++bt_reads;return 0;
}
static tusb_speed_t speed=TUSB_SPEED_HIGH;
static unsigned phy_creates,phy_deletes;static bool phy_live;
static int fail_stage;static uint8_t reply[64];static unsigned reply_size;static int64_t clock_us;
const char *esp_err_to_name(esp_err_t e){(void)e;return "test";}
esp_err_t gpio_install_isr_service(int flags){assert(flags==0);return fail_stage==2?-99:0;}
esp_err_t esp_efuse_mac_get_default(uint8_t *mac){memset(mac,0x11,6);return fail_stage==1?-99:0;}
esp_err_t usb_new_phy(const usb_phy_config_t *c,usb_phy_handle_t *h)
{
    assert(!phy_live&&c->controller==USB_PHY_CTRL_OTG&&c->target==USB_PHY_TARGET_UTMI);
    assert(c->otg_mode==USB_OTG_MODE_DEVICE&&c->otg_speed==USB_PHY_SPEED_HIGH);
    /* Regression: non-NULL otg_io_conf causes the real S31 driver assertion. */
    assert(c->otg_io_conf==NULL&&c->ext_io_conf==NULL);
    if(fail_stage==5)return -99;
    ++phy_creates;phy_live=true;*h=&phy_live;return 0;
}
esp_err_t usb_del_phy(usb_phy_handle_t h){assert(phy_live&&h==&phy_live);++phy_deletes;phy_live=false;return 0;}
esp_err_t tinyusb_driver_install(const tinyusb_config_t *c){++installs;cfg=*c;assert(phy_live&&c->phy.skip_setup&&c->phy.self_powered&&c->phy.vbus_monitor_io==8&&c->descriptor.qualifier&&c->descriptor.qualifier->bDeviceClass==0xef);return fail_stage==3?-99:0;}
esp_err_t tinyusb_driver_uninstall(void){assert(phy_live);++uninstalls;return uninstall_error;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return &cfg;}
void vSemaphoreDelete(SemaphoreHandle_t h){assert(h==&cfg);}
int xSemaphoreTake(SemaphoreHandle_t h,uint32_t t){
    assert(h==&cfg&&t==portMAX_DELAY&&lock_depth==0);
    if(preempt_stop_owner&&!in_stop_handoff){
        preempt_stop_owner=false;in_stop_handoff=true;
        if(setjmp(stop_handoff)==0)task_fn(NULL);
        in_stop_handoff=false;++stop_handoffs;assert(lock_depth==0);
    }
    ++lock_depth;return 1;
}
int xSemaphoreGive(SemaphoreHandle_t h){assert(h==&cfg&&lock_depth==1);--lock_depth;
    if(yield_at_write&&writes>=yield_at_write){yield_at_write=0;longjmp(pump_return,1);}return 1;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *out)
{(void)name;(void)arg;assert(stack==4096&&priority==6);task_fn=fn;*out=&cfg;return fail_stage==4?0:pdPASS;}
void vTaskDelay(uint32_t t){clock_us+=t*1000;
    if(in_stop_handoff){assert(t==1&&lock_depth==0);longjmp(stop_handoff,1);}
    if(pump_once)longjmp(pump_return,1);task_fn(NULL);
}
void vTaskDelete(void *t){assert(!t);}
int64_t esp_timer_get_time(void){return clock_us;}
bool tud_audio_buffer_and_schedule_control_xfer(uint8_t p,const tusb_control_request_t *r,void *b,uint16_t n)
{(void)r;assert(p==0&&n<=64);memcpy(reply,b,n);reply_size=n;return true;}
uint16_t tud_audio_read(void *b,uint16_t n){memset(b,0,n);
    if(host_signal){uint8_t *p=b;for(unsigned i=0;i+3<n;i+=4){p[i+1]=64;p[i+3]=192;}}
    return malformed?3:n;}
uint16_t tud_audio_available(void){return available;}
tusb_speed_t tud_speed_get(void){return speed;}
bool tud_audio_clear_ep_out_ff(void){++clears;return true;}
esp_err_t bench_audio_pcm_write(const int32_t *b,size_t n){assert(!in_stop_handoff);assert(b&&n==128);++writes;return pcm_error;}
extern bool tud_audio_set_req_entity_cb(uint8_t,const tusb_control_request_t *,uint8_t *);
extern bool tud_audio_get_req_entity_cb(uint8_t,const tusb_control_request_t *);
extern bool tud_audio_set_itf_cb(uint8_t,const tusb_control_request_t *);
extern bool tud_audio_set_itf_close_ep_cb(uint8_t,const tusb_control_request_t *);
extern void tud_audio_feedback_params_cb(uint8_t,uint8_t,audio_feedback_params_t *);
extern void tud_suspend_cb(bool);
extern void tud_resume_cb(void);
static tusb_control_request_t req(unsigned entity,unsigned ctrl,unsigned ch,unsigned request,unsigned len)
{return (tusb_control_request_t){.bRequest=(uint8_t)request,.wValue=(uint16_t)((ctrl<<8)|ch),.wIndex=(uint16_t)(entity<<8),.wLength=(uint16_t)len};}
int main(void)
{
    for(fail_stage=1;fail_stage<=5;++fail_stage)assert(chime_usb_start()!=0&&!chime_usb_wanted()&&!phy_live&&phy_creates==phy_deletes);
    fail_stage=0;assert(chime_usb_start()==0&&!chime_usb_wanted());assert(chime_usb_start()==ESP_ERR_INVALID_STATE);
    assert(cfg.descriptor.string_count==5&&!strcmp(cfg.descriptor.string[2],"Chime USB Audio"));
    assert(!strcmp(cfg.descriptor.string[3],"111111111111"));
    tusb_control_request_t itf={.wIndex=1,.wValue=1};assert(tud_audio_set_itf_cb(0,&itf)&&chime_usb_wanted());
    uint32_t epoch=chime_usb_epoch();itf.wIndex=2;assert(!tud_audio_set_itf_cb(0,&itf));itf.wIndex=1;
    assert(chime_usb_output_begin(0)==0&&clears==1);chime_usb_output_end();assert(writes==0);
    tusb_control_request_t r=req(4,1,0,1,4);uint8_t rate[]={0x80,0xbb,0,0};
    assert(tud_audio_set_req_entity_cb(0,&r,rate));rate[0]=0;assert(!tud_audio_set_req_entity_cb(0,&r,rate));
    assert(tud_audio_get_req_entity_cb(0,&r)&&reply_size==4&&tu_unaligned_read32(reply)==48000);
    r.bRequest=2;assert(tud_audio_get_req_entity_cb(0,&r)&&reply_size==14&&tu_unaligned_read16(reply)==1);
    r=req(2,2,0,1,2);uint8_t vol[]={0,0xec};assert(tud_audio_set_req_entity_cb(0,&r,vol));
    assert(tud_audio_get_req_entity_cb(0,&r)&&reply_size==2&&(int16_t)tu_unaligned_read16(reply)==-20*256);
    assert(chime_usb_adjust_volume(1)==ESP_OK);
    assert(tud_audio_get_req_entity_cb(0,&r)&&(int16_t)tu_unaligned_read16(reply)==-19*256);
    assert(chime_usb_adjust_volume(-1000)==ESP_OK);
    assert(tud_audio_get_req_entity_cb(0,&r)&&(int16_t)tu_unaligned_read16(reply)==-60*256);
    assert(chime_usb_adjust_volume(1000)==ESP_OK);
    assert(tud_audio_get_req_entity_cb(0,&r)&&(int16_t)tu_unaligned_read16(reply)==0);
    assert(tud_audio_set_req_entity_cb(0,&r,vol)); /* host change replaces baseline */
    assert(chime_usb_adjust_volume(-1)==ESP_OK);
    assert(tud_audio_get_req_entity_cb(0,&r)&&(int16_t)tu_unaligned_read16(reply)==-21*256);
    assert(tud_audio_set_req_entity_cb(0,&r,vol));
    vol[1]=1;assert(!tud_audio_set_req_entity_cb(0,&r,vol));vol[0]=1;vol[1]=0xec;assert(!tud_audio_set_req_entity_cb(0,&r,vol));
    r.wValue|=1;assert(!tud_audio_set_req_entity_cb(0,&r,vol)&&!tud_audio_get_req_entity_cb(0,&r));
    r=req(2,1,0,1,1);uint8_t mute=1;assert(tud_audio_set_req_entity_cb(0,&r,&mute)&&!chime_usb_wanted()&&chime_usb_epoch()>epoch&&chime_usb_muted());
    mute=0;assert(tud_audio_set_req_entity_cb(0,&r,&mute)&&chime_usb_wanted()&&!chime_usb_muted());
    epoch=chime_usb_epoch();assert(tud_audio_set_req_entity_cb(0,&r,&mute)&&chime_usb_epoch()==epoch);
    r.wLength=2;assert(!tud_audio_set_req_entity_cb(0,&r,&mute));
    audio_feedback_params_t fb={0};tud_audio_feedback_params_cb(0,1,&fb);assert(fb.method==AUDIO_FEEDBACK_METHOD_FIFO_COUNT&&fb.sample_freq==48000);
    /* Run the actual worker until a simulated failure has released its lock
     * and reached idle delay. No task scheduling/concurrency is simulated. */
    assert(chime_usb_output_begin(0)==0);pcm_error=-99;pump_once=true;
    if(setjmp(pump_return)==0)task_fn(NULL);
    pump_once=false;assert(!chime_usb_wanted()&&chime_usb_error()==-99&&writes==1);
    assert(chime_usb_output_begin(0)==ESP_ERR_INVALID_STATE);
    pcm_error=0;assert(tud_audio_set_itf_cb(0,&itf)&&chime_usb_wanted());
    assert(chime_usb_error()==0&&chime_usb_output_begin(0)==0);
    /* No packets must still time out, not bypass the prefill safety check. */
    available=0;clock_us+=101000;pump_once=true;if(setjmp(pump_return)==0)task_fn(NULL);
    pump_once=false;assert(!chime_usb_wanted()&&chime_usb_error()==ESP_ERR_TIMEOUT);
    speed=TUSB_SPEED_FULL;available=4096;assert(tud_audio_set_itf_cb(0,&itf)&&chime_usb_output_begin(0)==0);
    malformed=true;pump_once=true;if(setjmp(pump_return)==0)task_fn(NULL);
    pump_once=false;malformed=false;assert(!chime_usb_wanted()&&chime_usb_error()==ESP_ERR_TIMEOUT);
    assert(tud_audio_set_itf_cb(0,&itf)&&chime_usb_wanted());
    /* Real worker: nonzero host L/R must survive conversion/ramp and reach
     * the successful I2S submission meter. Stop the mock task AFTER unlock. */
    r=req(2,2,0,1,2);vol[0]=vol[1]=0;
    assert(tud_audio_set_req_entity_cb(0,&r,vol));
    host_signal=true;assert(chime_usb_output_begin(0)==0);
    yield_at_write=writes+1;if(setjmp(pump_return)==0)task_fn(NULL);
    bool finished=true;assert(chime_usb_output_poll(&finished)==0&&!finished&&flow_reports==0);
    clock_us+=1001000;assert(chime_usb_output_poll(&finished)==0&&flow_reports==1);
    assert(strstr(flow_log,"frames=128 bytes=512 short=0"));
    assert(strstr(flow_log,"vol_db=0 mute=0 in16=16384/16384"));
    assert(strstr(flow_log,"nz_in=128/128 nz_out=127/127"));
    assert(!strstr(flow_log,"out24=0/0"));
    assert(chime_usb_output_poll(&finished)==0&&flow_reports==1); /* rate limit */
    clock_us+=1001000;assert(chime_usb_output_poll(&finished)==0&&flow_reports==2);
    assert(strstr(flow_log,"frames=0 bytes=0")&&strstr(flow_log,"in16=0/0 out24=0/0"));
    /* Restart resets meter; valid USB silence stays silent, no injected tone. */
    assert(tud_audio_set_itf_cb(0,&itf)&&chime_usb_output_begin(0)==0);host_signal=false;
    yield_at_write=writes+1;if(setjmp(pump_return)==0)task_fn(NULL);
    clock_us+=1001000;assert(chime_usb_output_poll(&finished)==0&&flow_reports==3);
    assert(strstr(flow_log,"frames=128 bytes=512")&&strstr(flow_log,"in16=0/0 out24=0/0"));
    assert(strstr(flow_log,"nz_in=0/0 nz_out=0/0"));
    puts("PASS real USB worker PCM FLOW: nonzero L/R to I2S, silent host, no stale meter after restart, 1s reporting outside output lock");
    /* The very same task handles BT. USB suspend/mute cannot steal its TX,
     * and after BT ends an unchanged USB host epoch can be begun again. */
    chime_usb_output_end();bt_wanted=true;
    assert(chime_stream_bt_begin(0)==0&&bt_rendering);
    uint32_t usb_unchanged_epoch=chime_usb_epoch();
    yield_at_write=writes+1;if(setjmp(pump_return)==0)task_fn(NULL);
    assert(bt_reads==1&&chime_stream_bt_poll(&finished)==0);
    tud_suspend_cb(false);assert(!chime_usb_wanted());
    yield_at_write=writes+1;if(setjmp(pump_return)==0)task_fn(NULL);
    assert(bt_reads==2&&bt_rendering);tud_resume_cb();
    ++bt_epoch;assert(chime_stream_bt_poll(&finished)==ESP_ERR_INVALID_STATE);
    chime_stream_end();assert(!bt_rendering&&bt_ends==1);
    assert(chime_usb_epoch()!=usb_unchanged_epoch); /* suspend/resume events only */
    usb_unchanged_epoch=chime_usb_epoch();
    assert(chime_usb_output_begin(0)==0&&chime_usb_epoch()==usb_unchanged_epoch);
    yield_at_write=writes+1;if(setjmp(pump_return)==0)task_fn(NULL);
    assert(bt_reads==2);chime_usb_output_end();
    bt_wanted=true;assert(chime_stream_bt_begin(0)==0);pcm_error=ESP_ERR_INVALID_STATE;
    yield_at_write=writes+1;if(setjmp(pump_return)==0)task_fn(NULL);
    assert(!bt_wanted&&chime_stream_bt_poll(&finished)==ESP_ERR_INVALID_STATE);
    pcm_error=0;chime_stream_end();assert(bt_ends==2);
    /* Reproduce USB -> BT -> USB with priority-6 PCM winning the mutex
     * race against the lower-priority control owner. The unchanged host USB
     * epoch must recover without unplug/restart, and only one writer exists. */
    assert(chime_usb_wanted()&&chime_usb_output_begin(0)==0);
    yield_at_write=writes+1;if(setjmp(pump_return)==0)task_fn(NULL);
    unsigned before_handoff=writes;uint32_t same_usb_epoch=chime_usb_epoch();
    preempt_stop_owner=true;chime_usb_output_end();
    assert(stop_handoffs==1&&writes==before_handoff&&lock_depth==0);
    bt_wanted=true;assert(chime_stream_bt_begin(0)==0);
    yield_at_write=writes+1;if(setjmp(pump_return)==0)task_fn(NULL);
    before_handoff=writes;preempt_stop_owner=true;chime_stream_end();
    assert(stop_handoffs==2&&writes==before_handoff&&!bt_rendering&&lock_depth==0);
    bt_wanted=false;++bt_epoch; /* disconnect; USB host never stopped */
    assert(chime_usb_wanted()&&chime_usb_epoch()==same_usb_epoch&&chime_usb_output_begin(0)==0);
    before_handoff=writes;yield_at_write=writes+1;if(setjmp(pump_return)==0)task_fn(NULL);
    assert(writes==before_handoff+1&&chime_usb_epoch()==same_usb_epoch);
    before_handoff=writes;preempt_stop_owner=true;chime_usb_output_end();
    assert(stop_handoffs==3&&writes==before_handoff&&lock_depth==0);
    puts("PASS priority-injected stop handoff: USB -> BT -> unchanged-epoch USB, worker blocks before owner lock, no extra I2S/second writer");
    puts("PASS every BT I2S error including INVALID_STATE latches stream failure instead of leaving amps enabled with a dead worker");
    puts("PASS shared USB/BT worker: one task/TX, BT survives USB suspend, generation cancellation, USB restart without epoch change");
    tud_suspend_cb(false);assert(!chime_usb_wanted());tud_resume_cb();assert(chime_usb_wanted());assert(tud_audio_set_itf_cb(0,&itf)&&chime_usb_wanted());
    tinyusb_event_t event={.id=TINYUSB_EVENT_DETACHED};cfg.event_cb(&event,NULL);assert(!chime_usb_wanted());
    assert(tud_audio_set_itf_close_ep_cb(0,&itf));chime_usb_stop();assert(!chime_usb_wanted()&&uninstalls==2&&!phy_live&&phy_creates==phy_deletes);chime_usb_stop();
    /* If the stack cannot be removed, never delete its live PHY or permit
     * another installation. An explicit stop after clearing the injected
     * fault can release it; production reports reset required, no auto retry. */
    fail_stage=4;uninstall_error=-88;
    assert(chime_usb_start()==ESP_ERR_NO_MEM&&phy_live&&chime_usb_error()==-88);
    assert(chime_usb_start()==ESP_ERR_INVALID_STATE);
    uninstall_error=0;fail_stage=0;chime_usb_stop();
    assert(!phy_live&&phy_creates==phy_deletes);
    assert(chime_usb_start()==0);uninstall_error=-89;chime_usb_stop();
    assert(phy_live&&chime_usb_error()==-89&&chime_usb_start()==ESP_ERR_INVALID_STATE);
    uninstall_error=0;chime_usb_stop();assert(!phy_live&&phy_creates==phy_deletes);
    printf("PASS S31 UTMI-only PHY/no OTG IO map, self-powered software VBUS preserved, ownership/rollback; USB callback/control lifecycle: fixed 48k, exact lengths/channel bounds, real master mute/attenuation, async feedback selection, detach/suspend, install failures/cleanup (%u installs); no USB bus/RTOS concurrency simulated\n",installs);
}

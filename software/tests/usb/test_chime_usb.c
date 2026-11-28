#include "chime_usb.h"
#include "chime_render.h"
#include "usb_test_types.h"
#include "esp_private/usb_phy.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static tinyusb_config_t cfg;
static unsigned created,deleted,reports;static bool phy,mounted=true,ready=true,accept=true;
static int fail,render_error,uninstall_error;static uint16_t usage;
static uint8_t reply[64];static uint16_t length;
void chime_usb_test_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
esp_err_t gpio_install_isr_service(int flags){(void)flags;return fail==2?-99:0;}
esp_err_t esp_efuse_mac_get_default(uint8_t *mac){memset(mac,0x11,6);return fail==1?-99:0;}
esp_err_t usb_new_phy(const usb_phy_config_t *c,usb_phy_handle_t *h)
{assert(c->target==USB_PHY_TARGET_UTMI&&c->otg_io_conf==NULL&&!phy);if(fail==5)return -99;phy=true;*h=&phy;++created;return 0;}
esp_err_t usb_del_phy(usb_phy_handle_t h){assert(phy&&h==&phy);phy=false;++deleted;return 0;}
esp_err_t tinyusb_driver_install(const tinyusb_config_t *c){cfg=*c;assert(phy&&c->phy.skip_setup&&c->phy.self_powered);return fail==3?-99:0;}
esp_err_t tinyusb_driver_uninstall(void){return uninstall_error;}
esp_err_t chime_render_start(void){return fail==4?-99:0;}
void chime_render_stop(void){}
esp_err_t chime_render_begin(bool usb_only){(void)usb_only;return render_error;}
void chime_render_end(void){}
esp_err_t chime_render_poll(bool *done){*done=false;return render_error;}
bool tud_mounted(void){return mounted;}
bool tud_hid_ready(void){return ready;}
bool tud_hid_report(uint8_t id,void const *report,uint16_t len)
{assert(id==0&&len==2);if(!accept)return false;const uint8_t *b=report;usage=(uint16_t)(b[0]|((unsigned)b[1]<<8));++reports;return true;}
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
static uint32_t clock_rate=48000U*65536U,last_feedback;static unsigned feedback_calls;
uint32_t bench_audio_clock_rate_q16(void){return clock_rate;}
bool tud_audio_n_fb_set(uint8_t func,uint32_t feedback){assert(!func);last_feedback=feedback;++feedback_calls;return true;}
#endif
tusb_speed_t tud_speed_get(void){return TUSB_SPEED_HIGH;}
uint16_t tud_audio_available(void){return 1024;}
uint16_t tud_audio_read(void *p,uint16_t n){memset(p,0,n);return n;}
bool tud_audio_clear_ep_out_ff(void){return true;}
bool tud_audio_buffer_and_schedule_control_xfer(uint8_t p,const tusb_control_request_t *r,void *b,uint16_t n)
{(void)p;(void)r;memcpy(reply,b,n);length=n;return true;}
extern bool tud_audio_set_itf_cb(uint8_t,const tusb_control_request_t *);
extern bool tud_audio_set_req_entity_cb(uint8_t,const tusb_control_request_t *,uint8_t *);
extern bool tud_audio_get_req_entity_cb(uint8_t,const tusb_control_request_t *);
extern void tud_audio_feedback_params_cb(uint8_t,uint8_t,audio_feedback_params_t *);
extern bool tud_audio_rx_done_isr(uint8_t,uint16_t,uint8_t,uint8_t,uint8_t);
extern void tud_suspend_cb(bool);
extern void tud_resume_cb(void);
int main(void)
{
    for(fail=1;fail<=5;++fail)assert(chime_usb_start()!=0&&!phy&&created==deleted);
    fail=0;assert(chime_usb_start()==0&&phy);assert(chime_usb_start()==ESP_ERR_INVALID_STATE);
    assert(!strcmp(cfg.descriptor.string[2],"Chime USB Audio"));
    assert(cfg.descriptor.device->bcdDevice==0x0102);
    chime_usb_hid_poll();assert(reports==1&&usage==0); /* initial neutral */
    assert(chime_usb_adjust_volume(2)==0);chime_usb_hid_poll();assert(usage==0xe9);
    chime_usb_hid_poll();assert(usage==0);chime_usb_hid_poll();assert(usage==0xe9);chime_usb_hid_poll();assert(usage==0);
    unsigned before=reports;chime_usb_hid_poll();assert(reports==before);
    assert(chime_usb_adjust_volume(-1)==0);accept=false;chime_usb_hid_poll();assert(reports==before);
    accept=true;chime_usb_hid_poll();assert(usage==0xea);chime_usb_hid_poll();assert(usage==0);
    tusb_control_request_t itf={.wIndex=1,.wValue=1};assert(tud_audio_set_itf_cb(0,&itf)&&chime_usb_wanted());
    chime_usb_stats_t stats;chime_usb_snapshot(NULL);chime_usb_snapshot(&stats);
    assert(stats.coherent&&stats.installed&&stats.streaming&&stats.wanted&&!stats.suspended);
    assert(stats.rx_packets==0&&stats.rx_bytes==0&&stats.rx_zlp==0);
    assert(tud_audio_rx_done_isr(0,24,0,1,1)&&tud_audio_rx_done_isr(0,0,0,1,1));
    chime_usb_snapshot(&stats);assert(stats.rx_packets==2&&stats.rx_bytes==24&&stats.rx_zlp==1);
    tusb_control_request_t r={.bRequest=1,.wValue=2<<8,.wIndex=2<<8,.wLength=2};uint8_t vol[]={0,0xec};
    assert(tud_audio_set_req_entity_cb(0,&r,vol));assert(chime_usb_volume()==-20*256);
    assert(chime_usb_adjust_volume(1)==0&&chime_usb_volume()==-20*256); /* no duplicate local attenuation */
    assert(tud_audio_get_req_entity_cb(0,&r)&&length==2&&(int16_t)tu_unaligned_read16(reply)==-20*256);
    uint32_t epoch=chime_usb_epoch();r.wValue=1<<8;r.wLength=1;uint8_t mute=1;
    assert(tud_audio_set_req_entity_cb(0,&r,&mute)&&chime_usb_muted()&&chime_usb_wanted()&&chime_usb_epoch()==epoch);
    tud_suspend_cb(false);chime_usb_snapshot(&stats);assert(stats.suspended&&!stats.wanted&&stats.coherent);assert(!chime_usb_wanted()&&chime_usb_adjust_volume(1)!=0);
    tud_resume_cb();before=reports;chime_usb_hid_poll();assert(reports==before+1&&usage==0);
    before=reports;chime_usb_hid_poll();assert(reports==before); /* stale pre-suspend step dropped */
    assert(chime_usb_adjust_volume(1)==0);mounted=false;
    tinyusb_event_t e={.id=TINYUSB_EVENT_DETACHED};cfg.event_cb(&e,NULL);
    assert(!chime_usb_wanted()&&chime_usb_adjust_volume(1)!=0);
    mounted=true;e.id=TINYUSB_EVENT_ATTACHED;cfg.event_cb(&e,NULL);
    chime_usb_hid_poll();assert(usage==0);before=reports;chime_usb_hid_poll();assert(reports==before);
    audio_feedback_params_t fb={0};tud_audio_feedback_params_cb(0,1,&fb);assert(fb.sample_freq==48000);
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    assert(fb.method==AUDIO_FEEDBACK_METHOD_DISABLED&&last_feedback==6U*65536U);
    unsigned count=feedback_calls;clock_rate+=900000U;
    tud_audio_feedback_params_cb(0,1,&fb);assert(last_feedback>6U*65536U);
    tud_audio_rx_done_isr(0,24,0,1,1);assert(feedback_calls==count+2&&last_feedback>6U*65536U);
#else
    assert(fb.method==AUDIO_FEEDBACK_METHOD_FIFO_COUNT);
#endif
    assert(tud_audio_set_itf_cb(0,&itf));chime_usb_fail(-99);assert(!chime_usb_wanted()&&chime_usb_error()==-99);
    assert(tud_audio_set_itf_cb(0,&itf)&&chime_usb_wanted());
    chime_usb_snapshot(&stats);assert(stats.rx_packets==0&&stats.rx_bytes==0&&stats.rx_zlp==0&&stats.error==0);
    uninstall_error=-98;chime_usb_stop();assert(phy);uninstall_error=0;chime_usb_stop();assert(!phy&&created==deleted);
    puts("PASS USB lifecycle/rollback, UAC controls/feedback, HID press-release, retry, suspend/detach stale-key discard; no host hardware simulated");return 0;
}

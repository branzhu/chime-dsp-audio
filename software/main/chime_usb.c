#include "chime_usb.h"
#include "chime_stream.h"
#include "chime_render.h"
#include "chime_hid.h"
#include "board_support.h"
#include "chime_usb_descriptors.h"
#include "tinyusb.h"
#include "esp_private/usb_phy.h"
#include "tinyusb_default_config.h"
#include "tusb.h"
#include <stdatomic.h>
#include "esp_log.h"
#include "esp_mac.h"
#include <stdio.h>
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
#include "bench_audio.h"
#include "chime_clock_follow.h"
/* RX ISR is the only filter owner. Epoch changes reset it without a task/ISR
 * data race. USB FIFO feedback remains active even with BT disconnected. */
static uint32_t fb_level=2048U<<16,fb_epoch;
#endif
static atomic_bool streaming,muted,failed,suspended;
static atomic_uint epoch;
static atomic_int volume,last_error;
static atomic_uint rx_packets,rx_bytes,rx_zlp;
static atomic_bool installed;
static usb_phy_handle_t owned_phy;
static chime_hid_t hid; /* input task is the only HID queue owner */
static atomic_uint hid_generation;
static unsigned hid_seen;
static const tusb_desc_device_t device={
    .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,
    .bDeviceClass=TUSB_CLASS_MISC,.bDeviceSubClass=MISC_SUBCLASS_COMMON,.bDeviceProtocol=MISC_PROTOCOL_IAD,
    .bMaxPacketSize0=64,.idVendor=0xcafe,.idProduct=0x4011,.bcdDevice=0x0102,
    .iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1
}; /* Development VID/PID only; allocate production identity before release. */
static const tusb_desc_device_qualifier_t qualifier={
    .bLength=sizeof(tusb_desc_device_qualifier_t),.bDescriptorType=TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB=0x0200,.bDeviceClass=TUSB_CLASS_MISC,.bDeviceSubClass=MISC_SUBCLASS_COMMON,
    .bDeviceProtocol=MISC_PROTOCOL_IAD,.bMaxPacketSize0=64,.bNumConfigurations=1,.bReserved=0
};
static const uint8_t hid_report[]={TUD_HID_REPORT_DESC_CONSUMER()};
#define TOTAL_LEN (TUD_CONFIG_DESC_LEN+TUD_AUDIO20_SPEAKER_STEREO_FB_DESC_LEN+TUD_HID_DESC_LEN)
static const uint8_t full_config[]={
    TUD_CONFIG_DESCRIPTOR(1,3,0,TOTAL_LEN,TUSB_DESC_CONFIG_ATT_SELF_POWERED,0),
    TUD_AUDIO20_SPEAKER_STEREO_FB_DESCRIPTOR(0,4,2,16,0x01,196,0x81,4,1),
    TUD_HID_DESCRIPTOR(2,0,HID_ITF_PROTOCOL_NONE,sizeof(hid_report),0x82,2,5)
};
static const uint8_t high_config[]={
    TUD_CONFIG_DESCRIPTOR(1,3,0,TOTAL_LEN,TUSB_DESC_CONFIG_ATT_SELF_POWERED,0),
    /* DWC2 rearms ISO for the next microframe parity. Use every HS
     * microframe, not bInterval=4 (8 microframes, same parity each time).
     * 48k stereo PCM16: 6 frames/125us + one frame of feedback headroom.
     * Feedback also uses interval 1 to avoid a longer ISO IN schedule. */
    TUD_AUDIO20_SPEAKER_STEREO_FB_DESCRIPTOR(0,4,2,16,0x01,28,0x81,4,1),
    TUD_HID_DESCRIPTOR(2,0,HID_ITF_PROTOCOL_NONE,sizeof(hid_report),0x82,2,7)
};
_Static_assert(sizeof(full_config)==TOTAL_LEN,"FS descriptor length");
_Static_assert(sizeof(high_config)==TOTAL_LEN,"HS descriptor length");
static char serial[17];
static const char *strings[]={ (const char[]){0x09,0x04},"Chime Lab","Chime USB Audio",serial,"Stereo 48kHz"};
static void stream_stop(void)
{
    atomic_store(&streaming,false);atomic_fetch_add(&epoch,1);
}
static void event(tinyusb_event_t *e,void *arg)
{
    (void)arg;if(e->id==TINYUSB_EVENT_ATTACHED)atomic_fetch_add(&hid_generation,1);
    if(e->id==TINYUSB_EVENT_DETACHED){atomic_fetch_add(&hid_generation,1);atomic_store(&suspended,false);stream_stop();}
}
void tud_suspend_cb(bool remote){(void)remote;atomic_store(&suspended,true);atomic_fetch_add(&epoch,1);atomic_fetch_add(&hid_generation,1);}
void tud_resume_cb(void){atomic_store(&suspended,false);atomic_fetch_add(&epoch,1);atomic_fetch_add(&hid_generation,1);}
bool tud_audio_set_itf_cb(uint8_t rhport,tusb_control_request_t const *r)
{
    (void)rhport;if(r->wIndex!=1||r->wValue>1)return false;
    atomic_store(&rx_packets,0);atomic_store(&rx_bytes,0);atomic_store(&rx_zlp,0);
    atomic_store(&streaming,r->wValue==1);atomic_store(&failed,false);atomic_store(&last_error,ESP_OK);atomic_fetch_add(&epoch,1);return true;
}
bool tud_audio_set_itf_close_ep_cb(uint8_t rhport,tusb_control_request_t const *r)
{
    (void)rhport;if(r->wIndex!=1)return false;stream_stop();return true;
}
bool tud_audio_rx_done_isr(uint8_t port,uint16_t bytes,uint8_t func,uint8_t ep,uint8_t alt)
{
    (void)port;(void)func;(void)ep;(void)alt;
    atomic_fetch_add(&rx_packets,1);atomic_fetch_add(&rx_bytes,bytes);
    if(!bytes)atomic_fetch_add(&rx_zlp,1);
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    uint32_t generation=atomic_load(&epoch);
    if(fb_epoch!=generation){fb_level=2048U<<16;fb_epoch=generation;}
    uint32_t feedback=chime_clock_usb_feedback(&fb_level,tud_audio_available(),
        bench_audio_clock_rate_q16(),tud_speed_get()==TUSB_SPEED_HIGH);
    tud_audio_n_fb_set(func,feedback);
#endif
    return true; /* No logging, GPIO, I2C or blocking work in this ISR. */
}
void tud_audio_feedback_params_cb(uint8_t func,uint8_t alt,audio_feedback_params_t *p)
{
    (void)func;(void)alt;p->sample_freq=48000;
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    /* Manual: do not simultaneously run TinyUSB's fixed-nominal FIFO servo.
     * Seed before the first packet, not the first OUT callback. Q16 samples
     * per USB (micro)frame. FS conversion is still handled by TinyUSB. */
    p->method=AUDIO_FEEDBACK_METHOD_DISABLED;
    tud_audio_n_fb_set(func,bench_audio_clock_rate_q16()/(tud_speed_get()==TUSB_SPEED_HIGH?8000U:1000U));
#else
    p->method=AUDIO_FEEDBACK_METHOD_FIFO_COUNT;
#endif
}
bool tud_audio_get_req_entity_cb(uint8_t port,tusb_control_request_t const *r)
{
    if((r->wIndex&255)!=0)return false;
    unsigned entity=r->wIndex>>8,ctrl=r->wValue>>8,ch=r->wValue&255;
    if(ch)return false;
    if(entity==4) {
        if(ctrl==AUDIO20_CS_CTRL_SAM_FREQ) {
            if(r->bRequest==AUDIO20_CS_REQ_CUR){uint32_t rate=tu_htole32(48000);return tud_audio_buffer_and_schedule_control_xfer(port,r,&rate,4);}
            if(r->bRequest==AUDIO20_CS_REQ_RANGE) {
                audio20_control_range_4_n_t(1) range={.wNumSubRanges=tu_htole16(1),
                    .subrange={{.bMin=(int32_t)tu_htole32(48000),.bMax=(int32_t)tu_htole32(48000),.bRes=0}}};
                return tud_audio_buffer_and_schedule_control_xfer(port,r,&range,sizeof(range));
            }
        }
        if(ctrl==AUDIO20_CS_CTRL_CLK_VALID&&r->bRequest==AUDIO20_CS_REQ_CUR){uint8_t valid=1;return tud_audio_buffer_and_schedule_control_xfer(port,r,&valid,1);}
    }
    if(entity==2) {
        if(ctrl==AUDIO20_FU_CTRL_MUTE&&r->bRequest==AUDIO20_CS_REQ_CUR){uint8_t v=atomic_load(&muted);return tud_audio_buffer_and_schedule_control_xfer(port,r,&v,1);}
        if(ctrl==AUDIO20_FU_CTRL_VOLUME) {
            if(r->bRequest==AUDIO20_CS_REQ_CUR){int16_t v=(int16_t)tu_htole16((uint16_t)atomic_load(&volume));return tud_audio_buffer_and_schedule_control_xfer(port,r,&v,2);}
            if(r->bRequest==AUDIO20_CS_REQ_RANGE){audio20_control_range_2_n_t(1) range={.wNumSubRanges=tu_htole16(1),
                .subrange={{.bMin=(int16_t)tu_htole16((uint16_t)(-60*256)),.bMax=0,.bRes=tu_htole16(256)}}};
                return tud_audio_buffer_and_schedule_control_xfer(port,r,&range,sizeof(range));}
        }
    }
    return false;
}
bool tud_audio_set_req_entity_cb(uint8_t port,tusb_control_request_t const *r,uint8_t *buf)
{
    (void)port;if((r->wIndex&255)!=0||(r->wValue&255)!=0||r->bRequest!=AUDIO20_CS_REQ_CUR)return false;
    unsigned entity=r->wIndex>>8,ctrl=r->wValue>>8;
    if(entity==4&&ctrl==AUDIO20_CS_CTRL_SAM_FREQ&&r->wLength==4)return tu_le32toh(tu_unaligned_read32(buf))==48000;
    if(entity==2) {
        if(ctrl==AUDIO20_FU_CTRL_MUTE&&r->wLength==1&&buf[0]<=1){atomic_store(&muted,buf[0]!=0);return true;}
        if(ctrl==AUDIO20_FU_CTRL_VOLUME&&r->wLength==2){int v=(int16_t)tu_le16toh(tu_unaligned_read16(buf));if(v>0||v< -60*256||v%256)return false;atomic_store(&volume,v);return true;}
    }
    return false;
}
static void hid_sync(void)
{
    unsigned generation=atomic_load(&hid_generation);
    if(generation!=hid_seen){chime_hid_reset(&hid);hid_seen=generation;}
}
esp_err_t chime_usb_adjust_volume(int steps)
{
    hid_sync();
    if(!installed||atomic_load(&suspended)||!tud_mounted())return ESP_ERR_INVALID_STATE;
    chime_hid_adjust(&hid,steps);return ESP_OK;
}
void chime_usb_hid_poll(void)
{
    hid_sync();
    if(!installed||atomic_load(&suspended)||!tud_mounted())return;
    uint16_t usage;
    if(tud_hid_ready()&&chime_hid_peek(&hid,&usage)){
        uint8_t report[2]={(uint8_t)usage,(uint8_t)(usage>>8)};
        if(tud_hid_report(0,report,sizeof(report)))chime_hid_sent(&hid);
    }
}
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance){(void)instance;return hid_report;}
uint16_t tud_hid_get_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,uint8_t *buffer,uint16_t len)
{(void)instance;(void)id;(void)type;(void)buffer;(void)len;return 0;}
void tud_hid_set_report_cb(uint8_t instance,uint8_t id,hid_report_type_t type,uint8_t const *buffer,uint16_t len)
{(void)instance;(void)id;(void)type;(void)buffer;(void)len;}
bool chime_usb_wanted(void){return installed&&atomic_load(&streaming)&&!atomic_load(&suspended)&&!atomic_load(&failed);}
bool chime_usb_muted(void){return atomic_load(&muted);}
int chime_usb_volume(void){return atomic_load(&volume);}
uint32_t chime_usb_epoch(void){return atomic_load(&epoch);}
esp_err_t chime_usb_error(void){return atomic_load(&last_error);}
uint16_t chime_usb_fifo(void){return installed&&atomic_load(&streaming)?tud_audio_available():0;}
uint16_t chime_usb_read(void *pcm,uint16_t bytes){return installed?tud_audio_read(pcm,bytes):0;}
void chime_usb_clear(void){if(installed)tud_audio_clear_ep_out_ff();}
void chime_usb_fail(esp_err_t err){atomic_store(&failed,true);atomic_store(&last_error,err);}
/* USB-only callbacks are reserved for explicitly armed external DSP audition. */
esp_err_t chime_usb_output_begin(unsigned unused){(void)unused;return chime_render_begin(true);}
esp_err_t chime_usb_output_poll(bool *finished){return chime_render_poll(finished);}
void chime_usb_output_end(void){chime_render_end();}
void chime_stream_end(void){chime_render_end();}
esp_err_t chime_stream_mix_begin(unsigned unused){(void)unused;return chime_render_begin(false);}
esp_err_t chime_stream_mix_poll(bool *finished){return chime_render_poll(finished);}
static void release_phy(void)
{
    if(!owned_phy)return;
    esp_err_t err=usb_del_phy(owned_phy);
    if(err==ESP_OK)owned_phy=NULL;
    else ESP_LOGE("chime_usb","UTMI PHY cleanup failed=%d; reset required",err);
}
esp_err_t chime_usb_start(void)
{
    if(installed||owned_phy)return ESP_ERR_INVALID_STATE;
    /* Chip-unique serial, not shared by every board. */
    uint8_t mac[6];esp_err_t err=esp_efuse_mac_get_default(mac);
    if(err)goto fail;
    snprintf(serial,sizeof(serial),"%02X%02X%02X%02X%02X%02X",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    tinyusb_config_t cfg=TINYUSB_DEFAULT_CONFIG(event,NULL);
    cfg.phy.self_powered=true;cfg.phy.vbus_monitor_io=BOARD_GPIO_VBUS_DET;
    /* S31 has UTMI only and usb_dwc_info.otg_signals == NULL. esp_tinyusb
     * 2.4.0 passes USB_PHY_SELF_POWERED_DEVICE to usb_new_phy, whose legacy
     * OTG GPIO setup asserts on this target. Own UTMI setup without OTG IOs;
     * keep self_powered=true so the driver's software VBUS monitor stays on. */
    cfg.phy.skip_setup=true;
    cfg.descriptor.qualifier=&qualifier;
    cfg.descriptor.device=&device;cfg.descriptor.string=strings;cfg.descriptor.string_count=5;
    cfg.descriptor.full_speed_config=full_config;cfg.descriptor.high_speed_config=high_config;
    /* Board GPIO ISR service is shared; do not uninstall it here. */
    err=gpio_install_isr_service(0);if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE)goto fail;
    const usb_phy_config_t phy_cfg={.controller=USB_PHY_CTRL_OTG,
        .target=USB_PHY_TARGET_UTMI,.otg_mode=USB_OTG_MODE_DEVICE,
        .otg_speed=USB_PHY_SPEED_HIGH,.otg_io_conf=NULL,.ext_io_conf=NULL};
    err=usb_new_phy(&phy_cfg,&owned_phy);if(err)goto fail;
    err=tinyusb_driver_install(&cfg);if(err)goto fail;
    installed=true;atomic_store(&volume,0);atomic_store(&muted,false);atomic_store(&suspended,false);
    chime_hid_reset(&hid);hid_seen=atomic_fetch_add(&hid_generation,1)+1;
    err=chime_render_start();
    if(err!=ESP_OK){
        esp_err_t cleanup=tinyusb_driver_uninstall();
        if(cleanup!=ESP_OK){chime_usb_fail(cleanup);return err;}
        installed=false;goto fail;
    }
    ESP_LOGI("chime_usb","PHY utmi-self-powered-v2; application-owned UTMI, legacy OTG IO mapping skipped; software VBUS=IO%d",BOARD_GPIO_VBUS_DET);
    ESP_LOGI("chime_usb","STREAM hs-microframe-v4; HS=125us/28B FS=1ms/196B; nominal first feedback, ZLP-safe OUT; prefill=1024 bytes /100ms");
    ESP_LOGI("chime_usb","UAC2 + HID PC volume; short press is local mixed-output mute");
    ESP_LOGI("chime_usb","UAC2 USB1 native PHY, 48kHz stereo PCM16, async feedback; master -60..0dB, unity source summing, no fixed attenuation; NO power limiter; selected outputs");
    return ESP_OK;
fail:
    release_phy();
    return err;
}
void chime_usb_stop(void)
{
    if(!installed)return;
    chime_render_stop();
    esp_err_t err=tinyusb_driver_uninstall();
    if(err!=ESP_OK){atomic_store(&failed,true);atomic_store(&last_error,err);
        ESP_LOGE("chime_usb","USB stop failed=%d; resources retained; reset required",err);return;}
    installed=false;release_phy();stream_stop();atomic_fetch_add(&hid_generation,1);
}

void chime_usb_snapshot(chime_usb_stats_t *out)
{
    if(!out)return;
    chime_usb_stats_t s={0};
    for(unsigned attempt=0;attempt<3;++attempt){
        uint32_t before=atomic_load(&epoch);
        s=(chime_usb_stats_t){.epoch=before,.rx_packets=atomic_load(&rx_packets),
            .rx_bytes=atomic_load(&rx_bytes),.rx_zlp=atomic_load(&rx_zlp),
            .installed=atomic_load(&installed),.streaming=atomic_load(&streaming),
            .suspended=atomic_load(&suspended),.muted=atomic_load(&muted),
            .wanted=chime_usb_wanted(),.error=atomic_load(&last_error)};
        if(before==atomic_load(&epoch)){s.coherent=true;break;}
    }
    *out=s;
}

#include "chime_usb.h"
#include "chime_stream.h"
#include "chime_bluetooth.h"
#include "bench_audio.h"
#include "board_support.h"
#include "chime_pcm.h"
#include "chime_usb_descriptors.h"
#include "tinyusb.h"
#include "esp_private/usb_phy.h"
#include "tinyusb_default_config.h"
#include "tusb.h"
#include <stdatomic.h>
#include <string.h>
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
static atomic_bool streaming,muted,failed,quitting,suspended;
static atomic_uint epoch;
static atomic_int volume,last_error;
static atomic_uint rx_packets,rx_bytes,rx_zlp;
static uint16_t prefill_peak;
static SemaphoreHandle_t output_lock;
static TaskHandle_t worker;
static atomic_bool worker_running;
static atomic_bool installed;
static usb_phy_handle_t owned_phy; /* TinyUSB skip_setup: application owns teardown. */
static uint32_t ramp_frame;
static bool primed;
static uint64_t prime_started;
static bool output_bluetooth;
static uint32_t bluetooth_epoch;
static bool output_enabled; /* protected by output_lock */
/* Owner is lower priority than the PCM worker. Publish stop BEFORE waiting
 * for output_lock: otherwise the worker can give/re-take it indefinitely.
 * Only a serialized begin clears this request, while holding output_lock. */
static atomic_bool output_stop_requested;
/* Worker accumulates only successfully submitted I2S blocks. Owner polls
 * snapshots; serial logging never occurs inside the worker or either lock. */
typedef struct {
    chime_pcm_levels_t levels;
    uint32_t frames,bytes,short_blocks;
    uint16_t fifo;
} pcm_flow_t;
static pcm_flow_t flow;
static uint64_t flow_started;
/* Separate, short statistics lock: the owner must not wait for the PCM
 * worker's output_lock while that worker is blocked inside I2S DMA write. */
static portMUX_TYPE flow_mux=portMUX_INITIALIZER_UNLOCKED;
static const tusb_desc_device_t device={
    .bLength=sizeof(tusb_desc_device_t),.bDescriptorType=TUSB_DESC_DEVICE,.bcdUSB=0x0200,
    .bDeviceClass=TUSB_CLASS_MISC,.bDeviceSubClass=MISC_SUBCLASS_COMMON,.bDeviceProtocol=MISC_PROTOCOL_IAD,
    .bMaxPacketSize0=64,.idVendor=0xcafe,.idProduct=0x4011,.bcdDevice=0x0101,
    .iManufacturer=1,.iProduct=2,.iSerialNumber=3,.bNumConfigurations=1
}; /* Development VID/PID only; allocate production identity before release. */
static const tusb_desc_device_qualifier_t qualifier={
    .bLength=sizeof(tusb_desc_device_qualifier_t),.bDescriptorType=TUSB_DESC_DEVICE_QUALIFIER,
    .bcdUSB=0x0200,.bDeviceClass=TUSB_CLASS_MISC,.bDeviceSubClass=MISC_SUBCLASS_COMMON,
    .bDeviceProtocol=MISC_PROTOCOL_IAD,.bMaxPacketSize0=64,.bNumConfigurations=1,.bReserved=0
};
#define TOTAL_LEN (TUD_CONFIG_DESC_LEN+TUD_AUDIO20_SPEAKER_STEREO_FB_DESC_LEN)
static const uint8_t full_config[]={
    TUD_CONFIG_DESCRIPTOR(1,2,0,TOTAL_LEN,TUSB_DESC_CONFIG_ATT_SELF_POWERED,0),
    TUD_AUDIO20_SPEAKER_STEREO_FB_DESCRIPTOR(0,4,2,16,0x01,196,0x81,4,1)
};
static const uint8_t high_config[]={
    TUD_CONFIG_DESCRIPTOR(1,2,0,TOTAL_LEN,TUSB_DESC_CONFIG_ATT_SELF_POWERED,0),
    /* DWC2 rearms ISO for the next microframe parity. Use every HS
     * microframe, not bInterval=4 (8 microframes, same parity each time).
     * 48k stereo PCM16: 6 frames/125us + one frame of feedback headroom.
     * Feedback also uses interval 1 to avoid a longer ISO IN schedule. */
    TUD_AUDIO20_SPEAKER_STEREO_FB_DESCRIPTOR(0,4,2,16,0x01,28,0x81,4,1)
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
    (void)arg;if(e->id==TINYUSB_EVENT_DETACHED){atomic_store(&suspended,false);stream_stop();}
}
void tud_suspend_cb(bool remote){(void)remote;atomic_store(&suspended,true);atomic_fetch_add(&epoch,1);}
void tud_resume_cb(void){atomic_store(&suspended,false);atomic_fetch_add(&epoch,1);}
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
    return true; /* No logging, GPIO, I2C or blocking work in this ISR. */
}
void tud_audio_feedback_params_cb(uint8_t func,uint8_t alt,audio_feedback_params_t *p)
{
    (void)func;(void)alt;p->method=AUDIO_FEEDBACK_METHOD_FIFO_COUNT;p->sample_freq=48000;
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
        if(ctrl==AUDIO20_FU_CTRL_MUTE&&r->wLength==1&&buf[0]<=1){if(atomic_exchange(&muted,buf[0]!=0)!=(buf[0]!=0))atomic_fetch_add(&epoch,1);return true;}
        if(ctrl==AUDIO20_FU_CTRL_VOLUME&&r->wLength==2){int v=(int16_t)tu_le16toh(tu_unaligned_read16(buf));if(v>0||v< -60*256||v%256)return false;atomic_store(&volume,v);return true;}
    }
    return false;
}
esp_err_t chime_usb_adjust_volume(int steps)
{
    if(!atomic_load(&installed))return ESP_ERR_INVALID_STATE;
    if(steps>60)steps=60;
    if(steps< -60)steps=-60;
    int old=atomic_load(&volume),next;
    do {
        next=old+steps*256;
        if(next>0)next=0;
        if(next< -60*256)next=-60*256;
    }while(!atomic_compare_exchange_weak(&volume,&old,next));
    /* Same state read by UAC GET_CUR and the existing smoothed renderer.
     * This descriptor has no interrupt endpoint: host slider refresh is not
     * guaranteed; do not claim HID/system-volume synchronization. */
    return ESP_OK;
}
bool chime_usb_wanted(void){return installed&&atomic_load(&streaming)&&!atomic_load(&suspended)&&!atomic_load(&muted)&&!atomic_load(&failed);}
bool chime_usb_muted(void){return atomic_load(&muted);}
uint32_t chime_usb_epoch(void){return atomic_load(&epoch);}
esp_err_t chime_usb_error(void){return atomic_load(&last_error);}
static void pump_task(void *arg)
{
    (void)arg;uint8_t pcm[512];int32_t out[256];uint64_t starved_since=0;
    bool previous_bluetooth=false;
    int last_bt_volume=1;int32_t bt_gain=0;
    int last_volume=1;int32_t gain=32768,current_gain=32768;uint32_t volume_ramp=0;int32_t volume_start=32768;
    while(!atomic_load(&quitting)) {
        xSemaphoreTake(output_lock,portMAX_DELAY);
        if(atomic_load(&output_stop_requested))output_enabled=false;
        if(output_enabled&&output_bluetooth) {
            /* The only continuous-audio I2S writer handles BOTH transports.
             * USB stays enumerated and is drained below while BT owns TX. */
            previous_bluetooth=true;
            bool ready=false;
            uint32_t bt_generation=chime_bluetooth_epoch();
            esp_err_t err=bt_generation==bluetooth_epoch?chime_bluetooth_render(pcm,128,&ready):ESP_ERR_INVALID_STATE;
            int target_volume=chime_bluetooth_volume();
            if(target_volume!=last_bt_volume){bt_gain=target_volume<=-96*256?0:chime_pcm_gain(target_volume);last_bt_volume=target_volume;}
            int32_t target=bt_gain;
            if(ramp_frame==0)current_gain=target;
            for(unsigned frame=0;frame<128&&err==ESP_OK;++frame){
                if(current_gain!=target){int32_t delta=(target-current_gain)/480;current_gain+=delta?delta:target>current_gain?1:-1;}
                chime_pcm_convert(pcm+frame*4,out+frame*2,1,current_gain);
                if(ramp_frame<480&&ready){
                    for(unsigned ch=0;ch<2;++ch)out[frame*2+ch]=(int32_t)((int64_t)(out[frame*2+ch]/256)*ramp_frame/480)*256;
                    ++ramp_frame;
                }
            }
            bool write_failed=false;
            if(err==ESP_OK&&bt_generation==chime_bluetooth_epoch()){
                err=bench_audio_pcm_write(out,128);write_failed=err!=ESP_OK;
            }else if(err==ESP_OK)err=ESP_ERR_INVALID_STATE;
            if(err!=ESP_OK){output_enabled=false;if(write_failed)chime_bluetooth_fail(err);}
            /* At least 192 bytes/ms at 48k PCM16 stereo, including time
             * spent blocked in I2S. Drain a full bounded FIFO window. */
            for(unsigned i=0;i<16&&atomic_load(&streaming)&&tud_audio_available();++i)tud_audio_read(pcm,sizeof(pcm));
            xSemaphoreGive(output_lock);
            if(err!=ESP_OK)vTaskDelay(pdMS_TO_TICKS(1));
        } else if(output_enabled&&chime_usb_wanted()) {
            if(previous_bluetooth){last_volume=1;gain=current_gain=32768;volume_ramp=0;previous_bluetooth=false;}
            /* Start with a few ms queued so scheduling jitter does not create
             * zero-padded holes. The asynchronous feedback holds FIFO level. */
            uint16_t available=tud_audio_available();
            if(!primed&&available>prefill_peak)prefill_peak=available;
            if(!primed&&available<1024) {
                if((uint64_t)esp_timer_get_time()-prime_started>100000ULL){atomic_store(&failed,true);atomic_store(&last_error,ESP_ERR_TIMEOUT);output_enabled=false;
                    ESP_LOGE("chime_usb","USB prefill timeout fifo=%u peak=%u rx_packets=%u rx_bytes=%u zlp=%u; restart host stream",
                        (unsigned)available,(unsigned)prefill_peak,atomic_load(&rx_packets),atomic_load(&rx_bytes),atomic_load(&rx_zlp));}
                xSemaphoreGive(output_lock);vTaskDelay(pdMS_TO_TICKS(1));continue;
            }
            if(!primed)ESP_LOGI("chime_usb","PCM READY fifo=%u rx_packets=%u rx_bytes=%u zlp=%u",
                (unsigned)available,atomic_load(&rx_packets),atomic_load(&rx_bytes),atomic_load(&rx_zlp));
            primed=true;
            memset(pcm,0,sizeof(pcm));
            uint16_t n=tud_audio_read(pcm,sizeof(pcm));
            if(n<sizeof(pcm)){if(!starved_since)starved_since=(uint64_t)esp_timer_get_time();}
            else starved_since=0;
            if((starved_since&&(uint64_t)esp_timer_get_time()-starved_since>100000ULL)||n%4){atomic_store(&failed,true);atomic_store(&last_error,ESP_ERR_TIMEOUT);output_enabled=false;ESP_LOGE("chime_usb","USB underrun/misaligned PCM; latched until host stream restart");}
            else {
                int requested_volume=atomic_load(&volume);
                if(requested_volume!=last_volume){gain=chime_pcm_gain(requested_volume);last_volume=requested_volume;volume_start=current_gain;volume_ramp=0;}
                for(unsigned frame=0;frame<128;++frame) {
                    if(volume_ramp<480){++volume_ramp;current_gain=volume_start+(int32_t)((int64_t)(gain-volume_start)*volume_ramp/480);}
                    chime_pcm_convert(pcm+frame*4,out+frame*2,1,current_gain);
                    if(ramp_frame<480) {
                        for(unsigned ch=0;ch<2;++ch)out[frame*2+ch]=(int32_t)((int64_t)(out[frame*2+ch]/256)*ramp_frame/480)*256;
                        ++ramp_frame;
                    }
                }
                int err=bench_audio_pcm_write(out,128);
                if(err){atomic_store(&failed,true);atomic_store(&last_error,err);output_enabled=false;ESP_LOGE("chime_usb","I2S write failed=%d; USB latched until host stream restart",err);}
                else {
                    chime_pcm_levels_t levels={0};chime_pcm_measure(&levels,pcm,out,128);
                    uint16_t fifo=tud_audio_available();
                    portENTER_CRITICAL(&flow_mux);
                    for(unsigned ch=0;ch<2;++ch) {
                        if(levels.input_peak[ch]>flow.levels.input_peak[ch])flow.levels.input_peak[ch]=levels.input_peak[ch];
                        if(levels.output_peak[ch]>flow.levels.output_peak[ch])flow.levels.output_peak[ch]=levels.output_peak[ch];
                        flow.levels.input_nonzero[ch]+=levels.input_nonzero[ch];
                        flow.levels.output_nonzero[ch]+=levels.output_nonzero[ch];
                    }
                    flow.frames+=128;flow.bytes+=n;
                    if(n<sizeof(pcm))++flow.short_blocks;
                    flow.fifo=fifo;
                    portEXIT_CRITICAL(&flow_mux);
                }
            }
            xSemaphoreGive(output_lock);
        } else {
            starved_since=0;
            /* Drain unused host data; never replay samples after SS+/sine. */
            for(unsigned i=0;i<16&&atomic_load(&streaming)&&tud_audio_available();++i)tud_audio_read(pcm,sizeof(pcm));
            xSemaphoreGive(output_lock);vTaskDelay(pdMS_TO_TICKS(1));
        }
    }
    atomic_store(&worker_running,false);vTaskDelete(NULL);
}
esp_err_t chime_usb_output_begin(unsigned unused)
{
    (void)unused;if(!chime_usb_wanted())return ESP_ERR_INVALID_STATE;
    const bool high_speed=tud_speed_get()==TUSB_SPEED_HIGH;
    ESP_LOGI("chime_usb","PCM BEGIN speed=%s; OUT interval=%uus max_packet=%u",
        high_speed?"HS":"FS",high_speed?125u:1000u,high_speed?28u:196u);
    (void)high_speed;
    xSemaphoreTake(output_lock,portMAX_DELAY);tud_audio_clear_ep_out_ff();primed=false;prefill_peak=0;prime_started=(uint64_t)esp_timer_get_time();ramp_frame=0;
    portENTER_CRITICAL(&flow_mux);memset(&flow,0,sizeof(flow));flow_started=prime_started;portEXIT_CRITICAL(&flow_mux);
    atomic_store(&output_stop_requested,false);
    output_bluetooth=false;output_enabled=true;xSemaphoreGive(output_lock);return ESP_OK;
}
void chime_usb_output_end(void)
{
    if(!output_lock)return;
    /* At most the in-flight bounded I2S block may complete. On its next loop
     * the worker enters the idle branch and blocks for a tick, allowing this
     * lower-priority owner to acquire the mutex and confirm no writer is live.
     * Do not replace with taskYIELD(): it need not run lower-priority tasks. */
    atomic_store(&output_stop_requested,true);
    xSemaphoreTake(output_lock,portMAX_DELAY);output_enabled=false;
    if(output_bluetooth)chime_bluetooth_output_end();
    output_bluetooth=false;xSemaphoreGive(output_lock);
}
void chime_stream_end(void){chime_usb_output_end();}
esp_err_t chime_stream_bt_begin(unsigned unused)
{
    (void)unused;
    if(!output_lock)return ESP_ERR_INVALID_STATE;
    xSemaphoreTake(output_lock,portMAX_DELAY);
    esp_err_t err=chime_bluetooth_output_begin();
    if(err==ESP_OK){bluetooth_epoch=chime_bluetooth_epoch();ramp_frame=0;
        atomic_store(&output_stop_requested,false);output_bluetooth=true;output_enabled=true;}
    xSemaphoreGive(output_lock);return err;
}
esp_err_t chime_stream_bt_poll(bool *finished)
{
    *finished=false;
    if(bluetooth_epoch!=chime_bluetooth_epoch()||!chime_bluetooth_wanted())return ESP_ERR_INVALID_STATE;
    return ESP_OK;
}
esp_err_t chime_usb_output_poll(bool *finished)
{
    *finished=false;if(!chime_usb_wanted())return ESP_ERR_INVALID_STATE;
    pcm_flow_t snapshot={0};bool report=false;uint32_t elapsed_ms=0;
    uint64_t now=(uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&flow_mux);
    if(now-flow_started>=1000000ULL) {
        snapshot=flow;memset(&flow,0,sizeof(flow));
        elapsed_ms=(uint32_t)((now-flow_started)/1000);flow_started=now;report=true;
    }
    portEXIT_CRITICAL(&flow_mux);
    if(report)ESP_LOGI("chime_usb","PCM FLOW ms=%u frames=%u bytes=%u short=%u fifo=%u vol_db=%d mute=%u in16=%u/%u out24=%u/%u nz_in=%u/%u nz_out=%u/%u rx_packets=%u",
        (unsigned)elapsed_ms,(unsigned)snapshot.frames,(unsigned)snapshot.bytes,
        (unsigned)snapshot.short_blocks,(unsigned)snapshot.fifo,atomic_load(&volume)/256,
        (unsigned)atomic_load(&muted),(unsigned)snapshot.levels.input_peak[0],(unsigned)snapshot.levels.input_peak[1],
        (unsigned)snapshot.levels.output_peak[0],(unsigned)snapshot.levels.output_peak[1],
        (unsigned)snapshot.levels.input_nonzero[0],(unsigned)snapshot.levels.input_nonzero[1],
        (unsigned)snapshot.levels.output_nonzero[0],(unsigned)snapshot.levels.output_nonzero[1],atomic_load(&rx_packets));
    return ESP_OK;
}
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
    output_lock=xSemaphoreCreateMutex();if(!output_lock)return ESP_ERR_NO_MEM;
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
    installed=true;atomic_store(&quitting,false);atomic_store(&volume,0);
    atomic_store(&worker_running,true);
    if(xTaskCreate(pump_task,"source_pcm",4096,NULL,6,&worker)!=pdPASS){
        atomic_store(&worker_running,false);err=ESP_ERR_NO_MEM;
        esp_err_t cleanup=tinyusb_driver_uninstall();
        if(cleanup!=ESP_OK){atomic_store(&failed,true);atomic_store(&last_error,cleanup);
            ESP_LOGE("chime_usb","USB rollback failed=%d; resources retained; reset required",cleanup);return err;}
        installed=false;goto fail;
    }
    ESP_LOGI("chime_usb","PHY utmi-self-powered-v2; application-owned UTMI, legacy OTG IO mapping skipped; software VBUS=IO%d",BOARD_GPIO_VBUS_DET);
    ESP_LOGI("chime_usb","STREAM hs-microframe-v4; HS=125us/28B FS=1ms/196B; nominal first feedback, ZLP-safe OUT; prefill=1024 bytes /100ms");
    ESP_LOGI("chime_usb","METER pcm-level-v5; PCM FLOW every 1s: input16 / submitted I2S24 peaks; no gain changes");
    ESP_LOGI("chime_usb","UAC2 USB1 native PHY, 48kHz stereo PCM16, async feedback; master -60..0dB, unity PCM at 0dB; NO power limiter; selected outputs");
    return ESP_OK;
fail:
    release_phy();
    vSemaphoreDelete(output_lock);output_lock=NULL;return err;
}
void chime_usb_stop(void)
{
    if(!installed)return;
    chime_usb_output_end();atomic_store(&quitting,true);
    while(atomic_load(&worker_running))vTaskDelay(pdMS_TO_TICKS(1));
    esp_err_t err=tinyusb_driver_uninstall();
    if(err!=ESP_OK){atomic_store(&failed,true);atomic_store(&last_error,err);
        ESP_LOGE("chime_usb","USB stop failed=%d; resources retained; reset required",err);return;}
    installed=false;release_phy();vSemaphoreDelete(output_lock);output_lock=NULL;stream_stop();
}

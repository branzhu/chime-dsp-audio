#include "chime_indicator.h"
#include "board_support.h"
#include "chime_network.h"
#include "chime_usb.h"
#include "chime_controls.h"
#include "chime_bluetooth.h"
#include <stdatomic.h>
#include "driver/ledc.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static atomic_uint base_state=4U; /* starting */
static atomic_bool standby,pairing,fatal;
static bool started;
static const gpio_num_t pins[]={BOARD_GPIO_LED_R,BOARD_GPIO_LED_G,BOARD_GPIO_LED_B};
void chime_indicator_publish(chime_indicator_state_t s)
{
    atomic_store(&base_state,(s.fault?1U:0U)|(s.stopped?2U:0U)|
        (s.starting?4U:0U)|(s.bluetooth?8U:0U));
}
void chime_indicator_set_standby(bool active){atomic_store(&standby,active);}
void chime_indicator_set_pairing(bool active){atomic_store(&pairing,active);}
void chime_indicator_fail(void){atomic_store(&fatal,true);}
static void release_channels(unsigned count)
{
    for(unsigned i=0;i<count;++i){
        ledc_channel_config_t c={.speed_mode=LEDC_LOW_SPEED_MODE,.channel=(ledc_channel_t)i,.deconfigure=true};
        (void)ledc_channel_config(&c);
        (void)gpio_set_level(pins[i],1);
        (void)gpio_set_direction(pins[i],GPIO_MODE_OUTPUT);
    }
}
static void worker(void *arg)
{
    (void)arg;chime_led_t led={0};
    for(;;){
        unsigned base=atomic_load(&base_state);
        chime_led_input_t input={.fault=(base&1U)!=0||atomic_load(&fatal),
            .stopped=(base&2U)!=0,.starting=(base&4U)!=0,.bluetooth=(base&8U)!=0,
            .standby=atomic_load(&standby),.pairing=atomic_load(&pairing),
            .setup=chime_network_led_setup()};
        /* A host's USB mute must never relabel or stop Bluetooth playback. */
        input.muted=chime_controls_muted()||(input.bluetooth?chime_bluetooth_volume()<=-96*256:chime_usb_muted());
        chime_led_rgb_t rgb=chime_led_render(&led,input,(uint64_t)esp_timer_get_time()/1000U);
        const uint8_t values[]={rgb.r,rgb.g,rgb.b};
        bool ok=true;
        for(unsigned i=0;i<3;++i){
            if(ledc_set_duty(LEDC_LOW_SPEED_MODE,(ledc_channel_t)i,values[i])!=ESP_OK||
               ledc_update_duty(LEDC_LOW_SPEED_MODE,(ledc_channel_t)i)!=ESP_OK)ok=false;
        }
        if(!ok){
            /* Driver-only failure does not take down the audio path. Do not
             * leave a stale healthy light; detach PWM and fall back to red. */
            ESP_LOGE("chime_led","PWM update failed; static red fallback, audio unchanged");
            release_channels(3);board_support_set_rgb(true,false,false);vTaskDelete(NULL);return;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}
esp_err_t chime_indicator_start(void)
{
    if(started)return ESP_ERR_INVALID_STATE;
    ledc_timer_config_t timer={.speed_mode=LEDC_LOW_SPEED_MODE,.duty_resolution=LEDC_TIMER_8_BIT,
        .timer_num=LEDC_TIMER_0,.freq_hz=4000,.clk_cfg=LEDC_AUTO_CLK};
    esp_err_t err=ledc_timer_config(&timer);
    if(err!=ESP_OK)return err;
    unsigned configured=0;
    for(unsigned i=0;i<3;++i){
        ledc_channel_config_t c={.gpio_num=pins[i],.speed_mode=LEDC_LOW_SPEED_MODE,
            .channel=(ledc_channel_t)i,.timer_sel=LEDC_TIMER_0,.duty=0,.hpoint=0,
            .flags={.output_invert=1}}; /* Common anode: logical duty0 -> pin HIGH. */
        err=ledc_channel_config(&c);if(err!=ESP_OK){release_channels(configured);return err;}
        ++configured;
    }
    if(xTaskCreate(worker,"chime_led",2048,NULL,1,NULL)!=pdPASS){release_channels(configured);return ESP_ERR_NO_MEM;}
    started=true;return ESP_OK;
}

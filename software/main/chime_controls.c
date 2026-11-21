#include "chime_controls.h"
#include "chime_input.h"
#include "chime_bluetooth.h"
#include "chime_usb.h"
#include "chime_indicator.h"
#include "board_support.h"
#include "sdkconfig.h"
#include <stdatomic.h>
#ifndef CONFIG_CHIME_ENCODER_REVERSE
#define CONFIG_CHIME_ENCODER_REVERSE 0
#endif
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static atomic_uint flags;
static atomic_bool quitting,running;
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static chime_rotary_t rotary;
static int steps;
static bool started;
bool chime_controls_blocked(void){return atomic_load(&flags)!=0;}
bool chime_controls_muted(void){return (atomic_load(&flags)&CHIME_CONTROL_MUTED)!=0;}
bool chime_controls_standby(void){return (atomic_load(&flags)&CHIME_CONTROL_STANDBY)!=0;}
/* Shared GPIO ISR service is installed by USB. Never uninstall that service.
 * This handler performs no I2C, AVRCP, logging, allocation or audio I/O. */
static void rotary_edge(void *arg)
{
    (void)arg;
    portENTER_CRITICAL_ISR(&mux);
    int delta=chime_rotary_sample(&rotary,gpio_get_level(BOARD_GPIO_ENCODER_A)!=0,
        gpio_get_level(BOARD_GPIO_ENCODER_B)!=0);
    if((delta>0&&steps<32)||(delta<0&&steps> -32))steps+=delta;
    portEXIT_CRITICAL_ISR(&mux);
}
static int take_steps(void)
{
    portENTER_CRITICAL(&mux);int delta=steps;steps=0;portEXIT_CRITICAL(&mux);return delta;
}
static void input_worker(void *arg)
{
    (void)arg;
    chime_button_t button;
    chime_button_init(&button,gpio_get_level(BOARD_GPIO_ENCODER_BUTTON)==0,
        (uint64_t)esp_timer_get_time()/1000U);
    bool previous_bt=chime_bluetooth_connected();
    while(!atomic_load(&quitting)){
        chime_input_event_t event=chime_button_sample(&button,
            gpio_get_level(BOARD_GPIO_ENCODER_BUTTON)==0,(uint64_t)esp_timer_get_time()/1000U);
        if(event!=CHIME_INPUT_NONE){
            unsigned next=chime_input_action(atomic_load(&flags),event);
            atomic_store(&flags,next);
            chime_indicator_set_standby((next&CHIME_CONTROL_STANDBY)!=0);
            ESP_LOGI("chime_input","%s mute=%u standby=%u",event==CHIME_INPUT_HOLD?"hold":"click",
                (next&CHIME_CONTROL_MUTED)!=0,(next&CHIME_CONTROL_STANDBY)!=0);
        }
        bool bt=chime_bluetooth_connected();
        int delta=take_steps();
        /* Do not apply queued movement from the old source to the new one. */
        if(bt!=previous_bt){delta=0;previous_bt=bt;}
#if CONFIG_CHIME_ENCODER_REVERSE
        delta=-delta;
#endif
        if(delta&&!chime_controls_standby()){
            esp_err_t err=bt?chime_bluetooth_adjust_volume(delta*2):chime_usb_adjust_volume(delta);
            if(err!=ESP_OK)ESP_LOGW("chime_input","volume source=%s steps=%d result=%d (not queued)",bt?"bt":"usb",delta,err);
        }
        chime_bluetooth_volume_notify();
        vTaskDelay(pdMS_TO_TICKS(5)>0?pdMS_TO_TICKS(5):1);
    }
    atomic_store(&running,false);vTaskDelete(NULL);
}
esp_err_t chime_controls_start(void)
{
    if(started)return ESP_ERR_INVALID_STATE;
    const gpio_config_t config={.pin_bit_mask=(1ULL<<BOARD_GPIO_ENCODER_A)|
        (1ULL<<BOARD_GPIO_ENCODER_B)|(1ULL<<BOARD_GPIO_ENCODER_BUTTON),
        .mode=GPIO_MODE_INPUT,.pull_up_en=GPIO_PULLUP_ENABLE,
        .pull_down_en=GPIO_PULLDOWN_DISABLE,.intr_type=GPIO_INTR_DISABLE};
    esp_err_t err=gpio_config(&config);if(err!=ESP_OK)return err;
    chime_rotary_init(&rotary,gpio_get_level(BOARD_GPIO_ENCODER_A)!=0,gpio_get_level(BOARD_GPIO_ENCODER_B)!=0);
    steps=0;atomic_store(&flags,0);atomic_store(&quitting,false);
    err=gpio_install_isr_service(0);
    if(err!=ESP_OK&&err!=ESP_ERR_INVALID_STATE)return err;
    err=gpio_isr_handler_add(BOARD_GPIO_ENCODER_A,rotary_edge,NULL);
    if(err!=ESP_OK)return err;
    err=gpio_isr_handler_add(BOARD_GPIO_ENCODER_B,rotary_edge,NULL);
    if(err!=ESP_OK){gpio_isr_handler_remove(BOARD_GPIO_ENCODER_A);return err;}
    err=gpio_set_intr_type(BOARD_GPIO_ENCODER_A,GPIO_INTR_ANYEDGE);
    if(err==ESP_OK)err=gpio_set_intr_type(BOARD_GPIO_ENCODER_B,GPIO_INTR_ANYEDGE);
    if(err==ESP_OK)err=gpio_intr_enable(BOARD_GPIO_ENCODER_A);
    if(err==ESP_OK)err=gpio_intr_enable(BOARD_GPIO_ENCODER_B);
    if(err==ESP_OK){
        atomic_store(&running,true);
        if(xTaskCreate(input_worker,"chime_input",3072,NULL,3,NULL)!=pdPASS){atomic_store(&running,false);err=ESP_ERR_NO_MEM;}
    }
    if(err!=ESP_OK){
        gpio_intr_disable(BOARD_GPIO_ENCODER_A);gpio_intr_disable(BOARD_GPIO_ENCODER_B);
        gpio_isr_handler_remove(BOARD_GPIO_ENCODER_A);gpio_isr_handler_remove(BOARD_GPIO_ENCODER_B);return err;
    }
    started=true;
    ESP_LOGI("chime_input","encoder A=42 B=40 BTN=43; reverse=%u; full-cycle, USB 1dB / BT 2-of-127, hold=1500ms",(unsigned)CONFIG_CHIME_ENCODER_REVERSE);
    return ESP_OK;
}
void chime_controls_stop(void)
{
    if(!started)return;
    gpio_intr_disable(BOARD_GPIO_ENCODER_A);gpio_intr_disable(BOARD_GPIO_ENCODER_B);
    gpio_isr_handler_remove(BOARD_GPIO_ENCODER_A);gpio_isr_handler_remove(BOARD_GPIO_ENCODER_B);
    atomic_store(&quitting,true);
    while(atomic_load(&running))vTaskDelay(1);
    started=false;atomic_store(&flags,0);chime_indicator_set_standby(false);
}

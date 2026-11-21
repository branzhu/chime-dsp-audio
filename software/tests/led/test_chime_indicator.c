#include "chime_indicator.h"
#include "driver/ledc.h"
#include "freertos/task.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
static void (*task)(void *);
static jmp_buf frame;
static unsigned duty[3],configured,released,delays;
static int64_t clock_us;
static int fail_config=-1,fail_task,fail_timer,fail_update,bt_volume=-30*256;
static bool setup,muted,red,physical_muted;
esp_err_t ledc_timer_config(const ledc_timer_config_t *c){assert(c->freq_hz==4000&&c->duty_resolution==8);return fail_timer?ESP_FAIL:ESP_OK;}
esp_err_t ledc_channel_config(const ledc_channel_config_t *c){
    if(c->deconfigure){++released;return ESP_OK;}
    const int pins[]={35,39,37};assert(c->gpio_num==pins[c->channel]&&c->flags.output_invert&&c->duty==0);
    if(c->channel==fail_config)return ESP_FAIL;
    ++configured;return ESP_OK;
}
esp_err_t ledc_set_duty(int mode,ledc_channel_t c,unsigned v){assert(mode==0&&c>=0&&c<3);duty[c]=v;return fail_update?ESP_FAIL:ESP_OK;}
esp_err_t ledc_update_duty(int mode,ledc_channel_t c){assert(mode==0&&c>=0&&c<3);return ESP_OK;}
esp_err_t gpio_set_level(gpio_num_t p,unsigned v){assert((p==35||p==39||p==37)&&v==1);return ESP_OK;}
esp_err_t gpio_set_direction(gpio_num_t p,int m){(void)p;assert(m==GPIO_MODE_OUTPUT);return ESP_OK;}
void board_support_set_rgb(bool r,bool g,bool b){assert(r&&!g&&!b);red=true;}
bool chime_network_led_setup(void){return setup;}
bool chime_usb_muted(void){return muted;}
bool chime_controls_muted(void){return physical_muted;}
int chime_bluetooth_volume(void){return bt_volume;}
int64_t esp_timer_get_time(void){return clock_us;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle){
    (void)name;assert(stack==2048&&arg==NULL&&priority==1&&handle==NULL);task=fn;return fail_task?0:pdPASS;
}
void vTaskDelay(uint32_t ticks){assert(ticks==20);++delays;longjmp(frame,1);}
void vTaskDelete(void *t){assert(t==NULL);longjmp(frame,2);}
static void tick(unsigned ms){clock_us=(int64_t)ms*1000;if(setjmp(frame)==0)task(NULL);}
static void rgb(unsigned r,unsigned g,unsigned b){assert(duty[0]==r&&duty[1]==g&&duty[2]==b);}
int main(int argc,char **argv){
    int scenario=argc>1?atoi(argv[1]):0;
    fail_timer=scenario==1;fail_config=scenario==2?1:-1;fail_task=scenario==3;
    esp_err_t err=chime_indicator_start();
    if(scenario){assert(err!=ESP_OK);assert(released==(scenario==1?0U:scenario==2?1U:3U));puts("PASS LED init rollback");return 0;}
    assert(err==ESP_OK&&configured==3&&chime_indicator_start()==ESP_ERR_INVALID_STATE);
    tick(0);rgb(3,3,3);
    chime_indicator_publish((chime_indicator_state_t){0});tick(10);rgb(24,24,24);
    muted=true;tick(20);rgb(0,0,0);
    chime_indicator_publish((chime_indicator_state_t){.bluetooth=true});tick(30);rgb(0,0,24); /* USB mute isolated */
    physical_muted=true;tick(35);rgb(0,0,0);physical_muted=false;
    bt_volume=-96*256;tick(40);rgb(0,0,0);bt_volume=-30*256;
    setup=true;tick(50);rgb(24,24,0);
    chime_indicator_set_standby(true);tick(60);rgb(0,0,0);
    chime_indicator_fail();tick(70);rgb(48,0,0);
    chime_indicator_publish((chime_indicator_state_t){.bluetooth=true});tick(80);rgb(48,0,0); /* fatal cannot be overwritten */
    fail_update=1;tick(90);assert(red&&released==3&&delays==10);
    puts("PASS LED PWM inversion, worker priority, live input/mute isolation, terminal fault, driver failure fallback");
}

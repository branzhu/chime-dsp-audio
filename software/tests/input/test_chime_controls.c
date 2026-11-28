#include "chime_controls.h"
#include "board_support.h"
#include "freertos/task.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
static void (*task)(void *),(*handlers[64])(void *);
static bool levels[64],bt_connected,standby_led;
static unsigned installed,adds,removes,enabled,disabled,depth,tick;
static int failure,usb_steps,bt_steps;
static int64_t clock_us;
static jmp_buf finished;
static int direction;
void input_test_enter(portMUX_TYPE *mux){(void)mux;assert(depth++==0);}
void input_test_exit(portMUX_TYPE *mux){(void)mux;assert(--depth==0);}
int gpio_get_level(int pin){return levels[pin]?1:0;}
esp_err_t gpio_config(const gpio_config_t *c){assert(c->pin_bit_mask==((1ULL<<42)|(1ULL<<40)|(1ULL<<43)));assert(c->pull_up_en==GPIO_PULLUP_ENABLE&&c->intr_type==GPIO_INTR_DISABLE);return failure==1?ESP_FAIL:ESP_OK;}
esp_err_t gpio_install_isr_service(int flags){assert(!flags);++installed;return ESP_ERR_INVALID_STATE;} /* USB owns service */
esp_err_t gpio_isr_handler_add(int pin,void (*handler)(void *),void *arg){assert(!arg&&(pin==42||pin==40));++adds;if(failure==2&&pin==40)return ESP_FAIL;handlers[pin]=handler;return ESP_OK;}
esp_err_t gpio_isr_handler_remove(int pin){assert(pin==42||pin==40);handlers[pin]=NULL;++removes;return ESP_OK;}
esp_err_t gpio_set_intr_type(int pin,int type){assert((pin==42||pin==40)&&type==GPIO_INTR_ANYEDGE);return ESP_OK;}
esp_err_t gpio_intr_enable(int pin){assert(pin==42||pin==40);++enabled;return ESP_OK;}
esp_err_t gpio_intr_disable(int pin){assert(pin==42||pin==40);++disabled;return ESP_OK;}
int xTaskCreate(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle){assert(!strcmp(name,"chime_input")&&stack==3072&&!arg&&priority==3&&!handle);task=fn;return failure==3?0:pdPASS;}
void chime_indicator_set_standby(bool active){standby_led=active;}
bool chime_bluetooth_connected(void){return bt_connected;}
esp_err_t chime_bluetooth_adjust_volume(int steps){assert(!depth);bt_steps+=steps;return ESP_OK;}
void chime_bluetooth_volume_notify(void){assert(!depth);}
void chime_usb_hid_poll(void){assert(!depth);}
esp_err_t chime_usb_adjust_volume(int steps){assert(!depth);usb_steps+=steps;return ESP_OK;}
int64_t esp_timer_get_time(void){return clock_us;}
static void state(unsigned ab){levels[42]=(ab&2U)!=0;levels[40]=(ab&1U)!=0;assert(handlers[42]);handlers[42](NULL);}
static void turn(void){state(1);state(0);state(2);state(3);}
/* Production task runs through all iterations on one stack; only RTOS wait
 * and hardware/transport calls are mocked. No radio or real timing claimed. */
#include "../../main/chime_controls.c"
void vTaskDelete(void *t){assert(!t);longjmp(finished,1);}
void vTaskDelay(uint32_t ticks)
{
    assert(ticks==5&&!depth);
    switch(tick++){
    case 0: turn();clock_us=5000;break;
    case 1: assert(usb_steps==(int)direction);levels[43]=false;clock_us=10000;break;
    case 2: clock_us=30000;break; /* debounced press */
    case 3: levels[43]=true;clock_us=50000;break;
    case 4: clock_us=70000;break;
    case 5: assert(chime_controls_muted()&&chime_controls_blocked());turn();clock_us=75000;break;
    case 6: assert(usb_steps==2*(int)direction&&chime_controls_muted());levels[43]=false;clock_us=100000;break;
    case 7: clock_us=120000;break;
    case 8: clock_us=1620000;break;
    case 9: assert(chime_controls_standby()&&standby_led);turn();clock_us=1625000;break;
    case 10: assert(usb_steps==2*(int)direction);levels[43]=true;clock_us=1630000;break;
    case 11: clock_us=1650000;break;
    case 12: assert(chime_controls_standby());levels[43]=false;clock_us=1700000;break;
    case 13: clock_us=1720000;break;
    case 14: clock_us=3220000;break;
    case 15: assert(!chime_controls_standby()&&!standby_led&&chime_controls_muted());bt_connected=true;turn();clock_us=3225000;break;
    case 16: assert(!bt_steps&&usb_steps==3*direction);turn();clock_us=3230000;break; /* queued old-source step discarded */
    case 17: assert(!bt_steps&&usb_steps==4*direction);atomic_store(&quitting,true);break;
    default: assert(0);
    }
}
int main(void)
{
#if CONFIG_CHIME_ENCODER_REVERSE
    direction=-1;
#else
    direction=1;
#endif
    levels[42]=levels[40]=levels[43]=true;
    failure=1;assert(chime_controls_start()==ESP_FAIL&&!adds);
    failure=2;assert(chime_controls_start()==ESP_FAIL&&!handlers[42]&&!handlers[40]);
    failure=3;assert(chime_controls_start()==ESP_ERR_NO_MEM&&!handlers[42]&&!handlers[40]);
    failure=0;assert(chime_controls_start()==ESP_OK&&enabled==4);
    assert(chime_controls_start()==ESP_ERR_INVALID_STATE);
    if(setjmp(finished)==0)task(NULL);
    assert(!atomic_load(&running));chime_controls_stop();chime_controls_stop();
    assert(!handlers[42]&&!handlers[40]&&!chime_controls_blocked()&&!standby_led&&installed==3&&disabled==4&&removes==5);
    puts("PASS production GPIO/ISR/task startup rollback, reversal config, PC-only HID steps, mute/standby/wake and task teardown");
    return 0;
}

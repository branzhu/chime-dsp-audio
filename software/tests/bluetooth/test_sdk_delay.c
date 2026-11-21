/* Execute actual generated gate AND pinned BTA runtime-report dispatch. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef uint16_t UINT16;
enum {ESP_A2D_SET_INVALID_PARAMS=1,ESP_A2D_SNK_SET_DELAY_VALUE_EVT=2,BTA_AV_OPEN_SST=3,AVDT_SUCCESS=0,BTA_AV_FAIL=1,BTA_AV_SET_DELAY_VALUE_EVT=4};
typedef struct {struct {unsigned delay_value,set_state;} a2d_set_delay_value_stat;} esp_a2d_cb_param_t;
typedef struct {UINT16 delay_value;unsigned status;} tBTA_AV_DELAY;
typedef struct {struct {UINT16 delay_value;} api_set_delay_vlaue;} tBTA_AV_DATA;
typedef struct {unsigned state,avdt_handle;} tBTA_AV_SCB;
typedef union {tBTA_AV_DELAY delay;} tBTA_AV;
static unsigned invalid,queued,reports,completions;static UINT16 current;static int report_result;
static tBTA_AV_SCB stream={BTA_AV_OPEN_SST,9};
static void completed(unsigned event,tBTA_AV *data){assert(event==BTA_AV_SET_DELAY_VALUE_EVT);assert(data->delay.delay_value==current);++completions;assert(data->delay.status==(unsigned)(report_result?BTA_AV_FAIL:0));}
static struct {void (*p_cback)(unsigned,tBTA_AV *);} bta_av_cb={completed};
static void AVDT_SetDelayValue(UINT16 v){current=v;}
static int AVDT_DelayReport(unsigned h,unsigned id,UINT16 v){assert(h==9&&!id&&v==current);++reports;return report_result;}
#include "bt-send-delay.inc"
static void BTA_SetDelayValue(UINT16 v){++queued;tBTA_AV_DATA data={{v}};bta_av_set_delay_value(&stream,&data);}
static void btc_a2d_cb_to_app(unsigned event,esp_a2d_cb_param_t *p){assert(event==ESP_A2D_SNK_SET_DELAY_VALUE_EVT&&!p->a2d_set_delay_value_stat.delay_value&&p->a2d_set_delay_value_stat.set_state==ESP_A2D_SET_INVALID_PARAMS);++invalid;}
#if DELAY_RECOVERY
#include "bt-set-delay-recovery.inc"
#else
#include "bt-set-delay-vendor.inc"
#endif
int main(void){
    btc_a2d_sink_set_delay_value(0);assert(invalid==1&&!queued&&!reports);
    const UINT16 values[]={1,480,960,1199,1200,1201,65535};
    for(unsigned i=0;i<sizeof(values)/sizeof(*values);++i){btc_a2d_sink_set_delay_value(values[i]);assert(current==values[i]&&queued==i+1&&reports==queued&&completions==queued);}
    unsigned sent=reports;stream.state=0;btc_a2d_sink_set_delay_value(1000);assert(reports==sent&&current==1000);
    stream.state=BTA_AV_OPEN_SST;report_result=1;btc_a2d_sink_set_delay_value(1100);assert(reports==sent+1);
    puts("PASS actual SDK delay gate/dispatch: sub-120ms, full uint16 range, zero reject, live update, closed-stream store, dispatch failure (no peer ACK simulation)");return 0;
}

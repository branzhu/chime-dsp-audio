/* Executes extracted, unmodified production owner callbacks. Full RTOS/socket
 * loop is built on target, not simulated here; on-board acceptance is required. */
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include "bench_sound.h"
#include "bench_dsp.h"
typedef int board_context_t;
#define ESP_ERR_NOT_SUPPORTED 0x106
#define MSG_PEEK 1
#define MSG_DONTWAIT 2
#define ESP_LOGI(...) ((void)0)
static unsigned stops,writes,records,publishes,invalidations;
static int last_record,executor_result,peek_result=-1;
static bool bt_connected,bt_wanted,controls_blocked;
static bool chime_controls_blocked(void){return controls_blocked;}
static uint32_t bt_epoch,usb_epoch;
static bool amps,stop_pending,tuning_queued,output_pending,usb_wanted=true,network_ready=true;
static uint32_t network_epoch=1;
static uint64_t clock_now=100;
static uint64_t now_us(void *p){(void)p;return clock_now;}
static bool chime_network_ready(void){return network_ready;}
static uint32_t chime_network_epoch(void){return network_epoch;}
static bool chime_usb_wanted(void){return usb_wanted;}
static uint32_t chime_usb_epoch(void){return usb_epoch;}
static bool chime_bluetooth_connected(void){return bt_connected;}
static bool chime_bluetooth_wanted(void){return bt_wanted;}
static uint32_t chime_bluetooth_epoch(void){return bt_epoch;}
static bool bench_web_sound_stop_pending(void){return stop_pending;}
static bool bench_web_tuning_queued(void){return tuning_queued;}
static bool bench_web_output_change_pending(void){return output_pending;}
static int recv(int socket,void *out,int count,int flags){(void)socket;(void)out;(void)count;(void)flags;errno=EAGAIN;return peek_result;}
void bench_sound_cancel(bench_sound_t *s,int reason){(void)reason;++stops;amps=false;s->active=false;}
static void bench_web_tuning_revoke(bool external,int reason){assert(external);(void)reason;++publishes;tuning_queued=false;}
static void sound_publish(const bench_sound_t *s){assert(!s->active);}
static void bench_web_dsp_invalidate(void){++invalidations;}
static void bench_web_record_request(int result){++records;last_record=result;}
int bench_dsp_execute(bench_dsp_t *dsp,const sigma_request_t *r,uint8_t *data){
    (void)dsp;(void)data;
    if(r->control==SIGMA_CONTROL_WRITE){assert(!amps&&stops>0);++writes;}
    return executor_result;
}
#include "ss_owner_callbacks.inc"
static bench_session_t make(bench_sound_t *sound){
    stops=writes=records=publishes=invalidations=0;last_record=executor_result=0;
    amps=true;stop_pending=tuning_queued=output_pending=false;usb_wanted=network_ready=true;
    network_epoch=1;clock_now=100;peek_result=-1;sound->active=true;
    return (bench_session_t){.socket=3,.network_epoch=1,.external=true,.usb_mode=true,.tuning_armed=7,.sound=sound};
}
int main(void){
    bench_sound_t sound={0};uint8_t data[4]={1,0,0,0},reply[4];
    sigma_request_t request={.control=SIGMA_CONTROL_WRITE,.address=0x120,.data_length=4,.data=data};
    bench_session_t session=make(&sound);
    assert(execute(&session,&request,reply)==0);
    assert(!amps&&!sound.active&&session.tuning_armed==0&&stops==1&&writes==1&&invalidations==1&&records==1&&session.request_deadline==0);
    session=make(&sound);executor_result=-99;
    assert(execute(&session,&request,reply)==-99&&!amps&&session.tuning_armed==0&&last_record==-99);
    session=make(&sound);request.flag=1;
    assert(execute(&session,&request,reply)==ESP_ERR_NOT_SUPPORTED&&!amps&&session.tuning_armed==0&&writes==0&&last_record==ESP_ERR_NOT_SUPPORTED);
    session=make(&sound);request.flag=0;request.control=SIGMA_CONTROL_READ;
    assert(execute(&session,&request,reply)==0&&amps&&sound.active&&session.tuning_armed==7&&stops==0&&publishes==0&&invalidations==0);
    session=make(&sound);peek_result=10;
    assert(!sound_cancelled(&session)&&!ss_idle(&session)); /* reads handled without preemptive stop */
    controls_blocked=true;assert(sound_cancelled(&session));controls_blocked=false;
    stop_pending=true;assert(sound_cancelled(&session));stop_pending=false;
    tuning_queued=true;assert(sound_cancelled(&session));tuning_queued=false;
    usb_wanted=false;assert(sound_cancelled(&session));usb_wanted=true;
    network_ready=false;assert(sound_cancelled(&session));network_ready=true;
    ++network_epoch;assert(sound_cancelled(&session));--network_epoch;
    session.request_deadline=clock_now;assert(sound_cancelled(&session));session.request_deadline=0;
    session.socket=-1;assert(ss_idle(&session)&&!sound_cancelled(&session));
    output_pending=true;assert(sound_cancelled(&session)&&session.output_interrupted_usb);output_pending=false;
    output_pending=false;session.external=false;session.bluetooth_mode=true;
    bt_connected=bt_wanted=true;usb_wanted=false;session.stream_epoch=bt_epoch;
    assert(!sound_cancelled(&session)); /* USB events cannot stop BT */
    ++usb_epoch;assert(!sound_cancelled(&session));
    ++bt_epoch;assert(sound_cancelled(&session));session.stream_epoch=bt_epoch;
    bt_wanted=false;assert(sound_cancelled(&session));bt_wanted=true;
    bt_connected=false;assert(sound_cancelled(&session));bt_connected=true;
    session.bluetooth_mode=false;usb_wanted=true;session.stream_epoch=usb_epoch;
    assert(sound_cancelled(&session)); /* connection cancels normal USB */
    session.external=true;assert(!sound_cancelled(&session)); /* explicit SS USB exception */
    revoke_tuning(&session,0);assert(session.tuning_armed==0&&!amps&&!sound.active);
    puts("PASS actual owner callbacks: writes stop amps/revoke before executor, errors and unknown Safeload fail silent; reads preserve audition; STOP/USB/network/queued command cancellation and idle checks");
    return 0;
}

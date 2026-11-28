#include "chime_render.h"
#include "chime_usb.h"
#include "chime_bluetooth.h"
#include "chime_mixer.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
void render_test_request_stop(void);
void chime_bt_src_accel_init(void){assert(!"PIE probe must not run on the PCM worker");}
void chime_usb_test_log(const char *tag,const char *format,...){(void)tag;(void)format;}
int xPortGetCoreID(void){return 0;}
static void (*task_fn)(void *);
static bool uw=true,bw=true,muted,standby,um,bt_active,bready=true,malformed,stop_in_src;
static uint32_t usb_gen=1,bt_gen=1;static uint16_t fifo_available=4096;
static volatile bool yield_worker=true;
static bool in_block,finished_block;static unsigned delays,src_cost,write_cost,remaining;
static unsigned depth,critical,writes,bt_begins,bt_ends,usb_reads,bt_reads;
static int error;static int64_t now;static int32_t last[256];static jmp_buf done;
void usb_test_enter_critical(portMUX_TYPE *m){assert(!critical&&!*m);*m=1;++critical;}
void usb_test_exit_critical(portMUX_TYPE *m){assert(critical==1&&*m);*m=0;--critical;}
SemaphoreHandle_t xSemaphoreCreateMutex(void){return &depth;}
void vSemaphoreDelete(SemaphoreHandle_t h){assert(h==&depth);}
int xSemaphoreTake(SemaphoreHandle_t h,uint32_t t){
 assert(h==&depth&&t==portMAX_DELAY&&!depth);
 if(in_block&&finished_block){if(!--remaining)longjmp(done,1);now+=2667;finished_block=false;}
 ++depth;return 1;}
int xSemaphoreGive(SemaphoreHandle_t h){assert(h==&depth&&depth==1);--depth;if(in_block)finished_block=true;else if(yield_worker)longjmp(done,1);return 1;}
int xTaskCreatePinnedToCore(void (*fn)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle,int core)
{assert(!strcmp(name,"source_pcm")&&!arg&&!handle&&core==0&&stack==8192&&priority==6);task_fn=fn;return 1;}
void vTaskDelay(uint32_t n){assert(!depth&&n);++delays;}
void vTaskDelete(void *arg){(void)arg;}
int64_t esp_timer_get_time(void){return now;}
bool chime_usb_wanted(void){return uw;}
bool chime_usb_muted(void){return um;}
int chime_usb_volume(void){return 0;}
uint32_t chime_usb_epoch(void){return usb_gen;}
uint16_t chime_usb_fifo(void){return uw?fifo_available:0;}
uint16_t chime_usb_read(void *pcm,uint16_t n){++usb_reads;uint8_t *p=pcm;for(unsigned i=0;i<n;i+=2){p[i]=0;p[i+1]=32;}return malformed?3:n;}
void chime_usb_clear(void){}
void chime_usb_fail(esp_err_t e){assert(e);uw=false;}
bool chime_bluetooth_wanted(void){return bw;}
uint32_t chime_bluetooth_epoch(void){return bt_gen;}
int chime_bluetooth_volume(void){return 0;}
esp_err_t chime_bluetooth_output_begin(void){bt_active=true;++bt_begins;return 0;}
void chime_bluetooth_output_end(void){bt_active=false;++bt_ends;}
esp_err_t chime_bluetooth_render_q27(int32_t *out,size_t frames,bool *ready)
{assert(bt_active&&frames==128&&!critical);++bt_reads;now+=src_cost;
 chime_render_stats_t inflight;chime_render_snapshot(&inflight);
 assert(inflight.src_active&&inflight.src_inflight_us==src_cost);
 for(unsigned i=0;i<256;++i)out[i]=CHIME_PCM_ONE/4;*ready=bready;
 if(stop_in_src){render_test_request_stop();}return 0;}
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
static unsigned clock_resets,clock_updates;static int clock_ppm,trim_error;
static bool clock_available=true,admitted_follow;static unsigned clock_bt_rate=48000;
bool bench_audio_clock_available(void){return clock_available;}
esp_err_t bench_audio_clock_set_ppm(int ppm){assert(depth==1);clock_ppm=ppm;++clock_updates;return trim_error;}
esp_err_t bench_audio_clock_reset(void){assert(depth==1);clock_ppm=0;++clock_resets;return 0;}
uint32_t bench_audio_clock_rate_q16(void){return 48000U*65536U;}
esp_err_t chime_bluetooth_output_begin_clocked(bool follow){admitted_follow=follow;return chime_bluetooth_output_begin();}
void chime_bluetooth_snapshot(chime_bluetooth_status_t *s){memset(s,0,sizeof(*s));
 s->codec=CHIME_BT_CODEC_LDAC;s->rate=clock_bt_rate;s->channels=2;s->epoch=bt_gen;
 s->clock_follow=admitted_follow;s->output_active=bt_active;s->primed=bready;s->queued_frames=4608;s->target_frames=4608;}
#endif
bool chime_controls_muted(void){return muted;}
bool chime_controls_standby(void){return standby;}
esp_err_t bench_audio_pcm_write(const int32_t *out,size_t frames){assert(depth==1&&!critical&&frames==128);memcpy(last,out,sizeof(last));++writes;now+=write_cost;return error;}
/* Include production renderer to inject STOP inside an in-flight SRC without an RTOS. */
#include "../../main/chime_render.c"
void render_test_request_stop(void){atomic_store(&stop_requested,true);}
static void block_count(unsigned count){now+=2667;in_block=true;finished_block=false;remaining=count;
 if(setjmp(done)==0)task_fn(NULL);in_block=false;assert(!depth&&!critical);}
static void block(void){block_count(1);}
static void begin(bool only){/* owner give also uses our deterministic yield */
 if(setjmp(done)==0)assert(chime_render_begin(only)==0);}
int main(void)
{
    assert(chime_render_start()==0);begin(false);
    for(unsigned i=0;i<8;++i)block();assert(usb_reads&&bt_reads&&last[255]==1073741824&&bt_begins==1);
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    assert(admitted_follow&&clock_updates);
    unsigned updates=clock_updates;
    clock_available=false;++bt_gen;block();assert(!admitted_follow&&clock_updates==updates&&clock_ppm==0);
    clock_available=true;clock_bt_rate=44100;++bt_gen;block();assert(!admitted_follow&&clock_updates==updates);
    clock_bt_rate=48000;++bt_gen;block();assert(admitted_follow&&clock_updates>updates);
    bt_ends=0;bt_begins=1; /* original lifecycle assertions below start here */
#endif
    muted=true;for(unsigned i=0;i<4;++i)block();assert(last[255]==0);unsigned r=usb_reads;block();assert(usb_reads>r);
    muted=false;for(unsigned i=0;i<4;++i)block();assert(last[255]==1073741824);
    bw=false;++bt_gen;for(unsigned i=0;i<4;++i)block();assert(last[255]==536870912&&bt_ends==1);
    bw=true;++bt_gen;for(unsigned i=0;i<4;++i)block();assert(last[255]==1073741824&&bt_begins==2);
    malformed=true;block();malformed=false;assert(!uw);for(unsigned i=0;i<4;++i)block();assert(last[255]==536870912);
    uw=true;++usb_gen;for(unsigned i=0;i<4;++i)block();assert(last[255]==1073741824);
    um=true;for(unsigned i=0;i<4;++i)block();assert(last[255]==536870912&&uw);
    um=false;bready=false;for(unsigned i=0;i<4;++i)block();assert(last[255]==536870912);
    bready=true;
    /* A slow FIR must not trigger an extra 1 ms sleep after DMA pacing. */
    chime_render_stats_t before,slow;chime_render_snapshot(&before);
    unsigned d=delays;src_cost=4000;write_cost=800;block_count(2);
    chime_render_snapshot(&slow);
    assert(delays==d&&slow.deadline_misses==before.deadline_misses+2);
    assert(slow.src_blocks==before.src_blocks+2&&slow.src_total_us==before.src_total_us+8000);
    assert(!slow.src_active&&!slow.src_inflight_us);
    assert(slow.last_src_us==4000&&slow.last_compute_us==4000&&slow.last_write_us==800);
    assert(slow.max_src_us>=4000&&slow.max_write_us>=800);
    src_cost=write_cost=0;
    assert(slow.last_period_us==7467&&slow.max_period_us>=7467);
    /* Sustained unpaced overload still lets the low-priority STOP owner run,
     * but must not sleep on every over-budget block. */
    d=delays;src_cost=4000;block_count(8);src_cost=0;
    chime_render_snapshot(&slow);assert(delays>d&&delays-d<8&&slow.unpaced_yields>0);
    stop_in_src=true;unsigned w=writes;block();assert(writes==w);stop_in_src=false;
    begin(true);r=bt_reads;for(unsigned i=0;i<4;++i)block();assert(bt_reads==r&&last[255]==536870912);
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    begin(false);trim_error=-88;unsigned before_error=writes;block();trim_error=0;
    bool fault_finished;assert(writes==before_error&&chime_render_poll(&fault_finished)==-88&&clock_ppm==0);
    begin(true);
#endif
    error=-99;block();bool finished;assert(chime_render_poll(&finished)==-99);
    chime_render_stats_t snapshot;chime_render_snapshot(&snapshot);assert(snapshot.blocks&&!snapshot.clips);
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    assert(clock_resets>=2&&clock_updates>0&&clock_ppm==0);
#endif
    puts("PASS production renderer: CPU0 affinity, no PIE probe/migration, simultaneous sources, mute consumes both, independent epochs/failures, no boost, external USB isolation, STOP during FIR, deadline miss without extra sleep, stage timings, I2S fault");return 0;
}


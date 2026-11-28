/* Control/scheduling model only: no codec or RTOS timing acceptance. The real
 * replay worker C unit is included so tests exercise its cleanup/deadlines. */
#include "../../main/chime_ldac_replay.c"
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
static packet_t items[AHEAD_PACKETS];static unsigned head,count,depth,allocated,alloc_calls,fail_alloc;
static unsigned submitted,ended,task_calls;static uint64_t clock_us,until;
static uint32_t generation;static bool simulated,rendering=true,task_fail,queue_fail;
static int bt_error;static jmp_buf escape;
#if CONFIG_CHIME_LDAC_REPLAY_FLASH
static uint64_t stop_at;
#endif
static uint64_t submit_times[128];static uint32_t submit_offsets[128];
void usb_test_enter_critical(portMUX_TYPE *m){assert(!depth&&!*m);++depth;*m=1;}
void usb_test_exit_critical(portMUX_TYPE *m){assert(depth==1&&*m==1);--depth;*m=0;}
int64_t esp_timer_get_time(void){return (int64_t)clock_us;}
static void advance(unsigned ms){clock_us+=(uint64_t)ms*1000;
#if CONFIG_CHIME_LDAC_REPLAY_FLASH
    if(stop_at&&clock_us>=stop_at){stop_at=0;chime_ldac_replay_stop();}
#endif
    if(clock_us>=until){assert(!allocated);longjmp(escape,1);}
}
void vTaskDelay(uint32_t ticks){assert(!depth&&ticks);advance(ticks);}
QueueHandle_t xQueueCreate(unsigned length,unsigned size){assert(length==AHEAD_PACKETS&&size==sizeof(packet_t));return queue_fail?NULL:items;}
void vQueueDelete(QueueHandle_t q){assert(q==items);}
unsigned uxQueueMessagesWaiting(QueueHandle_t q){assert(q==items);return count;}
int xQueueSend(QueueHandle_t q,const void *p,uint32_t wait){assert(q==items&&!wait&&!depth);if(count==AHEAD_PACKETS)return 0;items[(head+count++)%AHEAD_PACKETS]=*(const packet_t *)p;return pdTRUE;}
int xQueueReceive(QueueHandle_t q,void *p,uint32_t wait){assert(q==items&&!depth);if(count){*(packet_t *)p=items[head];head=(head+1)%AHEAD_PACKETS;--count;return pdTRUE;}advance(wait);return 0;}
int xTaskCreatePinnedToCore(void (*f)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle,int core){
    assert(f==worker&&name&&stack==4096&&!arg&&priority==7&&!handle&&core==0);++task_calls;return task_fail?0:pdPASS;}
esp_a2d_audio_buff_t *esp_a2d_audio_buff_alloc(uint16_t bytes){
    assert(!depth);if(++alloc_calls==fail_alloc)return NULL;
    esp_a2d_audio_buff_t *b=calloc(1,sizeof(*b));assert(b);b->data=malloc(bytes);assert(b->data);b->buff_size=bytes;++allocated;return b;}
void esp_a2d_audio_buff_free(esp_a2d_audio_buff_t *b){assert(!depth&&b&&allocated);free(b->data);free(b);--allocated;}
esp_err_t chime_bluetooth_replay_begin(unsigned rate,uint32_t *epoch){assert(!depth&&(rate==48000||rate==44100));if(simulated)return ESP_ERR_INVALID_STATE;simulated=true;bt_error=0;*epoch=++generation;return ESP_OK;}
void chime_bluetooth_replay_end(uint32_t epoch,esp_err_t error){assert(!depth&&epoch==generation);simulated=false;bt_error=error;++ended;}
void chime_bluetooth_snapshot(chime_bluetooth_status_t *s){assert(!depth);memset(s,0,sizeof(*s));s->simulated=simulated;s->epoch=generation;s->error=bt_error;}
bool chime_bluetooth_replay_submit(void *owned,uint32_t epoch){
    esp_a2d_audio_buff_t *b=owned;assert(!depth&&b->data[0]==b->number_frame);
    bool ok=simulated&&rendering&&epoch==generation;
    if(ok){assert(submitted<128);submit_times[submitted]=clock_us;submit_offsets[submitted++]=b->timestamp;}
    esp_a2d_audio_buff_free(b);return ok;}
static void run(uint64_t end){until=end;if(!setjmp(escape))worker(NULL);assert(!depth);}
static void stop_drain(void){chime_ldac_replay_stop();run(clock_us+10000);assert(!count&&!allocated&&!state.active);}
static void push(unsigned packets,unsigned nf){
    uint8_t data[4+6*8]={0};uint32_t at=state.uploaded_frames;
    for(unsigned i=0;i<4;++i)data[i]=(uint8_t)(at>>(i*8));
    for(unsigned i=0;i<packets;++i){data[4+6*i]=3;data[6+6*i]=(uint8_t)nf;data[7+6*i]=0xaa;data[8+6*i]=0xbb;data[9+6*i]=0xcc;}
    assert(chime_ldac_replay_push(data,4+6*packets)==ESP_OK);
}
#if CONFIG_CHIME_LDAC_REPLAY_FLASH
static const uint8_t tiny_fixture[]={
    12,0,3,0xaa,0x30,0,1,0xaa,0x30,0,2,0xaa,0x30,0,3,
    12,0,3,0xaa,0x30,0,4,0xaa,0x30,0,5,0xaa,0x30,0,6};
static bool prepare_fail;
esp_err_t chime_ldac_fixture_prepare(chime_ldac_fixture_t *out){
    if(prepare_fail)return ESP_ERR_INVALID_ARG;
    *out=(chime_ldac_fixture_t){.payload=tiny_fixture,.length=sizeof(tiny_fixture),.rate=48000,.cycle_frames=768,.packets=2};return ESP_OK;
}
size_t chime_ldac_frame_bytes(const uint8_t *p,size_t length,unsigned rate){assert(rate==48000&&length>=4&&p[0]==0xaa);return 4;}
#endif
int main(void){
    assert(chime_ldac_replay_begin(96000,70,0)==ESP_ERR_INVALID_ARG);
    assert(chime_ldac_replay_begin(48000,60,0)==ESP_ERR_INVALID_ARG);
    assert(chime_ldac_replay_begin(48000,70,2001)==ESP_ERR_INVALID_ARG);
    queue_fail=true;assert(chime_ldac_replay_begin(48000,70,0)==ESP_ERR_NO_MEM);queue_fail=false;
    task_fail=true;assert(chime_ldac_replay_begin(48000,70,0)==ESP_ERR_NO_MEM);task_fail=false;
    assert(chime_ldac_replay_begin(48000,70,0)==ESP_OK&&task_calls==2);
    assert(chime_ldac_replay_begin(48000,70,0)==ESP_ERR_INVALID_STATE);
    uint64_t base=state.start_us;push(8,15);run(base+301000);
    assert(submitted==8&&state.submitted_frames==8*15*128&&allocated==0);
    for(unsigned i=0;i<8;++i){uint64_t due=base+chime_replay_offset_us(submit_offsets[i],48000,0);assert(submit_times[i]>=due&&submit_times[i]<=due+1000);}
    stop_drain();assert(chime_ldac_replay_begin(44100,610,-1000)==ESP_OK);base=state.start_us;
    push(1,15);run(base+1000);assert(state.submitted_frames==1920);stop_drain();
    assert(chime_ldac_replay_begin(48000,70,0)==ESP_OK);base=state.start_us;run(base+30000);
    assert(!state.active&&state.error==ESP_ERR_TIMEOUT&&state.failure_reason==CHIME_REPLAY_FAILURE_INPUT_STARVED); /* no input: no demand-paced forgiveness */
    assert(chime_ldac_replay_begin(48000,70,0)==ESP_OK);push(1,1);clock_us=state.start_us+25000;
    unsigned old=submitted;run(clock_us+10000);assert(!state.active&&state.max_late_us==25000&&state.failure_reason==CHIME_REPLAY_FAILURE_FEEDER_LATE&&submitted==old&&!allocated);
    assert(chime_ldac_replay_begin(48000,70,0)==ESP_OK);push(2,1);stop_drain();assert(submitted==old);
    assert(chime_ldac_replay_begin(48000,70,0)==ESP_OK);fail_alloc=alloc_calls+2;
    uint8_t badseq[10]={1,0,0,0,3,0,1,0xaa,0xbb,0xcc};
    assert(chime_ldac_replay_push(badseq,sizeof(badseq))==ESP_ERR_INVALID_ARG&&!state.active);stop_drain();
    assert(chime_ldac_replay_begin(48000,70,0)==ESP_OK);
    uint8_t two[16]={0,0,0,0,3,0,1,0xaa,0xbb,0xcc,3,0,1,0xaa,0xbb,0xcc};
    assert(chime_ldac_replay_push(two,sizeof(two))==ESP_ERR_NO_MEM&&!state.active);stop_drain();fail_alloc=0;
    assert(chime_ldac_replay_begin(48000,70,0)==ESP_OK);rendering=false;push(1,1);run(state.start_us+10000);
    assert(!state.active&&state.error==ESP_ERR_INVALID_STATE&&!allocated);rendering=true;
#if CONFIG_CHIME_LDAC_REPLAY_FLASH
    assert(chime_ldac_replay_begin_flash(70,0)==ESP_ERR_INVALID_STATE);
    prepare_fail=true;assert(chime_ldac_replay_prepare_flash()==ESP_ERR_INVALID_ARG);prepare_fail=false;
    assert(chime_ldac_replay_prepare_flash()==ESP_OK&&state.flash_ready&&state.fixture_frames==768);
    assert(chime_ldac_replay_begin_flash(70,0)==ESP_OK&&state.preloaded);
    assert(chime_ldac_replay_prepare_flash()==ESP_ERR_INVALID_STATE);
    assert(chime_ldac_replay_push(two,sizeof(two))==ESP_ERR_INVALID_STATE); /* no transport mixing */
    base=state.start_us;old=submitted;stop_at=base+25000;run(base+40000);
    assert(submitted==old+4&&state.submitted_frames==1536&&state.completed_cycles==2&&!allocated&&!state.active);
    for(unsigned i=old;i<submitted;++i)assert(submit_offsets[i]==(i-old)*384);
    assert(chime_ldac_replay_begin_flash(70,1000)==ESP_OK);base=state.start_us;old=submitted;
    stop_at=base+17000;run(base+30000);assert(submitted==old+3&&state.submitted_frames==1152&&!allocated);
    assert(submit_offsets[old]==0); /* fresh session resets fixture cursor/clock */
    puts("PASS preloaded replay worker model: no HTTP input, seam cursor wrap, independent deadlines, restart/ppm, STOP frees held buffer; not real fixture seam/RTOS proof");
#endif
    puts("PASS actual replay worker model: independent deadlines, 44.1/48k, ppm, STOP/stale cleanup, late/missing input, allocation failure, owner rejection; no RF/RTOS acceptance");return 0;
}


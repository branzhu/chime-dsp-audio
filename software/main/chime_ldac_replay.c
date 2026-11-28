#include "sdkconfig.h"
#include "chime_ldac_replay.h"
#include "chime_ldac_replay_format.h"
#include "chime_bluetooth.h"
#include "esp_a2dp_api.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include <string.h>
#if CONFIG_CHIME_LDAC_REPLAY_FLASH
#include "chime_ldac_fixture.h"
#include "chime_ldac.h"
static chime_ldac_fixture_t fixture;
#endif
/* Bounded transport jitter reservoir, separate from the production raw queue.
 * The short CPU0/p7 release task models the source-side callback independently
 * of CPU1/p5 decoding. A lower-priority same-core task would be decoder-paced
 * under load, invalidating this experiment even with a full ingress queue.
 * HTTP supplies future packets; consumer/PCM fill NEVER controls their due time.
 * No busy spin, SPI/flash writes, new gain knob, or DSP/amp ownership bypass. */
#define AHEAD_PACKETS 32U
#define LATE_LIMIT_US 20000U
static portMUX_TYPE mux=portMUX_INITIALIZER_UNLOCKED;
static chime_ldac_replay_status_t state;
static QueueHandle_t ingress;
typedef struct {esp_a2d_audio_buff_t *buffer;uint32_t epoch,offset;} packet_t;
static void finish(uint32_t epoch,esp_err_t error,chime_ldac_replay_failure_t reason)
{
    bool owned=false;
    portENTER_CRITICAL(&mux);
    if(state.active&&state.epoch==epoch){state.active=false;state.error=error;state.failure_reason=reason;owned=true;}
    portEXIT_CRITICAL(&mux);
    if(owned)chime_bluetooth_replay_end(epoch,error);
}
void chime_ldac_replay_snapshot(chime_ldac_replay_status_t *out)
{
    portENTER_CRITICAL(&mux);*out=state;portEXIT_CRITICAL(&mux);
    out->queued_packets=ingress?(unsigned)uxQueueMessagesWaiting(ingress):0;
}
void chime_ldac_replay_stop(void)
{
    chime_ldac_replay_status_t s;chime_ldac_replay_snapshot(&s);finish(s.epoch,ESP_OK,CHIME_REPLAY_FAILURE_NONE);
}
static void worker(void *arg)
{
    (void)arg;
#if CONFIG_CHIME_LDAC_REPLAY_FLASH
    uint32_t fixture_epoch=0;size_t cursor=0;
#endif
    for(;;){
        packet_t p={0};bool have=false;
#if CONFIG_CHIME_LDAC_REPLAY_FLASH
        chime_ldac_replay_status_t source;chime_ldac_replay_snapshot(&source);
        if(source.active&&source.preloaded){
            uint32_t limit=(source.rate*source.duration_s+127U)/128U*128U;
            if(source.submitted_frames<limit){
                if(fixture_epoch!=source.epoch){fixture_epoch=source.epoch;cursor=0;}
                unsigned bytes=(unsigned)fixture.payload[cursor]|((unsigned)fixture.payload[cursor+1]<<8);
                unsigned nf=fixture.payload[cursor+2],use=(limit-source.submitted_frames)/128U;if(use>nf)use=nf;
                size_t take=0;
                for(unsigned i=0;i<use;++i)take+=chime_ldac_frame_bytes(fixture.payload+cursor+3+take,bytes-take,source.rate);
                esp_a2d_audio_buff_t *b=esp_a2d_audio_buff_alloc((uint16_t)(take+1));
                if(!b){finish(source.epoch,ESP_ERR_NO_MEM,CHIME_REPLAY_FAILURE_MEMORY);continue;}
                b->data_len=(uint16_t)(take+1);b->number_frame=(uint16_t)use;b->timestamp=source.submitted_frames;
                b->data[0]=(uint8_t)use;memcpy(b->data+1,fixture.payload+cursor+3,take);
                p=(packet_t){.buffer=b,.epoch=source.epoch,.offset=source.submitted_frames};have=true;
                cursor+=3+bytes;if(cursor==fixture.length)cursor=0;
            }else vTaskDelay(1);
        }else
#endif
        have=xQueueReceive(ingress,&p,pdMS_TO_TICKS(5))==pdTRUE;
        if(have){
            chime_ldac_replay_status_t s;
            for(;;){
                chime_ldac_replay_snapshot(&s);
                if(!s.active||s.epoch!=p.epoch)break;
                uint64_t now=(uint64_t)esp_timer_get_time();
                uint64_t due=s.start_us+chime_replay_offset_us(p.offset,s.rate,s.ppm);
                if(now>=due){
                    unsigned late=(unsigned)(now-due);
                    portENTER_CRITICAL(&mux);
                    if(state.epoch==p.epoch&&late>state.max_late_us)state.max_late_us=late;
                    portEXIT_CRITICAL(&mux);
                    if(late>LATE_LIMIT_US){finish(p.epoch,ESP_ERR_TIMEOUT,CHIME_REPLAY_FAILURE_FEEDER_LATE);break;}
                    uint32_t frames=(uint32_t)p.buffer->number_frame*128U;
                    bool accepted=chime_bluetooth_replay_submit(p.buffer,p.epoch);p.buffer=NULL;
                    if(!accepted){finish(p.epoch,ESP_ERR_INVALID_STATE,CHIME_REPLAY_FAILURE_OWNER_REVOKED);break;}
                    portENTER_CRITICAL(&mux);
                    if(state.epoch==p.epoch){state.submitted_frames+=frames;++state.packets;
                        if(state.preloaded&&state.fixture_frames)state.completed_cycles=state.submitted_frames/state.fixture_frames;}
                    portEXIT_CRITICAL(&mux);break;
                }
                vTaskDelay(1); /* absolute deadlines, no cumulative tick drift */
            }
            if(p.buffer)esp_a2d_audio_buff_free(p.buffer);
        }
        chime_ldac_replay_status_t s;chime_ldac_replay_snapshot(&s);
        if(!s.active)continue;
        chime_bluetooth_status_t bt;chime_bluetooth_snapshot(&bt);
        if(!bt.simulated||bt.epoch!=s.epoch||bt.error!=ESP_OK){finish(s.epoch,ESP_ERR_INVALID_STATE,CHIME_REPLAY_FAILURE_OWNER_REVOKED);continue;}
        uint32_t limit=(s.rate*s.duration_s+127U)/128U*128U;
        uint64_t now=(uint64_t)esp_timer_get_time();
        uint64_t next=s.start_us+chime_replay_offset_us(s.submitted_frames,s.rate,s.ppm);
        if(!s.preloaded&&s.submitted_frames<limit&&now>next+LATE_LIMIT_US){
            portENTER_CRITICAL(&mux);
            if(state.epoch==s.epoch&&now-next>state.max_late_us)state.max_late_us=(unsigned)(now-next);
            portEXIT_CRITICAL(&mux);
            finish(s.epoch,ESP_ERR_TIMEOUT,uxQueueMessagesWaiting(ingress)?
                CHIME_REPLAY_FAILURE_FEEDER_LATE:CHIME_REPLAY_FAILURE_INPUT_STARVED);
        }
        else if(s.submitted_frames==limit&&now>next+250000ULL)finish(s.epoch,ESP_OK,CHIME_REPLAY_FAILURE_NONE);
    }
}
static esp_err_t begin_source(unsigned rate,unsigned duration_s,int ppm,bool preloaded)
{
    if((rate!=44100&&rate!=48000)||(duration_s!=70&&duration_s!=610)||ppm< -2000||ppm>2000)
        return ESP_ERR_INVALID_ARG;
    chime_ldac_replay_status_t old;chime_ldac_replay_snapshot(&old);
    if(old.active||(ingress&&uxQueueMessagesWaiting(ingress)))return ESP_ERR_INVALID_STATE;
    if(!ingress){
        ingress=xQueueCreate(AHEAD_PACKETS,sizeof(packet_t));if(!ingress)return ESP_ERR_NO_MEM;
        if(xTaskCreatePinnedToCore(worker,"ldac_replay",4096,NULL,7,NULL,0)!=pdPASS){
            vQueueDelete(ingress);ingress=NULL;return ESP_ERR_NO_MEM;
        }
    }
    uint32_t epoch;esp_err_t err=chime_bluetooth_replay_begin(rate,&epoch);if(err!=ESP_OK)return err;
    chime_ldac_replay_status_t next={.active=true,.epoch=epoch,.rate=rate,.duration_s=duration_s,.ppm=ppm,
        .start_us=(uint64_t)esp_timer_get_time()+1000000ULL,.preloaded=preloaded,.flash_ready=old.flash_ready,.fixture_frames=old.fixture_frames};
    portENTER_CRITICAL(&mux);state=next;portEXIT_CRITICAL(&mux);return ESP_OK;
}
esp_err_t chime_ldac_replay_begin(unsigned rate,unsigned duration_s,int ppm)
{return begin_source(rate,duration_s,ppm,false);}
#if CONFIG_CHIME_LDAC_REPLAY_FLASH
esp_err_t chime_ldac_replay_prepare_flash(void)
{
    chime_ldac_replay_status_t s;chime_ldac_replay_snapshot(&s);
    if(s.active||(ingress&&uxQueueMessagesWaiting(ingress)))return ESP_ERR_INVALID_STATE;
    esp_err_t err=chime_ldac_fixture_prepare(&fixture);
    portENTER_CRITICAL(&mux);state.fixture_error=err;
    if(err==ESP_OK){state.flash_ready=true;state.fixture_frames=fixture.cycle_frames;}
    portEXIT_CRITICAL(&mux);
    return err;
}
esp_err_t chime_ldac_replay_begin_flash(unsigned duration_s,int ppm)
{
    if(!fixture.payload)return ESP_ERR_INVALID_STATE;
    return begin_source(fixture.rate,duration_s,ppm,true);
}
#endif
esp_err_t chime_ldac_replay_push(const uint8_t *body,size_t length)
{
    chime_ldac_replay_status_t s;chime_ldac_replay_snapshot(&s);
    if(!s.active||s.preloaded)return ESP_ERR_INVALID_STATE;
    uint32_t limit=(s.rate*s.duration_s+127U)/128U*128U,frames=0;unsigned packets=0;
    if(!chime_replay_batch(body,length,s.uploaded_frames,limit,&packets,&frames)){
        finish(s.epoch,ESP_ERR_INVALID_ARG,CHIME_REPLAY_FAILURE_BAD_BATCH);return ESP_ERR_INVALID_ARG;
    }
    if(packets>AHEAD_PACKETS-(unsigned)uxQueueMessagesWaiting(ingress)){
        finish(s.epoch,ESP_ERR_NO_MEM,CHIME_REPLAY_FAILURE_MEMORY);return ESP_ERR_NO_MEM;
    }
    size_t off=4;uint32_t offset=s.uploaded_frames;
    for(unsigned i=0;i<packets;++i){
        unsigned bytes=(unsigned)body[off]|((unsigned)body[off+1]<<8),nf=body[off+2];off+=3;
        esp_a2d_audio_buff_t *b=esp_a2d_audio_buff_alloc((uint16_t)(bytes+1));
        if(!b){finish(s.epoch,ESP_ERR_NO_MEM,CHIME_REPLAY_FAILURE_MEMORY);return ESP_ERR_NO_MEM;}
        b->data_len=(uint16_t)(bytes+1);b->number_frame=(uint16_t)nf;b->timestamp=offset;
        b->data[0]=(uint8_t)nf;memcpy(b->data+1,body+off,bytes);off+=bytes;
        packet_t p={.buffer=b,.epoch=s.epoch,.offset=offset};
        if(xQueueSend(ingress,&p,0)!=pdTRUE){esp_a2d_audio_buff_free(b);finish(s.epoch,ESP_ERR_NO_MEM,CHIME_REPLAY_FAILURE_MEMORY);return ESP_ERR_NO_MEM;}
        offset+=nf*128U;
    }
    portENTER_CRITICAL(&mux);if(state.active&&state.epoch==s.epoch)state.uploaded_frames+=frames;portEXIT_CRITICAL(&mux);
    return ESP_OK;
}

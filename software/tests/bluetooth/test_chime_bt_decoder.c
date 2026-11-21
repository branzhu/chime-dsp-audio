#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif
/* Actual decoder worker with deterministic SDK/RTOS fakes. This tests ownership
 * and bounded control flow, NOT the proprietary decoder's numerical output. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../main/chime_bt_decoder.c"
static packet_t slots[RAW_DEPTH];
static unsigned head,count,allocs,frees,opens,closes,emits,errors,decoded;
static uint32_t generation=1;
static bool live=true,queue_fail,task_fail,open_fail,auto_shutdown=true,invalidate_during_decode;
static unsigned mode,expected_rate=48000,expected_channels=2;
static esp_err_t last_error;
#if CHIME_BT_CAPTURE_LOG
#include <stdarg.h>
static char layout_message[512],capture_message[256],packet_message[512],input_messages[4][512];
static uint8_t rejected_bytes[1024];
static unsigned rejected_count,diagnostic_lines;
void chime_test_log(const char *tag,const char *format,...){
    (void)tag;char text[1024];va_list args;va_start(args,format);
    int n=vsnprintf(text,sizeof(text),format,args);va_end(args);assert(n>=0&&(size_t)n<sizeof(text));
    if(!strncmp(text,"packet failed ",14)){assert(strlen(text)<sizeof(packet_message));strcpy(packet_message,text);}
    if(strncmp(text,"LDAC ",5))return;
    assert(!live); /* fail callback must invalidate BEFORE the diagnostic UART */
    ++diagnostic_lines;
    if(!strncmp(text,"LDAC INPUT ",11)){
        unsigned index=strstr(text,"phase=submit ")?0:strstr(text,"phase=worker ")?1:strstr(text,"phase=fault ")?2:3;
        assert(strlen(text)<sizeof(input_messages[index]));strcpy(input_messages[index],text);
    }
    if(!strncmp(text,"LDAC layout ",12)){assert(strlen(text)<sizeof(layout_message));strcpy(layout_message,text);}
    if(!strncmp(text,"LDAC rejected ",14)){assert(strlen(text)<sizeof(capture_message));strcpy(capture_message,text);}
    if(!strncmp(text,"LDAC packet +",13)){
        unsigned offset=0;assert(sscanf(text+13,"%x",&offset)==1&&offset==rejected_count);
        const char *p=strchr(text,':');assert(p);++p;
        while(*p==' ')++p;
        while(*p){unsigned v;assert(sscanf(p,"%2x",&v)==1&&rejected_count<sizeof(rejected_bytes));
            rejected_bytes[rejected_count++]=(uint8_t)v;p+=2;while(*p==' ')++p;}
    }
}
#endif
static void (*task_entry)(void *);
static uint8_t decoder_token;
#if CHIME_BT_REAL_LHDC || CHIME_BT_REAL_LDAC
static uint8_t captured[16*960];
static size_t captured_bytes;
static bool capture;
#endif
static unsigned timer_step=1;static int64_t fake_clock;
static bool valid(uint32_t e){return live&&e==generation;}
static void emit(const uint8_t *p,uint32_t n,uint32_t e){assert(p==output&&n<=PCM_BYTES);if(valid(e)){
    ++emits;
#if CHIME_BT_REAL_LHDC || CHIME_BT_REAL_LDAC
    if(capture){assert(captured_bytes+n<=sizeof(captured));memcpy(captured+captured_bytes,p,n);captured_bytes+=n;}
#endif
}fake_clock+=11;}
static void fail(uint32_t e,esp_err_t error){if(valid(e)){++errors;last_error=error;live=false;}}
static unsigned timing_calls;static uint64_t timing_residence;
static void timing(uint32_t epoch,uint64_t received){assert(valid(epoch));assert(received<=(uint64_t)fake_clock);timing_residence=(uint64_t)fake_clock-received;++timing_calls;}
static const chime_bt_decoder_callbacks_t callbacks={valid,emit,fail,timing};
QueueHandle_t xQueueCreate(unsigned n,unsigned size){assert(n==RAW_DEPTH&&size==sizeof(packet_t));if(queue_fail)return NULL;head=count=0;return slots;}
int xQueueSend(QueueHandle_t q,const void *p,uint32_t wait){assert(q==slots&&!wait);
#if CONFIG_CHIME_BT_VENDOR_CODECS
    const packet_t *submitted=p;assert(!submitted->worker.recorded);
    if(submitted->codec==CHIME_BT_CODEC_LDAC)assert(submitted->submitted.recorded&&submitted->submitted.hashed);
    else assert(!submitted->submitted.recorded&&!submitted->submitted.hashed);
#endif
    if(count==RAW_DEPTH)return 0;slots[(head+count)%RAW_DEPTH]=*(const packet_t *)p;++count;return 1;
}
int xQueueReceive(QueueHandle_t q,void *p,uint32_t wait){assert(q==slots);if(!count){if(wait)atomic_store(&stopping,true);return 0;}*(packet_t *)p=slots[head];head=(head+1)%RAW_DEPTH;--count;return 1;}
unsigned uxQueueMessagesWaiting(QueueHandle_t q){assert(q==slots);return count;}
int64_t esp_timer_get_time(void){return fake_clock;}
void vQueueDelete(QueueHandle_t q){assert(q==slots&&!count);}
int xTaskCreatePinnedToCore(void (*f)(void *),const char *name,unsigned stack,void *arg,unsigned priority,TaskHandle_t *handle,int core){assert(core==DECODE_CORE);assert(!strcmp(name,"bt_decode")&&stack==6144&&!arg&&priority==5&&!handle);task_entry=f;return task_fail?0:pdPASS;}
void vTaskDelay(uint32_t ms){assert(ms==5);if(auto_shutdown&&!atomic_load(&exited))task_entry(NULL);}
void vTaskDelete(void *p){assert(!p);}
/* Test-only repair at the deallocator lets metadata fault injection use an
 * invalid data pointer without leaking its original allocation. No production
 * reader/decode path gets this repair. */
static esp_a2d_audio_buff_t *injected_metadata_buffer;
static uint8_t *injected_metadata_data;
void esp_a2d_audio_buff_free(esp_a2d_audio_buff_t *p){
    assert(p);if(p==injected_metadata_buffer){p->data=injected_metadata_data;injected_metadata_buffer=NULL;injected_metadata_data=NULL;}
    assert(p->data);++frees;free(p->data);p->data=NULL;free(p);
}
static esp_a2d_audio_buff_t *buffer(unsigned size){esp_a2d_audio_buff_t *p=malloc(sizeof(*p));assert(p);p->timestamp=100;p->data_len=(uint16_t)size;p->data=calloc(size?size:1,1);assert(p->data);++allocs;return p;}
esp_audio_err_t esp_aac_dec_open(void *cfg,uint32_t n,void **handle){esp_aac_dec_cfg_t *p=cfg;assert(n==sizeof(*p)&&p->sample_rate==(int32_t)expected_rate&&p->channel==expected_channels&&p->bits_per_sample==16&&p->no_adts_header&&!p->aac_plus_enable);++opens;if(open_fail)return -1;*handle=&decoder_token;return 0;}
esp_audio_err_t esp_sbc_dec_open(void *cfg,uint32_t n,void **handle){esp_sbc_dec_cfg_t *p=cfg;assert(n==sizeof(*p)&&p->sbc_mode==ESP_SBC_MODE_STD&&p->ch_num==expected_channels&&!p->enable_plc);++opens;if(open_fail)return -1;*handle=&decoder_token;return 0;}
esp_audio_err_t esp_aac_dec_close(void *h){assert(h==&decoder_token);++closes;return 0;}
esp_audio_err_t esp_sbc_dec_close(void *h){return esp_aac_dec_close(h);}
esp_audio_err_t esp_aac_dec_decode(void *h,esp_audio_dec_in_raw_t *in,esp_audio_dec_out_frame_t *out,esp_audio_dec_info_t *info){
    assert(h==&decoder_token&&in->len&&out->len==PCM_BYTES&&in->consumed==0);++decoded;fake_clock+=timer_step;
    in->consumed=in->len>2?2:in->len;out->decoded_size=expected_channels*2;
    *info=(esp_audio_dec_info_t){.sample_rate=expected_rate,.channel=(uint8_t)expected_channels,.bits_per_sample=16};
    if(invalidate_during_decode)++generation;
    switch(mode){case 1:in->consumed=0;break;case 2:in->consumed=in->len+1;break;case 3:out->decoded_size=PCM_BYTES+4;break;case 4:out->decoded_size=1;break;case 5:info->sample_rate=96000;break;case 6:info->bits_per_sample=24;break;case 7:return ESP_AUDIO_ERR_BUFF_NOT_ENOUGH;case 8:return -33;case 9:out->decoded_size=0;break;}
    return ESP_AUDIO_ERR_OK;
}
esp_audio_err_t esp_sbc_dec_decode(void *h,esp_audio_dec_in_raw_t *in,esp_audio_dec_out_frame_t *out,esp_audio_dec_info_t *info){return esp_aac_dec_decode(h,in,out,info);}
#if CONFIG_CHIME_BT_VENDOR_CODECS
esp_audio_err_t esp_opus_dec_open(void *cfg,uint32_t n,void **handle){esp_opus_dec_cfg_t *p=cfg;assert(n==sizeof(*p)&&p->sample_rate==48000&&p->channel==2&&p->frame_duration==ESP_OPUS_DEC_FRAME_DURATION_20_MS&&!p->self_delimited);++opens;*handle=&decoder_token;return 0;}
esp_audio_err_t esp_opus_dec_close(void *handle){return esp_aac_dec_close(handle);}
esp_audio_err_t esp_opus_dec_decode(void *handle,esp_audio_dec_in_raw_t *in,esp_audio_dec_out_frame_t *out,esp_audio_dec_info_t *info){assert(handle==&decoder_token);++decoded;in->consumed=in->len;out->decoded_size=3840;*info=(esp_audio_dec_info_t){.sample_rate=48000,.channel=2,.bits_per_sample=16};return 0;}
#if !CHIME_BT_REAL_LDAC
void *chime_ldac_open(unsigned rate,unsigned channels){assert(rate==48000&&channels==2);++opens;return &decoder_token;}
void chime_ldac_close(void *h){assert(h==&decoder_token);++closes;}
size_t chime_ldac_frame_bytes(const uint8_t *data,size_t n,unsigned rate){
    if(n<4||data[0]!=0xaa||(data[1]>>5)!=(rate==48000?1:0)||((data[1]>>3)&3)!=2)return 0;
    size_t bytes=(((size_t)data[1]&7)<<6)+(data[2]>>2)+4;return bytes<=n?bytes:0;
}
size_t chime_ldac_decode(void *h,const uint8_t *data,size_t n,uint8_t *out,size_t max){
    assert(h==&decoder_token&&chime_ldac_frame_bytes(data,n,48000)==n&&out==output&&max==PCM_BYTES);++decoded;return mode?0:512;
}
#endif
#if CONFIG_CHIME_BT_LHDC_V5 && !CHIME_BT_REAL_LHDC
void *chime_lhdc_open(unsigned rate,unsigned channels){assert((rate==48000||rate==44100)&&channels==2);++opens;return &decoder_token;}
void chime_lhdc_close(void *h){assert(h==&decoder_token);++closes;}
size_t chime_lhdc_frame_bytes(const uint8_t *data,size_t n){return n>=20&&data[0]==9&&data[1]==4?20:0;}
size_t chime_lhdc_decode(void *h,const uint8_t *data,size_t n,uint8_t *out,size_t max){
    assert(h==&decoder_token&&chime_lhdc_frame_bytes(data,n)==n&&out==output&&max==PCM_BYTES);++decoded;return mode?0:960;
}
static esp_a2d_audio_buff_t *lhdc_packet(unsigned n,unsigned seq){
    esp_a2d_audio_buff_t *p=buffer(2+n*20);p->data[0]=(uint8_t)(n<<2);p->data[1]=(uint8_t)seq;
    for(unsigned i=0;i<n;++i){p->data[2+i*20]=9;p->data[3+i*20]=4;}
    return p;
}
#endif
struct aptx_context *aptx_init(int hd){assert(hd==0||hd==1);++opens;return (struct aptx_context *)&decoder_token;}
void aptx_finish(struct aptx_context *h){assert(h==(struct aptx_context *)&decoder_token);++closes;}
size_t aptx_decode16(struct aptx_context *h,const unsigned char *in,size_t len,unsigned char *out,size_t max,size_t *written){assert(h==(struct aptx_context *)&decoder_token&&in&&out==output&&max==PCM_BYTES);++decoded;*written=16;return len;}
#endif
static void start(void){live=true;mode=0;expected_rate=48000;expected_channels=2;assert(chime_bt_decoder_start(&callbacks)==0);assert(chime_bt_decoder_start(&callbacks)==ESP_ERR_INVALID_STATE);}
static void run(void){task_entry(NULL);assert(chime_bt_decoder_stop());assert(allocs==frees);++generation;}
int main(void)
{
#if CONFIG_CHIME_BT_VENDOR_CODECS
    assert(input_crc32(NULL,0)==0&&input_crc32((const uint8_t *)"123456789",9)==0xcbf43926u);
#endif
    chime_bt_decoder_stats_t stats={0},saved;
    assert(!chime_bt_decoder_snapshot(NULL));
    stats_reset(99);assert(chime_bt_decoder_snapshot(&stats)&&stats.epoch==99&&!stats.packets);
    saved=stats;atomic_fetch_add(&stats_revision,1);
    assert(!chime_bt_decoder_snapshot(&stats)&&!memcmp(&stats,&saved,sizeof(stats)));
    atomic_fetch_add(&stats_revision,1);
    queue_fail=true;assert(chime_bt_decoder_start(&callbacks)==ESP_ERR_NO_MEM);queue_fail=false;
    task_fail=true;assert(chime_bt_decoder_start(&callbacks)==ESP_ERR_NO_MEM);task_fail=false;
    chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_AAC,48000,2);assert(allocs==frees);
    for(unsigned codec=CHIME_BT_CODEC_SBC;codec<=CHIME_BT_CODEC_AAC;++codec){
        start();unsigned before=emits,bt=timing_calls;
        chime_bt_decoder_submit(buffer(6),generation,(chime_bt_codec_t)codec,48000,2);run();assert(emits==before+3&&timing_calls==bt+1);
    }
    start();unsigned optional_timing=timing_calls,optional_emits=emits;cb.timing=NULL;
    chime_bt_decoder_submit(buffer(6),generation,CHIME_BT_CODEC_AAC,48000,2);run();
    assert(timing_calls==optional_timing&&emits==optional_emits+3); /* optional timing hook */
    /* Timing reports are per-epoch; a slow packet must not relax the queue,
     * decoder error or stale-epoch gates, or leak the previous max to restart. */
    start();timer_step=9000;
    chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_AAC,48000,2);run();
    assert(atomic_load(&last_packet_us)==18022&&atomic_load(&max_packet_us)==18022);
    assert(atomic_load(&last_decode_us)==18000&&atomic_load(&last_pcm_us)==22&&!atomic_load(&last_open_us));
    assert(chime_bt_decoder_snapshot(&stats)&&stats.epoch==generation-1&&stats.packets==1&&stats.slow_packets==1);
    assert(stats.packet_us==18022&&stats.max_packet_us==18022&&stats.decode_us==18000&&stats.pcm_us==22&&stats.audio_us>0);
    start();timer_step=1;assert(!atomic_load(&max_packet_us));
    assert(chime_bt_decoder_snapshot(&stats)&&!stats.epoch&&!stats.packets&&!stats.slow_packets&&!stats.max_packet_us);
    chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_AAC,48000,2);run();
    assert(atomic_load(&last_packet_us)==24&&atomic_load(&max_packet_us)==24);
    assert(atomic_load(&last_decode_us)==2&&atomic_load(&last_pcm_us)==22);
    assert(chime_bt_decoder_snapshot(&stats)&&stats.epoch==generation-1&&stats.packets==1&&stats.slow_packets==0&&stats.packet_us==24);
    /* Waiting BEFORE processing was invisible to packet_us/slow_packets. */
    start();
    chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_AAC,48000,2);
    chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_AAC,48000,2);
    fake_clock+=7000;run();
    assert(chime_bt_decoder_snapshot(&stats)&&stats.packets==2&&!stats.slow_packets);
    assert(stats.packet_us==24&&stats.queue_wait_us==7024&&stats.max_queue_wait_us==7024);
    assert(timing_residence==7025); /* wait + first decode, not entire packet CPU */
    assert(!stats.raw_after_dequeue&&stats.raw_max_after_dequeue==1);
    start();assert(chime_bt_decoder_snapshot(&stats)&&!stats.max_queue_wait_us&&!stats.raw_max_after_dequeue);
    run();
    start();
    chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_AAC,48000,2);
    fake_clock+=(int64_t)UINT32_MAX+100;run();
    assert(chime_bt_decoder_snapshot(&stats)&&stats.queue_wait_us==UINT32_MAX&&stats.max_queue_wait_us==UINT32_MAX);
    start();expected_rate=44100;expected_channels=1;
    chime_bt_decoder_submit(buffer(2),generation,CHIME_BT_CODEC_AAC,44100,1);run();
    for(unsigned m=1;m<=9;++m){start();unsigned before=errors;mode=m;chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_AAC,48000,2);run();assert(errors==before+(m==9?0U:1U));}
    start();open_fail=true;chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_AAC,48000,2);run();open_fail=false;assert(last_error==ESP_FAIL);
    start();unsigned old_opens=opens;
    chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_AAC,48000,2);++generation;run();assert(opens==old_opens);
    start();unsigned before=emits,before_timing=timing_calls;invalidate_during_decode=true;
    chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_AAC,48000,2);run();invalidate_during_decode=false;assert(emits==before&&timing_calls==before_timing);
    start();for(unsigned i=0;i<RAW_DEPTH+1;++i)chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_SBC,48000,2);
    assert(count==RAW_DEPTH&&last_error==ESP_ERR_NO_MEM);run();
    start();chime_bt_decoder_submit(buffer(RAW_MAX_BYTES+1),generation,CHIME_BT_CODEC_AAC,48000,2);assert(last_error==ESP_ERR_INVALID_SIZE);run();
    start();chime_bt_decoder_submit(buffer(0),generation,CHIME_BT_CODEC_AAC,48000,2);run();
    start();chime_bt_decoder_submit(NULL,generation,CHIME_BT_CODEC_AAC,48000,2);run();
    start();chime_bt_decoder_submit(buffer(4),generation,CHIME_BT_CODEC_AAC,48000,2);auto_shutdown=false;
    assert(!chime_bt_decoder_stop()&&raw_queue&&output);assert(chime_bt_decoder_start(&callbacks)==ESP_ERR_INVALID_STATE);
    auto_shutdown=true;assert(chime_bt_decoder_stop());assert(allocs==frees);
#if CONFIG_CHIME_BT_VENDOR_CODECS
#if !CHIME_BT_REAL_LDAC
    start();unsigned ldac_emits=emits;
    esp_a2d_audio_buff_t *ld=buffer(9);ld->data[0]=2;ld->data[1]=ld->data[5]=0xaa;ld->data[2]=ld->data[6]=0x30;
    chime_bt_decoder_submit(ld,generation,CHIME_BT_CODEC_LDAC,48000,2);run();assert(emits==ldac_emits+2);
    for(unsigned m=0;m<7;++m){
        start();unsigned before_error=errors,before_emits=emits;ld=buffer(5);ld->data[0]=1;ld->data[1]=0xaa;ld->data[2]=0x30;
        switch(m){case 0:ld->data[0]=2;break;case 1:ld->data[0]=0xa1;break;case 2:ld->data[0]=0x11;break;
          case 3:ld->data[2]=0x10;break;case 4:ld->data[3]=4;break;case 5:mode=1;break;case 6:ld->data[0]=0;break;}
        chime_bt_decoder_submit(ld,generation,CHIME_BT_CODEC_LDAC,48000,2);run();assert(errors==before_error+1&&emits==before_emits);
    }
    start();ldac_emits=emits;
    ld=buffer(3);ld->data[0]=0xc2;ld->data[1]=0xaa;ld->data[2]=0x30;
    chime_bt_decoder_submit(ld,generation,CHIME_BT_CODEC_LDAC,48000,2);
    ld=buffer(3);ld->data[0]=0xa1;
    chime_bt_decoder_submit(ld,generation,CHIME_BT_CODEC_LDAC,48000,2);run();assert(emits==ldac_emits+1);
    start();ld=buffer(520);ld->data[0]=0xc2;
    chime_bt_decoder_submit(ld,generation,CHIME_BT_CODEC_LDAC,48000,2);run();assert(last_error==ESP_ERR_INVALID_SIZE);
    #endif
    for(unsigned kind=CHIME_BT_CODEC_APTX;kind<=CHIME_BT_CODEC_APTX_HD;++kind){start();unsigned e=emits;chime_bt_decoder_submit(buffer(kind==CHIME_BT_CODEC_APTX?4:6),generation,(chime_bt_codec_t)kind,48000,2);run();assert(emits==e+1);}
    start();chime_bt_decoder_submit(buffer(5),generation,CHIME_BT_CODEC_APTX,48000,2);run();assert(last_error==ESP_ERR_INVALID_SIZE);
    start();unsigned e=emits;esp_a2d_audio_buff_t *op=buffer(3);op->data[0]=1;op->data[1]=0x98; /* 20ms CELT */
    chime_bt_decoder_submit(op,generation,CHIME_BT_CODEC_OPUS,48000,2);run();assert(emits==e+1);
    start();op=buffer(3);op->data[0]=1;op->data[1]=0x90; /* 10ms, not negotiated */
    chime_bt_decoder_submit(op,generation,CHIME_BT_CODEC_OPUS,48000,2);run();assert(last_error==ESP_ERR_NOT_SUPPORTED);
    start();e=emits;op=buffer(2);op->data[0]=0xc2;op->data[1]=0x98;chime_bt_decoder_submit(op,generation,CHIME_BT_CODEC_OPUS,48000,2);
    op=buffer(2);op->data[0]=0xa1;chime_bt_decoder_submit(op,generation,CHIME_BT_CODEC_OPUS,48000,2);run();assert(emits==e+1);
    start();op=buffer(2);op->data[0]=0xa1;chime_bt_decoder_submit(op,generation,CHIME_BT_CODEC_OPUS,48000,2);run();assert(last_error==ESP_ERR_INVALID_SIZE);
    start();op=buffer(2);op->data[0]=0xc2;chime_bt_decoder_submit(op,generation,CHIME_BT_CODEC_OPUS,48000,2);
    op=buffer(2);op->data[0]=0xa1;op->timestamp=101;chime_bt_decoder_submit(op,generation,CHIME_BT_CODEC_OPUS,48000,2);run();assert(last_error==ESP_ERR_INVALID_SIZE);
    start();op=buffer(1280);op->data[0]=1;chime_bt_decoder_submit(op,generation,CHIME_BT_CODEC_OPUS,48000,2);run();assert(last_error==ESP_ERR_INVALID_SIZE);
#endif
#if CONFIG_CHIME_BT_VENDOR_CODECS
#if CONFIG_CHIME_BT_LHDC_V5 && !CHIME_BT_REAL_LHDC
    /* V5 two-byte media header: count occupies SIX bits; 0x80 is 32 frames,
     * not an Opus/LDAC fragmentation flag. Check wrap and frame validation. */
    start();unsigned le=emits;
    chime_bt_decoder_submit(lhdc_packet(32,255),generation,CHIME_BT_CODEC_LHDC_V5,48000,2);
    chime_bt_decoder_submit(lhdc_packet(1,0),generation,CHIME_BT_CODEC_LHDC_V5,48000,2);
    run();assert(emits==le+33);
    for(unsigned kind=0;kind<7;++kind){
        start();unsigned be=errors,bp=emits;esp_a2d_audio_buff_t *l=lhdc_packet(2,7);
        switch(kind){case 0:l->data[0]=0;break;case 1:l->data[0]=3;break;case 2:l->data[0]=12;break;
            case 3:l->data[0]=4;break;case 4:l->data[23]=0;break;case 5:l->data_len=1;break;case 6:mode=1;break;}
        chime_bt_decoder_submit(l,generation,CHIME_BT_CODEC_LHDC_V5,48000,2);run();assert(errors==be+1&&emits==bp);
    }
    start();le=emits;unsigned ler=errors;
    chime_bt_decoder_submit(lhdc_packet(1,1),generation,CHIME_BT_CODEC_LHDC_V5,48000,2);
    chime_bt_decoder_submit(lhdc_packet(1,3),generation,CHIME_BT_CODEC_LHDC_V5,48000,2);
    run();assert(emits==le+1&&errors==ler+1);
    start();le=emits;chime_bt_decoder_submit(lhdc_packet(1,153),generation,CHIME_BT_CODEC_LHDC_V5,44100,2);run();assert(emits==le+1);
    /* All 63 counts and all four low-bit values follow upstream parsing.
     * Synthetic 20-byte frames keep even count63 inside RAW_MAX_BYTES. */
    for(unsigned frames=1;frames<=63;++frames)for(unsigned low=0;low<4;++low){
        start();unsigned be=errors,bp=emits;
        esp_a2d_audio_buff_t *l=lhdc_packet(frames,2);l->data[0]|=(uint8_t)low;
        chime_bt_decoder_submit(l,generation,CHIME_BT_CODEC_LHDC_V5,48000,2);run();
        assert(errors==be&&emits==bp+frames);
    }
    puts("PASS LHDC V5 worker: two-byte media header, 6-bit frame count, all-frame prevalidation, sequence/wrap, epoch reset and decoder error gates (API faked)");
#endif
    puts("PASS vendor worker: aptX/HD alignment; LDAC header/count/frame validation; Android Opus single/fragmented 20ms packets; TOC, size, timestamp and orphan rejection (decoder APIs faked)");
#endif
#if CHIME_BT_REAL_LHDC
    /* End-to-end: actual worker admission/header/frame validation -> actual
     * product frame adapter -> selected reference decoder -> captured PCM.
     * The independent 48k q5 encoder fixture uses 252-byte frames;
     * count2/low1/seq2 reproduces the logged 506-byte 09:02:7d:04 prefix. */
    for(unsigned rate_index=0;rate_index<2;++rate_index){
        unsigned rate=rate_index?44100:48000;
        char path[128];snprintf(path,sizeof(path),"software/tests/bluetooth/fixtures/lhdc/%u-q5.lhdc",rate);
        FILE *f=fopen(path,"rb");assert(f);uint8_t raw[4096];size_t size=fread(raw,1,sizeof(raw),f);fclose(f);assert(size==sizeof(raw));
        const unsigned counts[]={1,2,8,rate_index?14u:16u};
        for(unsigned k=0;k<sizeof(counts)/sizeof(counts[0]);++k){
            unsigned frames=counts[k];size_t bytes=0;
            for(unsigned i=0;i<frames;++i){size_t n=chime_lhdc_frame_bytes(raw+bytes,size-bytes);assert(n==(rate_index?274u:252u));bytes+=n;}
            uint8_t expected[sizeof(captured)];
            for(unsigned low=0;low<4;++low){
                start();unsigned be=errors,bp=emits;
                esp_a2d_audio_buff_t *l=buffer((unsigned)bytes+2u);l->data[0]=(uint8_t)((frames<<2)|low);l->data[1]=2;
                memcpy(l->data+2,raw,bytes);l->timestamp=1920;
                if(rate==48000&&frames==2&&low==1){assert(l->data_len==506&&!memcmp(l->data,"\x09\x02\x7d\x04",4));}
                capture=true;captured_bytes=0;
                chime_bt_decoder_submit(l,generation,CHIME_BT_CODEC_LHDC_V5,rate,2);run();capture=false;
                assert(errors==be&&emits==bp+frames&&captured_bytes==frames*960u);
                if(low)assert(!memcmp(expected,captured,captured_bytes));else memcpy(expected,captured,captured_bytes);
            }
        }
    }
    puts("PASS actual worker + reference LHDC decoder: 506-byte 09:02:7d:04, all low bits, grouped frames up to RAW_MAX_BYTES, 44.1/48k and identical PCM to low-zero packets");
#endif
#if CHIME_BT_REAL_LDAC
    /* Actual worker + frame wrapper + selected reference core. Adaptive encoder
     * record boundaries/counts are preserved; no reopen between bitrate changes.
     * Compare every PCM byte to direct sequential reference-frame decode. */
    uint8_t raw[2048],expected[800*512];size_t total=0;unsigned packets=0,target=0,mixed=0;
    const char *fixture="software/tests/bluetooth/fixtures/ldac-adaptive/48000-abr.packets";
    FILE *f=fopen(fixture,"rb");assert(f);void *direct=chime_ldac_open(48000,2);assert(direct);
    for(;;){int lo=fgetc(f);if(lo==EOF)break;int hi=fgetc(f),frames=fgetc(f);assert(hi>=0&&frames>0&&frames<=15);
        unsigned bytes=(unsigned)(lo|(hi<<8));assert(bytes<=sizeof(raw)&&fread(raw,1,bytes,f)==bytes);
        size_t offset=0,first=0;bool varying=false;
        for(int i=0;i<frames;++i){size_t n=chime_ldac_frame_bytes(raw+offset,bytes-offset,48000);assert(n);
            if(i&&n!=first)varying=true;first=n;
            assert(total+512<=sizeof(expected)&&chime_ldac_decode(direct,raw+offset,n,expected+total,512)==512);
            offset+=n;total+=512;
        }
        assert(offset==bytes);++packets;if(varying)++mixed;
        if(frames==5&&bytes==660&&!memcmp(raw,"\xaa\x32\x00",3))++target;
    }
    fclose(f);chime_ldac_close(direct);assert(target==21&&mixed>0&&packets>200);
    start();capture=true;f=fopen(fixture,"rb");assert(f);size_t checked=0;unsigned be=errors;
    for(;;){int lo=fgetc(f);if(lo==EOF)break;int hi=fgetc(f),frames=fgetc(f);unsigned bytes=(unsigned)(lo|(hi<<8));
        esp_a2d_audio_buff_t *l=buffer(bytes+1u);l->data[0]=(uint8_t)frames;assert(fread(l->data+1,1,bytes,f)==bytes);
        l->timestamp=1701888u;captured_bytes=0;
        packet_t p={.buffer=l,.epoch=generation,.codec=CHIME_BT_CODEC_LDAC,.rate=48000,.channels=2};
        process_packet(&p);assert(errors==be&&captured_bytes==(size_t)frames*512u);
        assert(checked+captured_bytes<=total&&!memcmp(captured,expected+checked,captured_bytes));checked+=captured_bytes;
        esp_a2d_audio_buff_free(l);
    }
    fclose(f);capture=false;assert(checked==total);run();
    /* No PCM from partially valid packets, and rejection after any frame
     * index is bounded. Prefix 05:aa:32:00 alone cannot reveal frame2..5. */
    f=fopen(fixture,"rb");assert(f);unsigned bytes=0,frames=0;
    for(;;){int lo=fgetc(f);assert(lo!=EOF);int hi=fgetc(f);frames=(unsigned)fgetc(f);bytes=(unsigned)(lo|(hi<<8));
        assert(fread(raw,1,bytes,f)==bytes);if(bytes==660&&frames==5&&!memcmp(raw,"\xaa\x32\x00",3))break;
    }fclose(f);
    for(unsigned fault=0;fault<9;++fault){start();unsigned bp=emits;be=errors;
        esp_a2d_audio_buff_t *l=buffer(fault==8?1500:661);l->data[0]=5;memcpy(l->data+1,raw,660);
        if(fault<5)l->data[1+fault*132]=0; /* each frame's sync */
        else if(fault==5)l->data_len=660; /* truncated last frame */
        else if(fault==6)l->data[0]=4; /* valid frames followed by remainder */
        else if(fault==7)l->data[0]=6; /* no sixth frame */
#if CHIME_BT_CAPTURE_LOG
        rejected_count=diagnostic_lines=0;layout_message[0]=capture_message[0]=0;
        uint8_t packet_copy[1024];unsigned copied=l->data_len<1024?l->data_len:1024;
        memcpy(packet_copy,l->data,copied);
#endif
        chime_bt_decoder_submit(l,generation,CHIME_BT_CODEC_LDAC,48000,2);run();
        assert(errors==be+1&&emits==bp&&last_error==ESP_ERR_INVALID_SIZE);
#if CHIME_BT_CAPTURE_LOG
        assert(rejected_count==copied&&!memcmp(rejected_bytes,packet_copy,copied));
        assert(diagnostic_lines==6+(copied+31)/32);
        if(fault<5){char field[80];snprintf(field,sizeof(field),"frame=%u/5 payload_offset=%u",fault+1,fault*132);
            assert(strstr(layout_message,field)&&strstr(layout_message,"invalid-or-truncated-frame"));}
        if(fault==6||fault==8)assert(strstr(layout_message,"trailing-bytes-after-frame-count"));
        if(fault==8)assert(strstr(capture_message,"capture=1024/1500"));
#endif
    }
    puts("PASS actual worker + reference LDAC: adaptive HQ/SQ/MQ/Q0/Q1 without reopen, mixed frame sizes, 21 exact 661-byte 05:aa:32:00 layouts, direct PCM identity, each-frame corruption/truncation/count rejection before PCM");

    /* Exact fault-time phone packets, not encoder-generated approximations.
     * Both have invalid frame2 sync at offset165. The second also has invalid
     * channel mode/length bits. A SYNTHETIC full-header control is test-only:
     * layout passes but frame1 decode still fails; never repair phone bytes. */
    static const struct {const char *file,*head,*layout;uint32_t timestamp;} phone_cases[]={
        {"48000-sync-corrupt.packet","\x3f\x32\x80","declared_bytes=164 head=3f:32:80",55570432u},
        {"48000-frame2-header-corrupt-2.packet","\x15\x22\x60","declared_bytes=156 head=15:22:60",23443968u}
    };
    for(unsigned sample=0;sample<sizeof(phone_cases)/sizeof(phone_cases[0]);++sample){
        uint8_t phone[657];char path[256];
        snprintf(path,sizeof(path),"software/tests/bluetooth/fixtures/ldac-phone-failure/%s",phone_cases[sample].file);
        f=fopen(path,"rb");assert(f);
        assert(fread(phone,1,sizeof(phone),f)==sizeof(phone)&&fgetc(f)==EOF);fclose(f);
        assert(phone[0]==4&&!memcmp(phone+165,phone_cases[sample].head,3));
        for(unsigned i=0;i<4;++i){unsigned offset=1u+i*164u;
            if(i!=1)assert(phone[offset]==0xaa&&phone[offset+1]==0x32&&phone[offset+2]==0x80);
            assert(chime_ldac_frame_bytes(phone+offset,sizeof(phone)-offset,48000)==(i==1?0u:164u));
        }
        for(unsigned control=0;control<2;++control){
            start();unsigned bp=emits;be=errors;
            esp_a2d_audio_buff_t *l=buffer(sizeof(phone));memcpy(l->data,phone,sizeof(phone));l->timestamp=phone_cases[sample].timestamp;
            if(control){
                memcpy(l->data+165,"\xaa\x32\x80",3);
                for(unsigned i=0;i<4;++i){unsigned offset=1u+i*164u;
                    assert(chime_ldac_frame_bytes(l->data+offset,sizeof(phone)-offset,48000)==164u);
                }
            }
#if CHIME_BT_CAPTURE_LOG
            rejected_count=diagnostic_lines=0;layout_message[0]=capture_message[0]=packet_message[0]=0;
#endif
            capture=true;captured_bytes=0;
            chime_bt_decoder_submit(l,generation,CHIME_BT_CODEC_LDAC,48000,2);run();capture=false;
            assert(errors==be+1&&emits==bp&&captured_bytes==0&&last_error==ESP_ERR_INVALID_SIZE&&!live);
            if(!control){
#if CHIME_BT_CAPTURE_LOG
                assert(strstr(packet_message,"stage=frame-layout"));
                assert(rejected_count==sizeof(phone)&&!memcmp(rejected_bytes,phone,sizeof(phone)));
                assert(diagnostic_lines==27&&strstr(capture_message,"capture=657/657"));
                assert(strstr(layout_message,"frame=2/4 payload_offset=164 remaining=492"));
                assert(strstr(layout_message,phone_cases[sample].layout));
                assert(strstr(layout_message,"invalid-or-truncated-frame"));
#endif
            }else{
#if CHIME_BT_CAPTURE_LOG
                assert(diagnostic_lines==4&&!rejected_count&&strstr(packet_message,"stage=decode"));
#endif
            }
        }
    }
    puts("PASS captured phone LDAC: two exact 657-byte packets rejected at frame2 before any PCM; synthetic header controls pass layout but fail first decode (not production repairs)");

#if CHIME_BT_CAPTURE_LOG
    /* Actual submission/dequeue evidence: only bytes mutated by this test are
     * synthetic. Production still refuses each original failing whole packet. */
    uint8_t phone_input[657];
    f=fopen("software/tests/bluetooth/fixtures/ldac-phone-failure/48000-frame2-header-corrupt-2.packet","rb");assert(f);
    assert(fread(phone_input,1,sizeof(phone_input),f)==sizeof(phone_input));fclose(f);
    assert(input_crc32(phone_input,sizeof(phone_input))==0x87cac939u);
    for(unsigned injection=0;injection<6;++injection){
        start();unsigned bp=emits;be=errors;
        esp_a2d_audio_buff_t *l=buffer(sizeof(phone_input));memcpy(l->data,phone_input,sizeof(phone_input));l->timestamp=23443968u;
        rejected_count=diagnostic_lines=0;memset(input_messages,0,sizeof(input_messages));
        chime_bt_decoder_submit(l,generation,CHIME_BT_CODEC_LDAC,48000,2);assert(count==1);
        assert(slots[head].submitted.recorded&&slots[head].submitted.hashed&&!slots[head].worker.recorded);
        assert(slots[head].submitted.crc32==0x87cac939u&&slots[head].submitted.bytes==657&&slots[head].submitted.timestamp==23443968u);
        assert(!memcmp(slots[head].submitted.head,phone_input,4));
        /* Deliberate post-transfer corruption injection in the SDK/RTOS fake. */
        if(injection==1)l->data[165]^=1;
        if(injection==2)++l->timestamp;
        if(injection==3)--l->data_len;
        if(injection==4){injected_metadata_buffer=l;injected_metadata_data=l->data;l->data=(uint8_t *)(uintptr_t)1;}
        if(injection==5)l->data_len=RAW_MAX_BYTES+1;
        run();assert(errors==be+1&&emits==bp&&!live&&last_error==ESP_ERR_INVALID_SIZE);
        assert(strstr(input_messages[0],"phase=submit ")&&strstr(input_messages[0],"recorded=1 hashed=1 bytes=657 ts=23443968"));
        assert(strstr(input_messages[3],"scope=original-sdk-buffer"));
        if(injection>=3){
            assert(strstr(packet_message,"stage=input-metadata")&&diagnostic_lines==4&&!rejected_count);
            assert(strstr(input_messages[1],"recorded=1 hashed=0")&&strstr(input_messages[2],"recorded=1 hashed=0"));
            assert(strstr(input_messages[3],"compare submit_worker=unavailable worker_fault=unavailable submit_fault=unavailable metadata_submit_worker=changed metadata_worker_fault=match"));
        }else{
            assert(strstr(packet_message,"stage=frame-layout")&&diagnostic_lines==27&&rejected_count==657);
            if(injection==1)assert(strstr(input_messages[3],"compare submit_worker=changed worker_fault=match submit_fault=changed"));
            else assert(strstr(input_messages[3],"compare submit_worker=match worker_fault=match submit_fault=match"));
            if(injection==2)assert(strstr(input_messages[3],"metadata_submit_worker=changed metadata_worker_fault=match"));
            else assert(strstr(input_messages[3],"metadata_submit_worker=match metadata_worker_fault=match"));
        }
    }
    /* Deliberate mutation after a worker sample, before the actual fault helper.
     * This separates during-processing changes from queued changes without a
     * timing-dependent thread or a production test hook. */
    start();unsigned bp=emits;be=errors;
    esp_a2d_audio_buff_t *late=buffer(sizeof(phone_input));memcpy(late->data,phone_input,sizeof(phone_input));late->timestamp=23443968u;
    chime_bt_decoder_submit(late,generation,CHIME_BT_CODEC_LDAC,48000,2);
    packet_t observed_packet;assert(xQueueReceive(raw_queue,&observed_packet,0)==pdTRUE);
    observed_packet.worker=input_evidence(late,&observed_packet.submitted);assert(observed_packet.worker.hashed);
    late->data[165]^=1;rejected_count=diagnostic_lines=0;
    ldac_layout_fail(&observed_packet,late->data+1,late->data_len-1,4,1,164,"invalid-or-truncated-frame");
    assert(errors==be+1&&emits==bp&&!live&&diagnostic_lines==27&&rejected_count==657);
    assert(strstr(input_messages[3],"compare submit_worker=match worker_fault=changed submit_fault=changed"));
    esp_a2d_audio_buff_free(late);run();
    /* A later metadata overwrite must not turn fault-only dumping into a
     * dereference of the corrupted pointer. Keep the original view only for
     * the already-bounded layout helper's three-byte fault header. */
    start();bp=emits;be=errors;
    late=buffer(sizeof(phone_input));memcpy(late->data,phone_input,sizeof(phone_input));
    chime_bt_decoder_submit(late,generation,CHIME_BT_CODEC_LDAC,48000,2);
    assert(xQueueReceive(raw_queue,&observed_packet,0)==pdTRUE);
    observed_packet.worker=input_evidence(late,&observed_packet.submitted);
    const uint8_t *original_view=late->data;
    injected_metadata_buffer=late;injected_metadata_data=late->data;late->data=(uint8_t *)(uintptr_t)1;
    rejected_count=diagnostic_lines=0;
    ldac_layout_fail(&observed_packet,original_view+1,656,4,1,164,"invalid-or-truncated-frame");
    assert(errors==be+1&&emits==bp&&!live&&diagnostic_lines==6&&!rejected_count);
    assert(strstr(capture_message,"capture=0/657; metadata changed; no payload dereference"));
    assert(strstr(input_messages[3],"compare submit_worker=match worker_fault=unavailable submit_fault=unavailable metadata_submit_worker=match metadata_worker_fault=changed"));
    esp_a2d_audio_buff_free(late);run();
    /* Invalid input is not hashed even before a submission sample exists. */
    start();bp=emits;be=errors;late=buffer(1);
    injected_metadata_buffer=late;injected_metadata_data=late->data;late->data=(uint8_t *)(uintptr_t)1;late->data_len=RAW_MAX_BYTES+1;
    diagnostic_lines=0;chime_bt_decoder_submit(late,generation,CHIME_BT_CODEC_LDAC,48000,2);run();
    assert(errors==be+1&&emits==bp&&diagnostic_lines==4&&strstr(packet_message,"stage=input-size"));
    assert(strstr(input_messages[0],"recorded=0 hashed=0")&&strstr(input_messages[2],"recorded=1 hashed=0"));
    /* Exact configured bound: full 4096-byte CRC, no overread or growth. */
    start();bp=emits;be=errors;late=buffer(RAW_MAX_BYTES);diagnostic_lines=0;
    chime_bt_decoder_submit(late,generation,CHIME_BT_CODEC_LDAC,48000,2);
    assert(count==1&&slots[head].submitted.crc32==0xc71c0011u);
    run();assert(errors==be+1&&emits==bp&&diagnostic_lines==4);
    assert(strstr(input_messages[3],"compare submit_worker=match worker_fault=match submit_fault=match"));
    /* A valid encoder packet with real submission/worker samples stays quiet
     * and emits the same one-frame amount of PCM; CRC is not an acceptance gate. */
    start();bp=emits;be=errors;uint8_t valid_header[3];
    f=fopen("software/tests/bluetooth/fixtures/ldac/48000-2.ldac","rb");assert(f&&fread(valid_header,1,3,f)==3);
    unsigned valid_bytes=(((valid_header[1]&7u)<<6)|(valid_header[2]>>2))+4u;
    late=buffer(valid_bytes+1);late->data[0]=1;memcpy(late->data+1,valid_header,3);
    assert(fread(late->data+4,1,valid_bytes-3,f)==valid_bytes-3);fclose(f);
    diagnostic_lines=0;capture=true;captured_bytes=0;
    chime_bt_decoder_submit(late,generation,CHIME_BT_CODEC_LDAC,48000,2);run();capture=false;
    assert(errors==be&&emits==bp+1&&captured_bytes==512&&live&&!diagnostic_lines);
    /* CRC comparison is diagnostic, not a new valid-input rejection policy.
     * Substitute another independently valid same-size encoded frame AFTER
     * submit. Worker observes different bytes yet follows the original decoder
     * contract, with no new steady-state UART or CRC-triggered restart. */
    start();bp=emits;be=errors;
    f=fopen("software/tests/bluetooth/fixtures/ldac/48000-2.ldac","rb");assert(f);
    late=buffer(valid_bytes+1);late->data[0]=1;assert(fread(late->data+1,1,valid_bytes,f)==valid_bytes);
    uint8_t replacement[515];assert(valid_bytes<=sizeof(replacement)&&fread(replacement,1,valid_bytes,f)==valid_bytes);fclose(f);
    assert(chime_ldac_frame_bytes(replacement,valid_bytes,48000)==valid_bytes&&memcmp(replacement,late->data+1,valid_bytes));
    diagnostic_lines=0;capture=true;captured_bytes=0;
    chime_bt_decoder_submit(late,generation,CHIME_BT_CODEC_LDAC,48000,2);
    uint32_t original_crc=slots[head].submitted.crc32;
    memcpy(late->data+1,replacement,valid_bytes);assert(input_crc32(late->data,late->data_len)!=original_crc);
    run();capture=false;
    assert(errors==be&&emits==bp+1&&captured_bytes==512&&live&&!diagnostic_lines);
    puts("PASS LDAC cross-stage evidence: IEEE CRC vector + captured packet, unchanged input, queued byte/timestamp changes, post-worker mutation, changed length/pointer rejected without reading, fault-only logs after invalidation and exact ownership");
#endif


#endif
    assert(opens>=closes&&decoded&&errors);puts("PASS actual external decoder worker: AAC/SBC configuration, bounds/progress/metadata errors, stale epochs, exact raw-buffer ownership, overflow and bounded teardown retention");
    return 0;
}

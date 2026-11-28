/* Reuse deterministic SDK/RTOS fakes, but link the REAL LHDC adapter/backend.
 * Exercise production process_packet -> PCM callback, queue, FIR and mixer.
 * Not a live radio/device capture. Only product-supported PCM16 44.1/48k. */
#define main unused_worker_regression_main
#include "test_chime_bt_decoder.c"
#undef main
#include "chime_bt_pcm.h"
#include "chime_mixer.h"
static FILE *callback_file;
static unsigned callback_count;
static void record_pcm(const uint8_t *p,uint32_t n,uint32_t epoch){
    assert(valid(epoch)&&n==960);assert(fwrite(p,1,n,callback_file)==n);callback_count++;
}
int main(int argc,char **argv){
    assert(argc==4);unsigned rate=atoi(argv[1]);assert(rate==44100||rate==48000);
    char path[1024];snprintf(path,sizeof(path),"%s.callback.pcm",argv[3]);callback_file=fopen(path,"wb");assert(callback_file);
    FILE *input=fopen(argv[2],"rb");assert(input);start();cb.pcm=record_pcm;
    unsigned seq=0;uint8_t hdr[2];
    while(fread(hdr,1,2,input)==2){
        unsigned n=(((unsigned)hdr[0]|((unsigned)hdr[1]<<8))&1023)*2+2;
        esp_a2d_audio_buff_t *b=buffer(n+2);b->data[0]=4;b->data[1]=(uint8_t)seq++;
        memcpy(b->data+2,hdr,2);assert(fread(b->data+4,1,n-2,input)==n-2);
        /* Same submit/queue ownership path as production; pump one queued packet. */
        chime_bt_decoder_submit(b,generation,CHIME_BT_CODEC_LHDC_V5,rate,2);assert(count==1);
        packet_t p;assert(xQueueReceive(raw_queue,&p,0)==pdTRUE);process_packet(&p);esp_a2d_audio_buff_free(p.buffer);assert(!errors);
    }
    fclose(input);fclose(callback_file);close_decoder();assert(chime_bt_decoder_stop());assert(allocs==frees);
    snprintf(path,sizeof(path),"%s.callback.pcm",argv[3]);input=fopen(path,"rb");assert(input);
    snprintf(path,sizeof(path),"%s.convert.pcm",argv[3]);FILE *conv=fopen(path,"wb");assert(conv);
    snprintf(path,sizeof(path),"%s.resampler.pcm",argv[3]);FILE *src=fopen(path,"wb");assert(src);
    snprintf(path,sizeof(path),"%s.mixer.pcm",argv[3]);FILE *mix=fopen(path,"wb");assert(mix);
    chime_bt_pcm_t q;assert(chime_bt_pcm_reset(&q,rate,2));chime_mixer_t m;chime_mixer_init(&m);
    uint8_t bytes[960];int32_t converted[480],filtered[256],mixed[256];unsigned rendered=0,failed=0;
    while(fread(bytes,1,sizeof(bytes),input)==sizeof(bytes)){
        chime_pcm16_q27(bytes,converted,240);
        for(unsigned i=0;i<480;i++){
            int v=(unsigned)bytes[2*i]|((unsigned)bytes[2*i+1]<<8);if(v>=32768)v-=65536;
            assert(converted[i]==v*4096); /* Exact unity, each sign and channel. */
        }
        assert(fwrite(converted,sizeof(*converted),480,conv)==480);
        assert(chime_bt_pcm_push(&q,bytes,sizeof(bytes),false));
        /* Fixed nominal clock isolates amplitude; no fabricated producer jitter.
         * Stop before FIR lookahead runs out; do not manufacture a silent tail. */
        while(q.count>=322&&(q.primed||q.count>=q.target_frames)){
            chime_bt_src_job_t job;
            if(!chime_bt_pcm_prepare_clocked(&q,&job,128,1000000ULL+rendered*1000000ULL/48000,false)){failed++;break;}
            chime_bt_src_process(&job,filtered,128);assert(fwrite(filtered,4,256,src)==256);
            chime_mixer_process(&m,NULL,filtered,false,true,0,CHIME_GAIN_ONE,false,mixed,128);
            assert(fwrite(mixed,4,256,mix)==256);rendered+=128;
        }
    }
    fclose(input);fclose(conv);fclose(src);fclose(mix);
    snprintf(path,sizeof(path),"%s.pipeline.json",argv[3]);FILE *j=fopen(path,"w");assert(j);
    fprintf(j,"{\"callback_frames\":%u,\"rendered_samples_per_channel\":%u,\"mixer_clipped_samples\":%u,\"unexpected_prepare_failures\":%u,\"discarded_frames\":%llu,\"zero_filled_samples\":[0,0],\"clock_ppm\":%d,\"tail_not_rendered_frames\":%zu}\n",callback_count,rendered,m.clipped_samples,failed,(unsigned long long)q.discarded_frames,q.ppm,q.count);fclose(j);
    return 0;
}


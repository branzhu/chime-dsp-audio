#include "chime_source.h"
#include "chime_bt_pcm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static chime_bt_pcm_t q;
static uint8_t block[CHIME_BT_QUEUE_FRAMES*4],out[128*4];
static void fill(int16_t l,int16_t r)
{
    for(size_t i=0;i<sizeof(block);i+=4){block[i]=(uint8_t)l;block[i+1]=(uint8_t)((uint16_t)l>>8);block[i+2]=(uint8_t)r;block[i+3]=(uint8_t)((uint16_t)r>>8);}
}
static void source_test(void)
{
    chime_source_t s;chime_source_init(&s);
    chime_source_update(&s,false,7,0);assert(s.selected==CHIME_SOURCE_USB);
    assert(!chime_source_admit(&s,false,true));assert(chime_source_admit(&s,true,true));
    assert(!chime_source_admit(&s,true,true));uint32_t generation=s.generation;
    chime_source_update(&s,false,7,99);assert(s.generation==generation); /* nonselected BT event */
    chime_source_update(&s,true,7,100);assert(s.selected==CHIME_SOURCE_BLUETOOTH&&s.generation!=generation);
    assert(!chime_source_admit(&s,true,false)); /* connected but paused still selected BT */
    assert(s.selected==CHIME_SOURCE_BLUETOOTH&&chime_source_admit(&s,true,true));generation=s.generation;
    chime_source_update(&s,true,999,100);assert(s.generation==generation); /* USB mute/reopen cannot affect BT */
    chime_source_update(&s,false,999,101);assert(s.selected==CHIME_SOURCE_USB&&chime_source_admit(&s,true,true));
    chime_source_update(&s,true,999,102);assert(chime_source_admit(&s,true,true));
    chime_source_update(&s,false,999,103);assert(chime_source_admit(&s,true,true)); /* USB epoch unchanged */
    chime_source_stop(&s);chime_source_update(&s,true,1000,104);assert(!chime_source_admit(&s,true,true));
    chime_source_update(&s,false,1001,105);assert(!chime_source_admit(&s,true,true));
    chime_source_retry(&s);assert(s.stopped&&!chime_source_admit(&s,true,true)); /* waking cannot clear STOP */
    chime_source_resume(&s);assert(chime_source_admit(&s,true,true));
    assert(!chime_source_admit(&s,true,true));
    chime_source_retry(&s);assert(chime_source_admit(&s,true,true));
    s.generation=UINT32_MAX;chime_source_resume(&s);assert(s.generation==1&&s.attempted==0);
    assert(!strcmp(chime_source_name(CHIME_SOURCE_BLUETOOTH),"bluetooth"));
    puts("PASS source policy: BT connection priority/paused ownership, selected epochs, unchanged USB epoch handback, STOP latch, explicit resume, generation wrap");
}
static void pcm_test(void)
{
    assert(!chime_bt_pcm_reset(&q,96000,2));assert(!chime_bt_pcm_reset(&q,48000,3));
    fill(INT16_MIN,INT16_MAX);
    assert(chime_bt_pcm_reset(&q,48000,2));
    assert(!chime_bt_pcm_render(&q,out,128,0));for(size_t i=0;i<sizeof(out);++i)assert(out[i]==0);
    assert(!chime_bt_pcm_push(&q,block,3,false));
    assert(chime_bt_pcm_push(&q,block,4096*4,false));assert(chime_bt_pcm_push(&q,block,4096*4,false));
    assert(!chime_bt_pcm_push(&q,block,4,false));
    assert(chime_bt_pcm_push(&q,block,4,true)&&q.count==CHIME_BT_QUEUE_FRAMES);
    assert(chime_bt_pcm_render(&q,out,128,0));
    for(size_t i=0;i<sizeof(out);i+=4)assert(out[i]==0&&out[i+1]==128&&out[i+2]==255&&out[i+3]==127);
    assert(chime_bt_pcm_reset(&q,44100,1));
    fill(12345,12345);assert(chime_bt_pcm_push(&q,block,q.target_frames*2U,false));assert(chime_bt_pcm_render(&q,out,128,0));
    for(size_t i=0;i<sizeof(out);i+=4)assert(out[i]==out[i+2]&&out[i+1]==out[i+3]);
    chime_bt_pcm_reset(&q,48000,2);assert(q.count==0&&q.phase==0&&!q.primed&&!q.ppm);
    /* Rebuffer preserves counters, discards residual input, and gates ALL
     * supported rates until 112ms, including mono and interpolation state. */
    const unsigned rates[]={16000,32000,44100,48000};
    for(unsigned r=0;r<4;++r)for(unsigned c=1;c<=2;++c){
        assert(chime_bt_pcm_reset(&q,rates[r],c));
        assert(chime_bt_pcm_push(&q,block,q.target_frames*c*2U,false));
        assert(chime_bt_pcm_render(&q,out,128,0));
        uint64_t pushed=q.pushed_frames,consumed=q.consumed_frames,rendered=q.rendered_frames;
        unsigned residual=(unsigned)q.count;
        chime_bt_pcm_rebuffer(&q,1000000);
        assert(q.pushed_frames==pushed&&q.consumed_frames==consumed&&q.rendered_frames==rendered);
        assert(q.discarded_frames==residual&&!q.count&&!q.phase&&!q.ppm&&!q.primed);
        assert(q.target_ms==112&&q.decay_at_us==1000000+CHIME_BT_TARGET_HOLD_US);
        assert(chime_bt_pcm_push(&q,block,(q.target_frames-1)*c*2U,false));
        assert(!chime_bt_pcm_render(&q,out,128,2000000));
        assert(chime_bt_pcm_push(&q,block,c*2U,false));
        assert(chime_bt_pcm_render(&q,out,128,2000000));
        assert(q.pushed_frames==q.consumed_frames+q.discarded_frames+q.count);
    }
    puts("PASS SBC PCM bounds/overflow/latest-frame drain, stereo extremes, mono duplication, zero prefill, rate/reset state");
}
static void drift(unsigned rate,int ppm)
{
    assert(chime_bt_pcm_reset(&q,rate,2));fill(1234,-1234);
    assert(chime_bt_pcm_push(&q,block,q.target_frames*4U,false));
    uint64_t accumulator=0;size_t minimum=CHIME_BT_QUEUE_FRAMES,maximum=0;
    for(unsigned i=0;i<225000;++i){ /* ten simulated minutes per rate / drift */
        accumulator+=(uint64_t)rate*128U*(uint64_t)(1000000+ppm);
        unsigned n=(unsigned)(accumulator/(48000ULL*1000000ULL));accumulator%=48000ULL*1000000ULL;
        uint64_t now=(uint64_t)i*128000000ULL/48000U;
        assert(chime_bt_pcm_push(&q,block,n*4U,false));chime_bt_pcm_arrival(&q,now);
        assert(chime_bt_pcm_render(&q,out,128,now));
        if(q.count<minimum)minimum=q.count;if(q.count>maximum)maximum=q.count;
        assert(q.ppm>=-2000&&q.ppm<=2000);
    }
    assert(maximum<CHIME_BT_QUEUE_FRAMES&&minimum>=rate*24U/1000U);
    assert(q.target_ms==CHIME_BT_TARGET_MIN_MS&&q.target_decays>0);
    printf("PASS 10min simulated ASRC rate=%u remote_ppm=%d queue=%zu..%zu correction=%dppm\n",rate,ppm,minimum,maximum,q.ppm);
}
int main(void)
{
    source_test();pcm_test();
    const unsigned rates[]={16000,32000,44100,48000};
    for(unsigned i=0;i<4;++i){drift(rates[i],300);drift(rates[i],-300);}
    puts("No Bluetooth radio, RTOS timing or analog sound simulated.");return 0;
}

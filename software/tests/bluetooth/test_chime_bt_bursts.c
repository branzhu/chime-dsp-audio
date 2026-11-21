/* Actual PCM queue/SRC, synthetic timing only: NOT a replay of phone packets.
 * Packet audio durations change without changing sample rate. No codec fake
 * can hide a short input: failed render must preserve PCM and emit all zeros. */
#include "chime_bt_pcm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static chime_bt_pcm_t q;
static uint8_t pcm[768*4],out[128*4];
static unsigned packet_frames(unsigned packet,unsigned profile)
{
    static const unsigned sizes[]={512,640,768};
    return profile?sizes[(packet/200)%3]:512;
}
static uint64_t delivery_us(uint64_t frames,unsigned rate,int ppm,unsigned packet,unsigned profile)
{
    /* Exact floor, without overflowing frames*1e12 in the long quiet test. */
    uint64_t numerator=frames*1000000ULL,denominator=(uint64_t)rate*(unsigned)(1000000+ppm);
    uint64_t t=numerator/denominator*1000000ULL+(numerator%denominator)*1000000ULL/denominator;
    if(profile)t+=(packet%7)*500U; /* bounded, ordered below */
    unsigned hold=profile==2?55000U:(profile==3||(profile==4&&t>=650000000ULL))?71000U:0U;
    if(hold&&t%2000000ULL<hold)t=t/2000000ULL*2000000ULL+hold;
    return t;
}
static void simulate(unsigned rate,int ppm,unsigned profile)
{
    assert(chime_bt_pcm_reset(&q,rate,2));
    for(unsigned i=0;i<sizeof(pcm);i+=4){pcm[i]=0xd2;pcm[i+1]=4;pcm[i+2]=0x2e;pcm[i+3]=0xfb;}
    unsigned packet=0,n=packet_frames(0,profile),gaps=0,events=0,run=0,max_run=0,prefill=0;
    uint64_t packet_end=n,due=delivery_us(packet_end,rate,ppm,packet,profile),previous_due=0;
    unsigned iterations=profile==4?303750U:67500U,gaps_before_final_minute=0;
    bool reached_low_latency=false;
    for(unsigned i=0;i<iterations;++i){ /* 180s, or 810s with jitter onset at 650s */
        uint64_t now=(uint64_t)i*128000000ULL/48000U;
        while(due<=now){
            assert(chime_bt_pcm_push(&q,pcm,n*4U,false));
            chime_bt_pcm_arrival(&q,due);
            previous_due=due;++packet;n=packet_frames(packet,profile);packet_end+=n;
            due=delivery_us(packet_end,rate,ppm,packet,profile);
            if(due<previous_due)due=previous_due; /* FIFO, no packet reordering */
        }
        size_t count=q.count,head=q.head;uint32_t phase=q.phase;
        bool was_primed=q.primed;
        bool ready=chime_bt_pcm_render(&q,out,128,now);
        if(!ready){
            if(was_primed){if(!run)++events;++gaps;++run;if(run>max_run)max_run=run;}
            else ++prefill;
            assert(q.count==count&&q.head==head&&q.phase==phase);
            for(unsigned k=0;k<sizeof(out);++k)assert(!out[k]);
            if(was_primed)assert(q.needed>count);
            else assert(count<q.target_frames);
        }else{
            uint32_t base=(uint32_t)((uint64_t)rate*65536U/48000U);
            uint32_t step=(uint32_t)((int64_t)base*(1000000+q.ppm)/1000000);
            unsigned consumed=(phase+128U*step)>>16;
            assert(q.phase==((phase+128U*step)&65535U));
            assert(q.count==count-consumed&&q.head==(head+consumed)%CHIME_BT_QUEUE_FRAMES);
            run=0;
            for(unsigned k=0;k<sizeof(out);k+=4)assert(!memcmp(out+k,pcm,4));
        }
        if(now>600000000ULL&&now<650000000ULL&&q.target_ms<=60U)reached_low_latency=true;
        if(i==iterations-22500U)gaps_before_final_minute=gaps;
        assert(q.pushed_frames==q.consumed_frames+q.count);
        assert(q.rendered_frames==(uint64_t)(i+1-gaps-prefill)*128U);
        assert(q.target_ms>=CHIME_BT_TARGET_MIN_MS&&q.target_ms<=CHIME_BT_TARGET_MAX_MS);
        assert(q.ppm>=-2000&&q.ppm<=2000&&q.clamp_blocks<=i+1);
    }
    assert(q.render_min<=q.render_max&&q.render_max<CHIME_BT_QUEUE_FRAMES);
    if(profile<4)assert(!gaps&&!events&&!max_run); /* INCLUDING 55/71ms holds */
    else{
        /* A first UNSEEN late burst cannot be predicted after latency decays.
         * Assert learning/recovery, not a misleading zero-gap claim. */
        if(!(reached_low_latency&&gaps&&events&&max_run<38))fprintf(stderr,"late rate=%u ppm=%d low=%u gaps=%u events=%u max=%u target=%u\n",rate,ppm,(unsigned)reached_low_latency,gaps,events,max_run,q.target_ms);
        assert(reached_low_latency&&gaps&&events&&max_run<38);
        assert(gaps==gaps_before_final_minute&&q.target_ms>CHIME_BT_TARGET_MIN_MS);
    }
    assert(prefill&&prefill<188); /* original 500ms startup timeout remains applicable */
    if(profile>=2)assert(q.target_raises||q.target_ms>=CHIME_BT_TARGET_START_MS);
    else assert(q.target_decays);
    printf("PASS synthetic burst SRC rate=%u remote_ppm=%d profile=%u events=%u zero_blocks=%u max_run=%u queue=%u..%u correction=%d clamp=%lu target_ms=%u raises=%lu decays=%lu prefill=%u\n",
        rate,ppm,profile,events,gaps,max_run,q.render_min,q.render_max,q.ppm,(unsigned long)q.clamp_blocks,q.target_ms,(unsigned long)q.target_raises,(unsigned long)q.target_decays,prefill);
}
static void supply(unsigned frames)
{
    while(frames){unsigned n=frames>768?768:frames;assert(chime_bt_pcm_push(&q,pcm,n*4U,false));frames-=n;}
}
static void policy_boundaries(void)
{
    const unsigned rates[]={16000,32000,44100,48000};
    for(unsigned r=0;r<4;++r){
        unsigned rate=rates[r];assert(chime_bt_pcm_reset(&q,rate,2));
        assert(q.target_ms==96&&q.target_frames==(rate*96+999)/1000);
        chime_bt_pcm_arrival(&q,0); /* zero is a valid first timestamp */
        q.phase=123;q.head=7;uint32_t phase=q.phase;size_t head=q.head,count=q.count;
        chime_bt_pcm_arrival(&q,71000);
        assert(q.target_ms==103&&q.target_raises==1);
        assert(q.phase==phase&&q.head==head&&q.count==count&&!q.primed);
        assert(q.filtered_error<0); /* no stale speed-up after raising reference */
        chime_bt_pcm_arrival(&q,1000000);assert(q.target_ms==112&&q.target_frames==(rate*112+999)/1000);
        chime_bt_pcm_arrival(&q,2000000);assert(q.target_ms==112&&q.target_raises==2);
        assert(!chime_bt_pcm_render(&q,out,128,2000000)&&!q.primed&&!q.short_input);
        assert(q.phase==phase&&q.head==head&&q.count==count);
        assert(chime_bt_pcm_reset(&q,rate,2));
        assert(q.target_ms==96&&!q.target_raises&&!q.target_decays&&!q.clock_seen&&!q.arrival_seen);
        assert(!q.phase&&!q.head&&!q.count);
    }
    assert(chime_bt_pcm_reset(&q,48000,2));
    supply(q.target_frames-1);assert(!chime_bt_pcm_render(&q,out,128,0)&&!q.primed);
    supply(1);chime_bt_pcm_arrival(&q,0);assert(chime_bt_pcm_render(&q,out,128,0)&&q.primed);
    chime_bt_pcm_arrival(&q,71000);uint64_t deadline=q.decay_at_us;
    assert(q.target_ms==103&&deadline==71000ULL+CHIME_BT_TARGET_HOLD_US);
    assert(chime_bt_pcm_render(&q,out,128,deadline-1)&&q.target_ms==103&&!q.target_decays);
    assert(chime_bt_pcm_render(&q,out,128,deadline)&&q.target_ms==102&&q.target_decays==1);
    assert(chime_bt_pcm_render(&q,out,128,deadline)&&q.target_ms==102&&q.target_decays==1);
    assert(q.decay_at_us==deadline+CHIME_BT_TARGET_DECAY_US);
    puts("PASS adaptive policy: input-rate-scaled prefill/target, zero-time first arrival, 71ms raises to103ms, 112ms cap/headroom, phase/data preserved on target change, reset");
}
int main(void)
{
    policy_boundaries();
    const unsigned rates[]={44100,48000};const int ppms[]={-300,0,300};
    for(unsigned r=0;r<2;++r)for(unsigned p=0;p<3;++p)for(unsigned profile=0;profile<5;++profile)
        simulate(rates[r],ppms[p],profile);
    puts("No radio/RTOS/I2S or phone clock measured; synthetic hold tests are not a claimed hardware dropout fix.");
    return 0;
}

#include "chime_bt_flow.h"
#include "chime_bt_pcm.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static chime_bt_pcm_t q;
static uint8_t pcm[128*4];
static void bins(void)
{
    chime_bt_flow_t f;chime_bt_flow_reset(&f,100);
    assert(f.first_pcm_us==CHIME_BT_FLOW_UNSEEN&&f.first_render_us==CHIME_BT_FLOW_UNSEEN);
    chime_bt_flow_record(&f,99,123,0,0,123,4608);assert(!f.bins[0].valid);
    chime_bt_flow_record(&f,100,128,0,1,128,4608);
    chime_bt_flow_record(&f,20099,128,0,0,256,4608);
    assert(f.bins[0].pushed==256&&f.bins[0].queue_max==256&&f.bins[0].packets==1);
    chime_bt_flow_record(&f,20100,0,128,0,128,4608);
    assert(f.bins[1].consumed==128&&f.bins[1].pushed==0);
    assert(f.first_pcm_us==0&&f.first_render_us==20000&&f.prime_queue==256&&f.prime_target==4608);
    chime_bt_flow_record(&f,320100,64,0,0,192,4608);
    assert(f.bins[0].index==16&&f.bins[0].pushed==64&&f.bins[0].packets==0);
    assert(f.first_pcm_us==0&&f.prime_queue==256);
    chime_bt_flow_reset(&f,0);assert(!f.bins[0].valid&&f.first_pcm_us==CHIME_BT_FLOW_UNSEEN);
    puts("PASS flow diagnostics: exact 20ms edges, zero timestamp, ring wrap, reset, first-prime capture");
}
static void capacity_counterexample(void)
{
    /* NOT a model of the reported phone/RF trace: demonstrate only that a
     * finite queue can overflow from burst admission without executing a
     * slow FIR. 8 queued LDAC packets x 4 frames x 128 samples = 4096 frames,
     * less than RAW_DEPTH=12 packets. No automatic drop/recovery is added. */
    chime_bt_flow_t f;chime_bt_flow_reset(&f,0);
    assert(chime_bt_pcm_reset(&q,48000,2));
    for(unsigned i=0;i<36;++i)assert(chime_bt_pcm_push(&q,pcm,sizeof(pcm),false));
    chime_bt_src_job_t job;assert(chime_bt_pcm_prepare(&q,&job,128,0));
    chime_bt_flow_record(&f,0,4608,128,0,(uint32_t)q.count,q.target_frames);
    assert(q.count==4480&&f.prime_queue==4608);
    unsigned admitted=0;
    for(unsigned i=0;i<32;++i){
        bool ok=chime_bt_pcm_push(&q,pcm,sizeof(pcm),false);
        if(!ok)break;
        admitted+=128;chime_bt_flow_record(&f,1000,128,0,0,(uint32_t)q.count,q.target_frames);
    }
    assert(q.count==8192&&admitted==3712&&q.pushed_frames==q.count+q.consumed_frames);
    assert(!chime_bt_pcm_push(&q,pcm,sizeof(pcm),false));
    assert(f.bins[0].queue_max==8192&&f.bins[0].consumed==128);
    puts("PASS capacity counterexample: 96ms prefill plus <8 LDAC-sized packets can fill PCM with no slow FIR; synthetic, NOT proof of observed cause");
    /* Normal pacing at 128 input frames/128 output frames has no growth. */
    assert(chime_bt_pcm_reset(&q,48000,2));
    for(unsigned i=0;i<36;++i)assert(chime_bt_pcm_push(&q,pcm,sizeof(pcm),false));
    for(unsigned i=0;i<375;++i){
        assert(chime_bt_pcm_prepare(&q,&job,128,(uint64_t)i*8000U/3U));
        assert(chime_bt_pcm_push(&q,pcm,sizeof(pcm),false));
        assert(q.count==4608);
    }
    puts("PASS same finite queue at nominal realtime pacing: no occupancy growth");
}
static void decoupled_clock(void)
{
    chime_bt_src_job_t job;memset(pcm,0,sizeof(pcm));
    assert(chime_bt_pcm_reset(&q,48000,2));
    for(unsigned i=0;i<40;++i)assert(chime_bt_pcm_push(&q,pcm,sizeof(pcm),false));
    int before=q.ppm;int32_t error=q.filtered_error;
    for(unsigned i=0;i<4;++i)assert(chime_bt_pcm_prepare_clocked(&q,&job,128,1000,false));
    assert(q.ppm==before&&q.filtered_error==error&&q.consumed_frames==512);
    /* Output observes PCM plus the prepared/in-flight input-frame budget. */
    chime_bt_pcm_control(&q,(unsigned)(q.count+q.consumed_frames));
    assert(q.ppm==1&&q.filtered_error==512);
    puts("PASS decoupled drift control: producer burst does not advance controller; output cadence accounts prepared/in-flight input frames");
}
int main(void){bins();capacity_counterexample();decoupled_clock();return 0;}




/* MSVC's C11 atomic ABI does not satisfy the upstream lock-free static assert.
 * Mock ONLY the quality collector for this facade test. The lifecycle/core and
 * wrapper are real. This is NOT validation of target atomics/collection. */
#include "chime_reference_codec.h"
#include "dbt_lhdcv5_codec.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static dbt_lhdcv5_quality_t value;
static bool started,busy;
static unsigned starts;
void dbt_lhdcv5_quality_start(void){started=true;++starts;}
bool dbt_lhdcv5_quality_snapshot(dbt_lhdcv5_quality_t *out){
    if(!started||busy)return false;
    *out=value;return true;
}
int main(void){
    chime_reference_quality_t out,saved;
    memset(&out,0xa5,sizeof(out));saved=out;
    assert(!chime_reference_quality_snapshot(NULL));
    assert(!chime_reference_quality_snapshot(&out)&&!memcmp(&out,&saved,sizeof(out)));
    void *d=chime_reference_open(0,48000,2);assert(d&&!starts);chime_reference_close(d);
    d=chime_reference_open(1,48000,2);assert(d&&starts==1);
    value=(dbt_lhdcv5_quality_t){.attempts=11,.rejected=12,.channel0_errors=13,.channel1_silenced=14,
        .peak_concealed=15,.nonfinite_frames=16,.clipped_frames=17,.clipped_samples=18,.event_attempt=19,
        .frame_bytes=252,.flags=0x14a,.peak_f32_bits=0x3f800000,.result=-2,.channel0_result=-3,.channel1_result=-4};
    assert(chime_reference_quality_snapshot(&out));
    assert(out.attempts==11&&out.rejected==12&&out.channel0_errors==13&&out.channel1_silenced==14);
    assert(out.peak_concealed==15&&out.nonfinite_frames==16&&out.clipped_frames==17&&out.clipped_samples==18);
    assert(out.event_attempt==19&&out.frame_bytes==252&&out.flags==0x14a&&out.peak_f32_bits==0x3f800000);
    assert(out.result==-2&&out.channel0_result==-3&&out.channel1_result==-4);
    saved=out;busy=true;assert(!chime_reference_quality_snapshot(&out)&&!memcmp(&out,&saved,sizeof(out)));busy=false;
    chime_reference_close(d);d=chime_reference_open(1,44100,2);assert(d&&starts==2);
    assert(chime_reference_quality_snapshot(&out)&&out.attempts==11);chime_reference_close(d);
    puts("PASS reference quality facade: LHDC-only startup, every event field, no reset on reopen, unavailable/contended output unchanged (quality collector MOCKED)");
    return 0;
}

/* Same coefficients, taps and continuous phase; bounded rounding difference.
 * Host PIE model is NOT actual instruction or board timing evidence. */
#include "chime_bt_src_internal.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static chime_bt_src_job_t job;
static int32_t scalar[256],candidate[256];
static unsigned maximum,cases;
static uint32_t random_state=0x9e3779b9U;
static uint32_t random_value(void){random_state=random_state*1664525U+1013904223U;return random_state;}
static void compare(size_t frames)
{
    chime_bt_src_scalar(&job,scalar,frames);chime_bt_src_candidate(&job,candidate,frames);
    for(size_t i=0;i<frames*2;++i){int64_t error=(int64_t)scalar[i]-candidate[i];if(error<0)error=-error;
        assert(error<=26);if((unsigned)error>maximum)maximum=(unsigned)error;}
    ++cases;
}
int main(void)
{
    static const uint32_t blend[]={0,1,0x7fffff,0x800000,0xffffff};
    static const int64_t steps[]={0,-8589934,-4295,4295,8589934};
    for(unsigned pattern=0;pattern<5;++pattern){
        for(unsigned k=0;k<CHIME_SRC_WINDOW;++k)for(unsigned ch=0;ch<2;++ch)
            job.pcm[k][ch]=pattern==0?INT16_MIN:pattern==1?INT16_MAX:pattern==2?
                ((k+ch)%2?INT16_MIN:INT16_MAX):pattern==3?(int16_t)(random_value()>>16):0;
        job.rate=48000;
        for(unsigned row=0;row<256;++row)for(unsigned b=0;b<5;++b)for(unsigned si=0;si<5;++si){
            job.phase=(row<<24)|blend[b];job.step=(uint64_t)((int64_t)(1ULL<<32)+steps[si]);compare(4);
        }
    }
    /* Full blocks, fractional/integer endpoints, random adversarial PCM. */
    for(unsigned i=0;i<1024;++i){
        for(unsigned k=0;k<CHIME_SRC_WINDOW;++k)for(unsigned ch=0;ch<2;++ch)job.pcm[k][ch]=(int16_t)(random_value()>>16);
        job.phase=random_value();job.step=(uint64_t)((int64_t)(1ULL<<32)+steps[i%5]);compare(128);
    }
    memset(job.pcm,0,sizeof(job.pcm));compare(128);for(unsigned i=0;i<256;++i)assert(!candidate[i]);
    printf("PASS endpoint FIR interpolation: %u cases, max error %u Q27 LSB <=26 (-134dBFS bound), unchanged Q30/192 taps/continuous phase; NOT bit-exact or hardware timing\n",cases,maximum);
}

/* Host mathematical model, NOT an emulator of PIE instructions. Target startup
 * independently compares real PIE raw sums and Q27 outputs against scalar C. */
#include "chime_bt_src_internal.h"
#include <assert.h>
#ifdef CHIME_SRC_TEST_ACCEL_GATE
extern bool chime_src_test_bad_dot;
#else
bool chime_bt_src_pie_enabled(void){return true;}
#endif
void chime_bt_src_dot_pie(const int16_t pcm[2][192],const int16_t halves[2][192],int64_t parts[4])
{
    for(unsigned half=0;half<2;++half)for(unsigned ch=0;ch<2;++ch){
        int64_t sum=0;
        for(unsigned k=0;k<192;++k){sum+=(int64_t)pcm[ch][k]*halves[half][k];
            assert(sum>=-(1LL<<39)&&sum<(1LL<<39));}
        parts[half*2+ch]=sum;
    }
#ifdef CHIME_SRC_TEST_ACCEL_GATE
    if(chime_src_test_bad_dot)++parts[0];
#endif
}

void chime_bt_src_dot_pie_planar(const int16_t *left,const int16_t *right,
    const int16_t halves[2][192],int64_t parts[4])
{
    for(unsigned half=0;half<2;++half)for(unsigned ch=0;ch<2;++ch){
        int64_t sum=0;const int16_t *pcm=ch?right:left;
        for(unsigned k=0;k<192;++k)sum+=(int64_t)pcm[k]*halves[half][k];
        parts[half*2+ch]=sum;
    }
}

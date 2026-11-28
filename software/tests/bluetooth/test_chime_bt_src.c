#include "chime_bt_src.h"
#ifdef CHIME_SRC_PIE_MODEL
#include "chime_bt_src_internal.h"
#define chime_bt_src_process chime_bt_src_candidate
#endif
#include "chime_mixer.h"
#include "../../main/chime_bt_src_coeff.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#define PI 3.14159265358979323846
static void compare_reference(const chime_bt_src_job_t *job,const int32_t *out,size_t frames)
{
    unsigned bank=job->rate<=32000?0U:job->rate==44100?1U:2U;
    uint64_t phase=job->phase;
    for(size_t f=0;f<frames;++f){
        unsigned row=(uint32_t)phase>>24;uint32_t blend=(uint32_t)phase&0xffffffU;
        size_t base=(size_t)(phase>>32);int64_t sum[2]={0,0};
        assert(base+CHIME_SRC_TAPS<=CHIME_SRC_WINDOW);
        for(unsigned k=0;k<CHIME_SRC_TAPS;++k){
            int32_t a=src_coeff[bank][row][k],b=src_coeff[bank][row+1][k];
            int32_t c=a+(int32_t)((int64_t)(b-a)*blend/16777216);
            for(unsigned ch=0;ch<2;++ch)sum[ch]+=(int64_t)job->pcm[base+k][ch]*c;
        }
        for(unsigned ch=0;ch<2;++ch){
            int32_t expected=(int32_t)((sum[ch]+(sum[ch]<0?-131072:131072))/262144);
            if(out[f*2+ch]!=expected){
                fprintf(stderr,"SRC mismatch rate=%u phase=%08x step=%llu f=%zu ch=%u actual=%d expected=%d\n",
                    job->rate,job->phase,(unsigned long long)job->step,f,ch,out[f*2+ch],expected);
                assert(out[f*2+ch]==expected);
            }
        }
        phase+=job->step;
    }
}
static void tone(unsigned rate,double frequency,double image)
{
    chime_bt_src_job_t job;job.rate=rate;job.step=((uint64_t)rate<<32)/48000U;
    uint64_t phase=0;double real=0,imag=0,spur_re=0,spur_im=0;int32_t out[256];
    for(unsigned block=0;block<375;++block){
        size_t base=(size_t)(phase>>32);job.phase=(uint32_t)phase;
        for(unsigned k=0;k<CHIME_SRC_WINDOW;++k){double v=sin(2*PI*frequency*(double)(base+k)/rate)*16000;
            job.pcm[k][0]=(int16_t)lround(v);job.pcm[k][1]=(int16_t)-job.pcm[k][0];}
        chime_bt_src_process(&job,out,128);
        for(unsigned i=0;i<128;++i){
            double t=(double)(block*128+i)/48000;
            double v=(double)out[i*2]/(16000*4096.0);
            double w=2*PI*frequency*t,im=2*PI*image*t;
            real+=v*cos(w);imag+=v*sin(w);spur_re+=v*cos(im);spur_im+=v*sin(im);
            assert(out[i*2]==-out[i*2+1]);
        }
        phase+=128*job.step;
    }
    double gain=20*log10(hypot(real,imag)*2/48000);
    double rejection=20*log10(fmax(hypot(spur_re,spur_im)*2/48000,1e-15));
    printf("SRC rate=%u tone=%.0fHz gain=%+.6fdB image=%.0fHz %.2fdBFS(relative tone)\n",rate,frequency,gain,image,rejection);
    assert(fabs(gain)<.05);assert(rejection< -90);
}
/* Compare the optimized kernel to the original full-width signed divide.
 * Exercise every phase row/bank and fractional endpoints, independently of
 * the spectral probes. This guards RV32 optimizations against quantization
 * changes: the expected result is bit-for-bit identical, not just close. */
static void exact_reference(void)
{
    static const unsigned rates[]={16000,32000,44100,48000};
    static const uint32_t blends[]={0,1,0x7fffff,0xffffff};
    chime_bt_src_job_t job={0};int32_t out[8];uint32_t random=1234567;
    for(unsigned k=0;k<CHIME_SRC_WINDOW;++k){
        random=random*1664525U+1013904223U;
        job.pcm[k][0]=(int16_t)((int)(random>>16)-32768);
        job.pcm[k][1]=k%2?INT16_MIN:INT16_MAX;
    }
    for(unsigned r=0;r<4;++r){
        job.rate=rates[r];job.step=((uint64_t)job.rate<<32)/48000U;
        for(unsigned index=0;index<CHIME_SRC_PHASES;++index){
            for(unsigned j=0;j<4;++j){
                job.phase=(index<<24)|blends[j];
                chime_bt_src_process(&job,out,4);
                compare_reference(&job,out,4);
            }
        }
    }
    puts("PASS SRC bit-exact original reference: four rates, all phase rows, fractional endpoints, random/extreme PCM");
}
/* Exercise the 48 kHz row cache with slow and fast fractional phase movement.
 * These cases deliberately hit blend=0, powers of two and the exact-divisibility
 * boundaries where the negative-delta correction cannot be elided. */
static void exact_48k_steps(void)
{
    static const uint32_t blends[]={0,1,2,4,8,16,32,64,128,256,512,1024,2048,
        4096,8192,16384,32768,65536,131072,262144,524288,1048576,2097152,
        4194304,8388608,0x7fffff,0xffffff};
    static const uint64_t steps[]={((uint64_t)48000<<32)/48000U,
        (((uint64_t)48000<<32)/48000U)-1,(((uint64_t)48000<<32)/48000U)+1,
        (((uint64_t)48000<<32)/48000U)-1200000,(((uint64_t)48000<<32)/48000U)+1200000};
    chime_bt_src_job_t job={0};int32_t out[16];uint32_t random=0x9e3779b9U;
    job.rate=48000;
    for(unsigned k=0;k<CHIME_SRC_WINDOW;++k){random=random*1664525U+1013904223U;
        job.pcm[k][0]=(int16_t)(random>>16);job.pcm[k][1]=(int16_t)((int)(random&0xffffU)-32768);}
    for(unsigned row=0;row<CHIME_SRC_PHASES;++row){
        for(unsigned si=0;si<sizeof(steps)/sizeof(steps[0]);++si){job.step=steps[si];
            for(unsigned bi=0;bi<sizeof(blends)/sizeof(blends[0]);++bi){
                /* Both sides of every power-of-two boundary. */
                for(int offset=-1;offset<=1;++offset){
                    int64_t blend=(int64_t)blends[bi]+offset;
                    if(blend<0||blend>0xffffff)continue;
                    job.phase=(row<<24)|(uint32_t)blend;
                    chime_bt_src_process(&job,out,8);compare_reference(&job,out,8);
                }
            }
        }
    }
    static const int ppm[]={0,-1,1,-300,300,-512,512,-2000,2000};
    static const uint32_t phases[]={0,1,0x00ffffff,0x7fffffff,0xff000000,0xffffffff};
    static const size_t lengths[]={0,1,3,127,128};
    int32_t block[256];
    for(unsigned pattern=0;pattern<3;++pattern){
        for(unsigned k=0;k<CHIME_SRC_WINDOW;++k){
            random=random*1664525U+1013904223U;
            job.pcm[k][0]=pattern==0?(int16_t)((int)(random>>16)-32768):
                pattern==1?(k%2?INT16_MIN:INT16_MAX):INT16_MIN;
            job.pcm[k][1]=pattern==2?INT16_MAX:(int16_t)~job.pcm[k][0];
        }
        for(unsigned pi=0;pi<sizeof(ppm)/sizeof(ppm[0]);++pi){
            job.step=(uint64_t)((int64_t)(1ULL<<32)*(1000000+ppm[pi])/1000000);
            for(unsigned ph=0;ph<sizeof(phases)/sizeof(phases[0]);++ph){
                job.phase=phases[ph];
                for(unsigned n=0;n<sizeof(lengths)/sizeof(lengths[0]);++n){
                    chime_bt_src_process(&job,block,lengths[n]);
                    compare_reference(&job,block,lengths[n]);
                }
            }
        }
    }
    puts("PASS SRC 48k row-cache bit-exact drift, all rows, full blocks, +/-2000ppm, powers-of-two and exact boundaries");
}
int main(void)
{
    exact_reference();
    exact_48k_steps();
    tone(16000,3000,13000);tone(32000,12000,20000);tone(44100,20000,23900);
    chime_bt_src_job_t job;memset(&job,0,sizeof(job));job.rate=48000;job.step=1ULL<<32;
    for(unsigned i=0;i<CHIME_SRC_WINDOW;++i)job.pcm[i][0]=job.pcm[i][1]=12345;
    int32_t a[256],b[256];chime_bt_src_process(&job,a,128);
    job.phase=0xffffffffU;chime_bt_src_process(&job,b,128);
    for(unsigned i=0;i<256;++i){assert(a[i]==12345*4096);assert(abs(b[i]-12345*4096)<32);}
    puts("PASS production FIR: passband/image probes, stereo symmetry, Q27 DC, phase endpoints; not a hardware or exhaustive spectral measurement");return 0;
}

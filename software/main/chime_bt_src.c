#include "chime_bt_src.h"
#include "chime_bt_src_coeff.h"
#include "chime_bt_src_internal.h"
#include <string.h>
#ifdef ESP_PLATFORM
#include "esp_attr.h"
#if CHIME_SRC_HAS_PIE
#include "esp_cpu.h"
#endif
#else
#define IRAM_ATTR
#endif
_Static_assert(CHIME_SRC_HISTORY==CHIME_SRC_TAPS,"SRC window/tap mismatch");
/* Signed high word of delta * unsigned Q32 blend. This is MULHSU on RV32;
 * unsigned extraction avoids implementation-defined signed right shifts. */
static inline int32_t interpolate_floor(int32_t delta,uint32_t blend)
{
    int64_t product=(int64_t)delta*(int64_t)(blend<<8);
    uint32_t high=(uint32_t)((uint64_t)product>>32);
    return high<=INT32_MAX?(int32_t)high:-1-(int32_t)(UINT32_MAX-high);
}
/* Preserve the original Q24 division's truncation toward zero, including
 * negative deltas whose product has an exactly zero fractional remainder. */
static inline int32_t interpolate(int32_t delta,uint32_t blend)
{
    return (int32_t)((int64_t)delta*blend/16777216);
}
static inline void output_frame(int32_t *out,int64_t left,int64_t right)
{
    out[0]=(int32_t)((left+(left<0?-131072:131072))/262144);
    out[1]=(int32_t)((right+(right<0?-131072:131072))/262144);
}
/* At 48 kHz the drift controller moves slowly between phase rows. Stage one
 * row in task-local RAM rather than rereading two flash rows for every output
 * sample. No shared mutable cache, allocation, queue or clock-policy changes.
 *
 * For d<0, trunc(d*b/2^24) = floor(d*b/2^24)+1 unless 2^24 divides d*b.
 * Cache a+(d<0), and use one signed-high multiply in the common FIR loop.
 * If L is the largest power of two dividing any negative d in this row,
 * b & (2^24/L-1) != 0 proves that NONE of those products is integral. All
 * exceptional blends take the exact original interpolation instead.
 * This is an integer identity, not an approximation of the coefficients. */
static void IRAM_ATTR process_48k(const chime_bt_src_job_t *job,int32_t *out,size_t frames,bool pie)
{
    struct {int32_t base,delta;} row[CHIME_SRC_TAPS];
    (void)pie;
    unsigned cached_index=CHIME_SRC_PHASES;
    uint32_t exact_mask=0;
    uint64_t phase=job->phase;
    bool fixed_phase=job->step==(1ULL<<32);
    for(size_t f=0;f<frames;++f){
        size_t base=(size_t)(phase>>32);uint32_t frac=(uint32_t)phase;
        unsigned index=frac>>24;uint32_t blend=frac&0xffffffU;
        if(index!=cached_index){
            uint32_t max_lowbit=1;
            for(unsigned k=0;k<CHIME_SRC_TAPS;++k){
                int32_t a=src_coeff[2][index][k];
                int32_t delta=src_coeff[2][index+1][k]-a;
                row[k].delta=delta;
                row[k].base=fixed_phase?a+interpolate(delta,blend):a+(delta<0);
                if(delta<0){
                    uint32_t magnitude=0U-(uint32_t)delta;
                    uint32_t lowbit=magnitude&(0U-magnitude);
                    if(lowbit>max_lowbit)max_lowbit=lowbit;
                }
            }
            /* L >= 2^24 makes at least one negative product always integral;
             * force the exact path even if future coefficient banks change. */
            exact_mask=max_lowbit>=0x1000000U?0U:(0x1000000U/max_lowbit)-1U;
            cached_index=index;
        }
        int64_t left=0,right=0;
        if(fixed_phase){
            /* Integer step still filters the fractional phase: NOT a bypass. */
            for(unsigned k=0;k<CHIME_SRC_TAPS;++k){
                int32_t c=row[k].base;
                left+=(int64_t)job->pcm[base+k][0]*c;
                right+=(int64_t)job->pcm[base+k][1]*c;
            }
        }else if(blend&exact_mask){
            /* Every negative product is fractional: adjusted base plus floor
             * is exactly the original truncation result, with no remainder
             * multiply/correction in the hot loop. */
            for(unsigned k=0;k<CHIME_SRC_TAPS;++k){
                int32_t c=row[k].base+interpolate_floor(row[k].delta,blend);
                left+=(int64_t)job->pcm[base+k][0]*c;
                right+=(int64_t)job->pcm[base+k][1]*c;
            }
        }else{
            /* blend 0 and the sparse exact-divisibility boundary use the
             * original truncation correction. */
            for(unsigned k=0;k<CHIME_SRC_TAPS;++k){
                int32_t delta=row[k].delta;
                int32_t c=row[k].base-(delta<0)+interpolate(delta,blend);
                left+=(int64_t)job->pcm[base+k][0]*c;
                right+=(int64_t)job->pcm[base+k][1]*c;
            }
        }
        output_frame(out+f*2,left,right);
        phase+=job->step;
    }
}
void IRAM_ATTR chime_bt_src_scalar(const chime_bt_src_job_t *job,int32_t *out,size_t frames)
{
    if(job->rate==48000){process_48k(job,out,frames,false);return;}
    /* Non-unity ratios change rows frequently: avoid the RAM copy there. */
    unsigned bank=job->rate<=32000?0U:job->rate==44100?1U:2U;
    uint64_t phase=job->phase;
    for(size_t f=0;f<frames;++f){
        size_t base=(size_t)(phase>>32);uint32_t frac=(uint32_t)phase;
        unsigned index=frac>>24;uint32_t blend=frac&0xffffffU;
        int64_t sum[2]={0,0};
        for(unsigned k=0;k<CHIME_SRC_TAPS;++k){
            int32_t a=src_coeff[bank][index][k],b=src_coeff[bank][index+1][k];
            int32_t c=a+interpolate(b-a,blend);
            for(unsigned ch=0;ch<2;++ch)sum[ch]+=(int64_t)job->pcm[base+k][ch]*c;
        }
        output_frame(out+f*2,sum[0],sum[1]);
        phase+=job->step;
    }
}

#if CHIME_SRC_HAS_PIE
/* CPU1-only candidate: transpose a block once into eight alignment lanes.
 * Lane l stores input[l+i], so selecting lane base%8 and offset base&~7
 * gives aligned sliding windows without copying 192 stereo samples for EACH
 * output point. CPU1-owned fixed scratch is prepared before playback.
 * No hot-path allocation, approximation, changed phase or FIR taps. */
#if CHIME_SRC_DOT_INTERPOLATION
#include "chime_bt_src_packed_48k.h"
/* Signed quotient/remainder decomposition avoids a 64x24-bit product that
 * would overflow int64. Both products below fit; signs agree, so separate
 * truncations sum to trunc(delta*blend/2^24) exactly. */
static inline int64_t blend_sums(int64_t a,int64_t b,uint32_t blend)
{
    int64_t delta=b-a;
    return a+(delta/16777216)*(int64_t)blend+((delta%16777216)*(int64_t)blend)/16777216;
}
#endif
enum {SRC_PIE_STRIDE=((CHIME_SRC_WINDOW+7U)/8U)*8U};
typedef struct {
    _Alignas(16) int16_t lanes[8][2][SRC_PIE_STRIDE];
} src_workspace_t;
#ifdef ESP_PLATFORM
/* Single explicitly pinned CPU1 producer owns this workspace. Do not take
 * RTC memory is byte-addressable but actual S31 PIE loads stall there. Reserve
 * this explicitly SIMD-accessed buffer in main SRAM; scalar ready blocks are
 * placed in RTC RAM instead. Total INTERNAL free is NOT a sufficient budget. */
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
static src_workspace_t *workspace;
static TaskHandle_t workspace_owner;
bool chime_bt_src_workspace_begin(void)
{
    if(workspace)return workspace_owner==xTaskGetCurrentTaskHandle();
    workspace=heap_caps_aligned_calloc(16,1,sizeof(*workspace),
        MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT|MALLOC_CAP_DMA);
    if(workspace)workspace_owner=xTaskGetCurrentTaskHandle();
    return workspace!=NULL;
}
void chime_bt_src_workspace_end(void)
{
    if(workspace&&workspace_owner==xTaskGetCurrentTaskHandle()){
        heap_caps_free(workspace);workspace=NULL;workspace_owner=NULL;
    }
}
#else
/* Host mathematical model is invoked serially; not a concurrency benchmark. */
static src_workspace_t host_workspace;
bool chime_bt_src_workspace_begin(void){return true;}
void chime_bt_src_workspace_end(void){}
#endif
void IRAM_ATTR chime_bt_src_candidate(const chime_bt_src_job_t *job,int32_t *out,size_t frames)
{
    if(job->rate!=48000){chime_bt_src_scalar(job,out,frames);return;}
#ifdef ESP_PLATFORM
    if(!workspace||workspace_owner!=xTaskGetCurrentTaskHandle()){
        chime_bt_src_scalar(job,out,frames);return;
    }
    src_workspace_t *w=workspace;
#else
    src_workspace_t *w=&host_workspace;
#endif
    enum {STRIDE=SRC_PIE_STRIDE};
    int16_t (*lanes)[2][STRIDE]=w->lanes;
    _Alignas(16) int16_t halves[2][CHIME_SRC_TAPS];
    _Alignas(16) int64_t parts[4];
    struct {int32_t base,delta;} row[CHIME_SRC_TAPS];
    size_t needed=frames?(size_t)((job->phase+(frames-1)*job->step)>>32)+CHIME_SRC_HISTORY:0;
    for(unsigned lane=0;lane<8;++lane)for(unsigned i=0;i<STRIDE;++i){
        unsigned input=lane+i;
        lanes[lane][0][i]=input<needed?job->pcm[input][0]:0;
        lanes[lane][1][i]=input<needed?job->pcm[input][1]:0;
    }
#if CHIME_SRC_DOT_INTERPOLATION
    _Alignas(16) int16_t endpoint[2][2][CHIME_SRC_TAPS];
    unsigned cached_index=CHIME_SRC_PHASES;uint64_t phase=job->phase;
    (void)halves;(void)row;
    for(size_t f=0;f<frames;++f){
        size_t base=(size_t)(phase>>32);uint32_t frac=(uint32_t)phase;
        unsigned index=frac>>24;uint32_t blend=frac&0xffffffU;
        if(index!=cached_index){
            /* PIE cannot read mapped flash or RTC memory. These complete
             * exact signed-half rows are staged once per phase-row change. */
            memcpy(endpoint[0],src_packed_48k[index],sizeof(endpoint[0]));
            memcpy(endpoint[1],src_packed_48k[index+1],sizeof(endpoint[1]));
            cached_index=index;
        }
        const int16_t *left=lanes[base&7U][0]+(base&~7U),*right=lanes[base&7U][1]+(base&~7U);
        chime_bt_src_dot_pie_planar(left,right,endpoint[0],parts);
        int64_t a=parts[0]*65536+parts[2],b=parts[1]*65536+parts[3];
        chime_bt_src_dot_pie_planar(left,right,endpoint[1],parts);
        output_frame(out+f*2,blend_sums(a,parts[0]*65536+parts[2],blend),
            blend_sums(b,parts[1]*65536+parts[3],blend));
        phase+=job->step;
    }
#else
    unsigned cached_index=CHIME_SRC_PHASES;uint32_t exact_mask=0;
    uint64_t phase=job->phase;bool fixed_phase=job->step==(1ULL<<32);
    for(size_t f=0;f<frames;++f){
        size_t base=(size_t)(phase>>32);uint32_t frac=(uint32_t)phase;
        unsigned index=frac>>24;uint32_t blend=frac&0xffffffU;
        if(index!=cached_index){
            uint32_t max_lowbit=1;
            for(unsigned k=0;k<CHIME_SRC_TAPS;++k){
                int32_t a=src_coeff[2][index][k],delta=src_coeff[2][index+1][k]-a;
                row[k].delta=delta;
                row[k].base=fixed_phase?a+interpolate(delta,blend):a+(delta<0);
                if(delta<0){uint32_t magnitude=0U-(uint32_t)delta,lowbit=magnitude&(0U-magnitude);
                    if(lowbit>max_lowbit)max_lowbit=lowbit;}
            }
            exact_mask=max_lowbit>=0x1000000U?0U:(0x1000000U/max_lowbit)-1U;
            cached_index=index;
        }
        for(unsigned k=0;k<CHIME_SRC_TAPS;++k){
            int32_t c=fixed_phase?row[k].base:(blend&exact_mask)?
                row[k].base+interpolate_floor(row[k].delta,blend):
                row[k].base-(row[k].delta<0)+interpolate(row[k].delta,blend);
            int32_t low=(int32_t)((uint32_t)c&65535U);low=(low^32768)-32768;
            int32_t high=(int32_t)(((uint32_t)c+32768U)>>16);high=(high^32768)-32768;
            halves[0][k]=(int16_t)high;halves[1][k]=(int16_t)low;
        }
        chime_bt_src_dot_pie_planar(lanes[base&7U][0]+(base&~7U),
            lanes[base&7U][1]+(base&~7U),halves,parts);
        output_frame(out+f*2,parts[0]*65536+parts[2],parts[1]*65536+parts[3]);
        phase+=job->step;
    }
#endif
}
#endif
void IRAM_ATTR chime_bt_src_process(const chime_bt_src_job_t *job,int32_t *out,size_t frames)
{
#if CHIME_SRC_HAS_PIE
    if(job->rate==48000&&chime_bt_src_pie_enabled()){
#ifdef ESP_PLATFORM
        /* A standalone benchmark must not migrate the CPU0 renderer. */
        if(esp_cpu_get_core_id()!=1){chime_bt_src_scalar(job,out,frames);return;}
#endif
        chime_bt_src_candidate(job,out,frames);return;
    }
#endif
    chime_bt_src_scalar(job,out,frames);
}

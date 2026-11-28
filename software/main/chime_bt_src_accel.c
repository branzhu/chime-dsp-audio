#include "chime_bt_src_internal.h"
#include <stdatomic.h>
#include <string.h>
static atomic_uint probe_flags;
static chime_bt_src_accel_stats_t probe_stats;
void chime_bt_src_accel_snapshot(chime_bt_src_accel_stats_t *out)
{
    if(!out)return;
    unsigned flags=atomic_load_explicit(&probe_flags,memory_order_acquire);
    memset(out,0,sizeof(*out));
    if(flags){*out=probe_stats;out->complete=true;}
}
static void probe_done(unsigned reason)
{
    probe_stats.reason=reason;
    atomic_store_explicit(&probe_flags,1,memory_order_release);
}
#if CHIME_SRC_HAS_PIE
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#ifdef ESP_PLATFORM
#include "esp_cpu.h"
#endif
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdlib.h>
/* Explicit standalone CPU1 benchmark only. Never invoke from the PCM
 * worker: S31 PIE use irreversibly changes task affinity, even on fallback. */
static bool enabled;
bool chime_bt_src_pie_enabled(void){return enabled;}
typedef struct {
    chime_bt_src_job_t job;
    uint8_t alignment_pad[16-sizeof(chime_bt_src_job_t)%16];
    int32_t scalar[256],pie[256];
    _Alignas(16) int16_t pcm[2][192],halves[2][192];
    _Alignas(16) int64_t parts[4];
} probe_t;
static bool probe_dot(probe_t *p)
{
    uint32_t random=0x9e3779b9U;
    for(unsigned pattern=0;pattern<5;++pattern){
        for(unsigned ch=0;ch<2;++ch)for(unsigned k=0;k<192;++k){
            random=random*1664525U+1013904223U;
            p->pcm[ch][k]=pattern==0?INT16_MIN:pattern==1?INT16_MAX:
                pattern==2?(k%2?INT16_MIN:INT16_MAX):(int16_t)(random>>16);
            random=random*1664525U+1013904223U;
            p->halves[ch][k]=pattern==0?INT16_MIN:pattern==1?INT16_MAX:
                pattern==2?(k%3?INT16_MAX:INT16_MIN):(int16_t)(random>>16);
        }
        chime_bt_src_dot_pie(p->pcm,p->halves,p->parts);
        for(unsigned half=0;half<2;++half)for(unsigned ch=0;ch<2;++ch){
            int64_t sum=0;
            for(unsigned k=0;k<192;++k)sum+=(int64_t)p->pcm[ch][k]*p->halves[half][k];
            if(sum!=p->parts[half*2+ch]){
                ESP_LOGE("chime_src","ACCEL raw mismatch pattern=%u half=%u ch=%u expected=%lld actual=%lld",
                    pattern,half,ch,(long long)sum,(long long)p->parts[half*2+ch]);
                return false;
            }
        }
    }
    return true;
}
static void fill_pcm(probe_t *p,unsigned pattern)
{
    uint32_t random=0x12345678U;
    for(unsigned k=0;k<CHIME_SRC_WINDOW;++k)for(unsigned ch=0;ch<2;++ch){
        random=random*1664525U+1013904223U;
        p->job.pcm[k][ch]=pattern==0?(int16_t)(random>>16):
            pattern==1?(k%2?INT16_MIN:INT16_MAX):(ch?INT16_MAX:INT16_MIN);
    }
}
static bool compare(probe_t *p,size_t frames)
{
    chime_bt_src_scalar(&p->job,p->scalar,frames);
    chime_bt_src_candidate(&p->job,p->pie,frames);
    for(size_t i=0;i<frames*2;++i){
        int64_t error=(int64_t)p->scalar[i]-p->pie[i];if(error<0)error=-error;
        if((uint64_t)error>probe_stats.max_error_q27)probe_stats.max_error_q27=(unsigned)error;
        if((uint64_t)error>CHIME_SRC_ERROR_BOUND_Q27){
        ESP_LOGE("chime_src","ACCEL output exceeds bound phase=%08lx step=%llu sample=%u scalar=%ld pie=%ld",
            (unsigned long)p->job.phase,(unsigned long long)p->job.step,(unsigned)i,
            (long)p->scalar[i],(long)p->pie[i]);
        return false;
    }
    }
    return true;
}
static bool probe_src(probe_t *p,unsigned *cases)
{
    static const uint32_t blends[]={0,1,0x7fffff,0x800000,0xffffff};
    static const int offsets[]={0,-1,1,-8589934,8589934};
    p->job.rate=48000;fill_pcm(p,0);
    for(unsigned row=0;row<256;++row){
        for(unsigned si=0;si<5;++si){
            p->job.step=(uint64_t)((int64_t)(1ULL<<32)+offsets[si]);
            for(unsigned bi=0;bi<5;++bi){
                p->job.phase=(row<<24)|blends[bi];
                if(!compare(p,3))return false;
                ++*cases;
            }
        }
        /* Keep startup/control tasks and the watchdog serviced. Outside all
         * measurement intervals; this is not part of the playback path. */
        if((row&7U)==7U)vTaskDelay(1);
    }
    for(unsigned pattern=0;pattern<3;++pattern){fill_pcm(p,pattern);
        for(unsigned si=0;si<5;++si){p->job.step=(uint64_t)((int64_t)(1ULL<<32)+offsets[si]);
            p->job.phase=0xffabcdefU;
            if(!compare(p,128))return false;
            ++*cases;
        }
    }
    return true;
}
static uint32_t median(uint32_t *v)
{
    for(unsigned i=1;i<7;++i){uint32_t x=v[i];unsigned j=i;
        while(j&&v[j-1]>x){v[j]=v[j-1];--j;}v[j]=x;}
    return v[3];
}
void chime_bt_src_accel_init(void)
{
    enabled=false;atomic_store(&probe_flags,0);memset(&probe_stats,0,sizeof(probe_stats));
#ifdef ESP_PLATFORM
    if(esp_cpu_get_core_id()!=1){
        ESP_LOGW("chime_src","ACCEL selected=scalar reason=probe-requires-dedicated-CPU1-task; no PIE instruction executed");
        probe_done(2);return;
    }
#endif
    probe_t *p=heap_caps_aligned_calloc(16,1,sizeof(*p),MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT);
    if(!p){ESP_LOGW("chime_src","ACCEL selected=scalar reason=probe-no-memory");probe_done(3);return;}
    if(!chime_bt_src_workspace_begin()){free(p);probe_done(6);return;}
    probe_stats.error_bound_q27=CHIME_SRC_ERROR_BOUND_Q27;
    probe_stats.bit_exact=CHIME_SRC_ERROR_BOUND_Q27==0;
    unsigned cases=0;
    if(!probe_dot(p)||!probe_src(p,&cases)){
        ESP_LOGE("chime_src","ACCEL selected=scalar reason=bit-exact-selftest-failed cases=%u",cases);
        free(p);chime_bt_src_workspace_end();probe_done(4);return;
    }
    fill_pcm(p,0);p->job.phase=0x12345678U;
    p->job.step=(1ULL<<32)+1288490; /* Representative +300 ppm, not unity shortcut. */
    uint32_t scalar[7],pie[7];
    for(unsigned i=0;i<7;++i){
        vTaskDelay(1);
        int64_t t=esp_timer_get_time();
        /* Alternate order to reduce systematic cache/interrupt bias. */
        if(i&1U)chime_bt_src_candidate(&p->job,p->pie,128);
        else chime_bt_src_scalar(&p->job,p->scalar,128);
        uint32_t first=(uint32_t)(esp_timer_get_time()-t);
        t=esp_timer_get_time();
        if(i&1U)chime_bt_src_scalar(&p->job,p->scalar,128);
        else chime_bt_src_candidate(&p->job,p->pie,128);
        uint32_t second=(uint32_t)(esp_timer_get_time()-t);
        scalar[i]=(i&1U)?second:first;pie[i]=(i&1U)?first:second;
    }
    uint32_t scalar_us=median(scalar),pie_us=median(pie);
    enabled=(uint64_t)pie_us*100U<(uint64_t)scalar_us*95U;
    ESP_LOGI("chime_src","ACCEL selected=%s validated_cases=%u scalar_us=%lu pie_us=%lu frames=128 median=7; error_bound_Q27=%u max_error_Q27=%u; WALL times, startup probe not playback guarantee",
        enabled?"pie":"scalar",cases,(unsigned long)scalar_us,(unsigned long)pie_us,CHIME_SRC_ERROR_BOUND_Q27,probe_stats.max_error_q27);
    probe_stats.enabled=enabled;probe_stats.exact_cases=cases;
    probe_stats.scalar_us=scalar_us;probe_stats.pie_us=pie_us;
    free(p);if(!enabled)chime_bt_src_workspace_end();probe_done(enabled?0:5);
}
void chime_bt_src_accel_deinit(void){enabled=false;chime_bt_src_workspace_end();}
#else
void chime_bt_src_accel_deinit(void){}
void chime_bt_src_accel_init(void){probe_done(1);}
#endif

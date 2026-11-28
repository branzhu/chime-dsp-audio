#include "chime_render.h"
#include "chime_usb.h"
#include "chime_bluetooth.h"
#include "chime_mixer.h"
#include "chime_pcm.h"
#include "chime_controls.h"
#include "bench_audio.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
#include "chime_clock_follow.h"
static chime_clock_servo_t clock_servo;
static bool clock_follow;
static esp_err_t clock_end(void)
{
    clock_follow=false;chime_clock_servo_reset(&clock_servo);
    return bench_audio_clock_reset();
}
#endif
#include "esp_timer.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include <stdatomic.h>
#include <string.h>
static SemaphoreHandle_t lock;
static atomic_bool running,quitting,stop_requested;
static bool enabled,bt_started,usb_primed;
static atomic_bool usb_only;
static uint32_t usb_epoch,bt_epoch;
static uint64_t usb_prime_at,usb_starved_at;
static atomic_int output_error;
static chime_mixer_t mixer;
static chime_render_stats_t stats;
static uint64_t src_started_us;
static portMUX_TYPE stats_mux=portMUX_INITIALIZER_UNLOCKED;
static void worker(void *arg)
{
    (void)arg;uint8_t raw[512];int32_t usb[256]={0},bt[256]={0},out[256];
    int uv=1,bv=1;int32_t ug=0,bg=0;uint64_t previous_begin=0,unpaced_at=0;
    /* Never run a PIE probe here: on S31 it forces migration to CPU1 even
     * when the probe ultimately selects scalar, starving the CPU1 decoder. */
    ESP_LOGI("chime_src","PCM worker ready core=%u priority=6 src=scalar pie_probe=disabled; decoder uses CPU1 in dual-core builds",
        (unsigned)xPortGetCoreID());
    while(!atomic_load(&quitting)){
        xSemaphoreTake(lock,portMAX_DELAY);
        if(atomic_load(&stop_requested))enabled=false;
        if(!enabled){
            /* Idle data must not reappear after manual sine/STOP. */
            for(unsigned i=0;i<16&&chime_usb_fifo();++i)chime_usb_read(raw,sizeof(raw));
            previous_begin=unpaced_at=0;
            xSemaphoreGive(lock);vTaskDelay(pdMS_TO_TICKS(1));continue;
        }
        uint64_t begin=(uint64_t)esp_timer_get_time();bool ur=false,br=false;
        uint32_t period=previous_begin?(uint32_t)(begin-previous_begin):0,src_us=0;
        previous_begin=begin;
        uint32_t ue=chime_usb_epoch(),be=chime_bluetooth_epoch();
        if(ue!=usb_epoch){chime_usb_clear();usb_epoch=ue;usb_primed=false;usb_prime_at=begin;usb_starved_at=0;}
        if(chime_usb_wanted()){
            uint16_t available=chime_usb_fifo();
            if(!usb_primed&&available>=1024)usb_primed=true;
            if(!usb_primed){if(begin-usb_prime_at>100000ULL)chime_usb_fail(ESP_ERR_TIMEOUT);}
            else {
                memset(raw,0,sizeof(raw));uint16_t n=chime_usb_read(raw,sizeof(raw));
                if(n%4)chime_usb_fail(ESP_ERR_INVALID_SIZE);
                else {
                    ur=n>0;chime_pcm16_q27(raw,usb,128);
                    if(n<sizeof(raw)){if(!usb_starved_at)usb_starved_at=begin;
                        if(begin-usb_starved_at>100000ULL)chime_usb_fail(ESP_ERR_TIMEOUT);}
                    else usb_starved_at=0;
                }
            }
        }
        /* Consume muted USB normally; only its gain is zero. */
        if(ue!=chime_usb_epoch())ur=false;
        bool wanted=!usb_only&&chime_bluetooth_wanted();
        if(bt_started&&(!wanted||be!=bt_epoch)){
            chime_bluetooth_output_end();bt_started=false;
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
            output_error=clock_end();if(output_error!=ESP_OK)enabled=false;
#endif
        }
        if(enabled&&wanted&&!bt_started){
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
            chime_bluetooth_status_t candidate;chime_bluetooth_snapshot(&candidate);
            clock_follow=bench_audio_clock_available()&&candidate.codec==CHIME_BT_CODEC_LDAC&&candidate.rate==48000&&candidate.channels==2;
            chime_clock_servo_reset(&clock_servo);
            esp_err_t admission=chime_bluetooth_output_begin_clocked(clock_follow);
            if(admission!=ESP_OK)clock_follow=false;
#else
            esp_err_t admission=chime_bluetooth_output_begin();
#endif
            if(admission==ESP_OK){bt_started=true;bt_epoch=be;}
        }
        if(bt_started){
            uint64_t src_begin=(uint64_t)esp_timer_get_time();
            portENTER_CRITICAL(&stats_mux);src_started_us=src_begin;stats.src_active=true;portEXIT_CRITICAL(&stats_mux);
            esp_err_t err=chime_bluetooth_render_q27(bt,128,&br);
            src_us=(uint32_t)((uint64_t)esp_timer_get_time()-src_begin);
            portENTER_CRITICAL(&stats_mux);stats.src_active=false;
            if(br){++stats.src_blocks;stats.src_total_us+=src_us;}
            portEXIT_CRITICAL(&stats_mux);
            if(err!=ESP_OK||bt_epoch!=chime_bluetooth_epoch())br=false;
        }
        if(ue!=chime_usb_epoch())ur=false;
        if(bt_epoch!=chime_bluetooth_epoch())br=false;
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
        if(clock_follow){
            chime_bluetooth_status_t cs;chime_bluetooth_snapshot(&cs);
            bool valid=br&&cs.clock_follow&&cs.output_active&&cs.epoch==bt_epoch&&!atomic_load(&stop_requested);
            int ppm=chime_clock_servo_update(&clock_servo,begin,cs.queued_frames+128U,cs.target_frames,valid);
            esp_err_t trim=bench_audio_clock_set_ppm(ppm);
            if(trim!=ESP_OK){output_error=trim;enabled=false;br=false;}
            portENTER_CRITICAL(&stats_mux);stats.clock_follow=valid;stats.clock_command_ppm=ppm;
            stats.clock_rate_q16=bench_audio_clock_rate_q16();portEXIT_CRITICAL(&stats_mux);
        }else{
            portENTER_CRITICAL(&stats_mux);stats.clock_follow=false;stats.clock_command_ppm=0;
            stats.clock_rate_q16=bench_audio_clock_rate_q16();portEXIT_CRITICAL(&stats_mux);
        }
#endif
        int u=chime_usb_volume(),b=chime_bluetooth_volume();
        if(u!=uv){ug=chime_pcm_gain_q30(u);uv=u;}
        if(b!=bv){bg=b<=-96*256?0:chime_pcm_gain_q30(b);bv=b;}
        chime_mixer_process(&mixer,usb,bt,ur,br,
            chime_usb_muted()?0:ug,bg,
            chime_controls_muted()||chime_controls_standby(),out,128);
        uint32_t compute=(uint32_t)((uint64_t)esp_timer_get_time()-begin);
        /* STOP has priority even if it arrived during FIR computation. */
        uint64_t write_begin=(uint64_t)esp_timer_get_time();
        if(atomic_load(&stop_requested))enabled=false;
        else if(enabled){output_error=bench_audio_pcm_write(out,128);if(output_error!=ESP_OK)enabled=false;}
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
        if(!enabled&&clock_follow){
            esp_err_t restore=clock_end();if(restore!=ESP_OK&&output_error==ESP_OK)output_error=restore;
            portENTER_CRITICAL(&stats_mux);stats.clock_follow=false;stats.clock_command_ppm=0;
            stats.clock_rate_q16=bench_audio_clock_rate_q16();portEXIT_CRITICAL(&stats_mux);
        }
#endif
        uint64_t completed=(uint64_t)esp_timer_get_time();
        uint32_t write_us=(uint32_t)(completed-write_begin);
        /* Normally DMA blocks and schedules control/decoder tasks for us.
         * With sustained overload it may never block at all. Bound that CPU
         * monopolization without penalizing every individual deadline miss.
         * Wall-time write duration is a heuristic, not proof of RTOS blocking. */
        bool recovery_yield=false;
        if(!enabled||write_us>=100U)unpaced_at=0;
        else if(!unpaced_at)unpaced_at=begin;
        else if(completed-unpaced_at>=20000ULL){recovery_yield=true;unpaced_at=completed;}
        portENTER_CRITICAL(&stats_mux);
        ++stats.blocks;stats.usb_short+=chime_usb_wanted()&&!ur?1U:0U;
        stats.bt_short+=wanted&&!br?1U:0U;stats.clips=mixer.clipped_samples;stats.peak=mixer.peak;
        stats.last_compute_us=compute;stats.last_src_us=src_us;stats.last_write_us=write_us;
        stats.last_period_us=period;stats.unpaced_yields+=recovery_yield?1U:0U;
        if(src_us>stats.max_src_us)stats.max_src_us=src_us;
        if(write_us>stats.max_write_us)stats.max_write_us=write_us;
        if(period>stats.max_period_us)stats.max_period_us=period;
        if(compute>stats.max_compute_us)stats.max_compute_us=compute;
        if(compute>2667U)++stats.deadline_misses;
        portEXIT_CRITICAL(&stats_mux);
        bool idle=!enabled;
        xSemaphoreGive(lock);
        /* No per-deadline sleep: allow DMA backlog to catch up immediately.
         * Retain a bounded fairness escape for sustained unpaced overload so
         * a lower-priority owner can run and request STOP before its mutex. */
        if(idle||recovery_yield)vTaskDelay(pdMS_TO_TICKS(1));
    }
    atomic_store(&running,false);vTaskDelete(NULL);
}
esp_err_t chime_render_start(void)
{
    if(lock)return ESP_ERR_INVALID_STATE;
    lock=xSemaphoreCreateMutex();if(!lock)return ESP_ERR_NO_MEM;
    atomic_store(&quitting,false);atomic_store(&stop_requested,true);enabled=false;
    atomic_store(&running,true);
    /* Scalar FIR belongs on CPU0. CPU1 is reserved for the codec worker;
     * explicit affinity also makes later lazy FPU pinning deterministic. */
    if(xTaskCreatePinnedToCore(worker,"source_pcm",8192,NULL,6,NULL,0)!=pdPASS){
        atomic_store(&running,false);vSemaphoreDelete(lock);lock=NULL;return ESP_ERR_NO_MEM;}
    return ESP_OK;
}
esp_err_t chime_render_begin(bool only_usb)
{
    if(!lock||(!chime_usb_wanted()&&(only_usb||!chime_bluetooth_wanted())))return ESP_ERR_INVALID_STATE;
    atomic_store(&stop_requested,true);
    xSemaphoreTake(lock,portMAX_DELAY);
    if(bt_started)chime_bluetooth_output_end();
    bt_started=false;
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    esp_err_t restore=clock_end();
    if(restore!=ESP_OK){enabled=false;output_error=restore;xSemaphoreGive(lock);return restore;}
#endif
    usb_only=only_usb;usb_epoch=chime_usb_epoch();bt_epoch=chime_bluetooth_epoch();
    chime_usb_clear();usb_primed=false;usb_prime_at=(uint64_t)esp_timer_get_time();usb_starved_at=0;
    chime_mixer_init(&mixer);output_error=ESP_OK;
    portENTER_CRITICAL(&stats_mux);memset(&stats,0,sizeof(stats));portEXIT_CRITICAL(&stats_mux);
    atomic_store(&stop_requested,false);enabled=true;xSemaphoreGive(lock);return ESP_OK;
}
void chime_render_end(void)
{
    if(!lock)return;
    atomic_store(&stop_requested,true); /* before taking mutex: prevents priority starvation */
    xSemaphoreTake(lock,portMAX_DELAY);enabled=false;
    if(bt_started)chime_bluetooth_output_end();
    bt_started=false;
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    esp_err_t restore=clock_end();if(restore!=ESP_OK)output_error=restore;
    portENTER_CRITICAL(&stats_mux);stats.clock_follow=false;stats.clock_command_ppm=0;
    stats.clock_rate_q16=bench_audio_clock_rate_q16();portEXIT_CRITICAL(&stats_mux);
#endif
    xSemaphoreGive(lock);
}
esp_err_t chime_render_poll(bool *finished)
{
    *finished=false;if(!lock)return ESP_ERR_INVALID_STATE;
    esp_err_t err=atomic_load(&output_error);bool only=atomic_load(&usb_only);
    if(err!=ESP_OK)return err;
    return chime_usb_wanted()||(!only&&chime_bluetooth_wanted())?ESP_OK:ESP_ERR_INVALID_STATE;
}
void chime_render_snapshot(chime_render_stats_t *s)
{
    uint64_t sampled_us=(uint64_t)esp_timer_get_time();
    portENTER_CRITICAL(&stats_mux);*s=stats;
    s->src_inflight_us=stats.src_active&&sampled_us>=src_started_us?(uint32_t)(sampled_us-src_started_us):0;
    portEXIT_CRITICAL(&stats_mux);
}
void chime_render_stop(void)
{
    if(!lock)return;
    chime_render_end();atomic_store(&quitting,true);
    while(atomic_load(&running))vTaskDelay(pdMS_TO_TICKS(1));
    vSemaphoreDelete(lock);lock=NULL;
}

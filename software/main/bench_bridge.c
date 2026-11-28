#include "bench_bridge.h"
#include "bench_audio.h"
#include "chime_usb.h"
#include "chime_source.h"
#include "chime_indicator.h"
#include "chime_controls.h"
#include "chime_stream.h"
#include "chime_render.h"
#include "chime_bluetooth.h"
#include "sdkconfig.h"
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
#include "chime_bt_decoder.h"
#endif
#if CONFIG_CHIME_BT_LHDC_V5
#include "chime_reference_codec.h"
#endif
#include "chime_dsp_boot.h"
#include "bench_sound.h"
#include "generated/chime_dsp_parameters.h"
#include <errno.h>
#include "bench_dsp.h"
#include "bench_web.h"
#include "chime_network.h"
#include "sigma_tcp_bridge.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include "lwip/sockets.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BENCH_PORT 8086
static const char *TAG="chime_bridge";

typedef struct {
    board_context_t *board;
    int socket;
    uint32_t network_epoch;
    uint64_t request_deadline;
    uint32_t commands;
    bool usb_mode; /* legacy name: shared continuous PCM mode */
    bool mixed_mode;
    bool controls_cancelled; /* expected physical cancellation, not a guard fault */
    uint32_t stream_epoch;
    bool external;
    unsigned tuning_armed; /* physical amp banks only, no DSP gain map */
    bench_sound_t *sound;
    bool output_interrupted_usb; /* preserve resume intent across poll cancellation */
    bench_dsp_t dsp;
} bench_session_t;

static uint64_t now_us(void *context)
{
    (void)context;
    return (uint64_t)esp_timer_get_time();
}
static bool cancelled(void *context)
{
    bench_session_t *s=context;
    if(s->socket<0)return false; /* local boot/USB never depends on Wi-Fi */
    return !chime_network_ready() || chime_network_epoch()!=s->network_epoch ||
           (s->request_deadline && now_us(NULL)>=s->request_deadline);
}
static void wait_us(void *context,uint32_t duration)
{
    (void)context;
    esp_rom_delay_us(duration);
}
static int read_io(void *context,uint16_t address,uint8_t *data,size_t bytes,uint32_t timeout)
{
    bench_session_t *s=context;
    if(cancelled(s)) return ESP_ERR_INVALID_STATE;
    const uint8_t sub[]={(uint8_t)(address>>8),(uint8_t)address};
    return i2c_master_transmit_receive(s->board->adau1451,sub,2,data,bytes,(int)timeout);
}
static int write_io(void *context,uint16_t address,const uint8_t *data,size_t bytes,uint32_t timeout)
{
    bench_session_t *s=context;
    if(cancelled(s) || bytes>DSP_TRANSFER_CHUNK_BYTES) return ESP_ERR_INVALID_SIZE;
    uint8_t tx[DSP_TRANSFER_CHUNK_BYTES+2];
    tx[0]=(uint8_t)(address>>8); tx[1]=(uint8_t)address;
    memcpy(tx+2,data,bytes);
    esp_err_t err=i2c_master_transmit(s->board->adau1451,tx,bytes+2,(int)timeout);
    if(err==ESP_OK && address==0xf400 && bytes>=2 && (data[1]&1U)) vTaskDelay(pdMS_TO_TICKS(2));
    if(err==ESP_OK && address==0xf890) vTaskDelay(pdMS_TO_TICKS(2));
    return err;
}
static int core_status(void *context,uint16_t *status)
{
    uint8_t data[2];
    uint64_t deadline=now_us(NULL)+100000ULL;
    /* GUI may have just re-enabled PLL. Poll a successful unlocked read,
     * never retry a failed I2C transaction or read CORE_STATUS before lock. */
    for(;;) {
        int err=read_io(context,0xf004,data,2,100);
        if(err!=ESP_OK) return err;
        if(now_us(NULL)>=deadline) return ESP_ERR_TIMEOUT;
        if(data[1]&1U) break;
        vTaskDelay(pdMS_TO_TICKS(2));
    }
    int err=read_io(context,0xf405,data,2,100);
    if(err==ESP_OK) *status=(uint16_t)(((uint16_t)data[0]<<8)|data[1]);
    return err;
}
static void revoke_tuning(bench_session_t *s,int reason)
{
    s->tuning_armed=0;
    if(s->sound){bench_sound_cancel(s->sound,reason);}
    bench_web_tuning_revoke(s->external,reason);
}
static bool stream_wanted(const bench_session_t *s)
{
    if(!s->usb_mode)return true;
    if(!s->external)return chime_usb_wanted()||chime_bluetooth_wanted();
    return chime_usb_wanted()&&s->stream_epoch==chime_usb_epoch();
}
static bool sound_cancelled(void *context)
{
    bench_session_t *s=context;
    if(chime_controls_standby()||(!s->usb_mode&&chime_controls_muted())){s->controls_cancelled=true;return true;}
    if(bench_web_sound_stop_pending()||bench_web_tuning_queued())return true;
    if(bench_web_output_change_pending()){
        if(s->usb_mode&&stream_wanted(s))s->output_interrupted_usb=true;
        return true;
    }
    if(s->socket<0)return s->usb_mode&&!stream_wanted(s);
    if(cancelled(s))return true;
    if(s->external)return s->usb_mode&&!stream_wanted(s); /* execute stops BEFORE writes */
    uint8_t byte;
    int n=recv(s->socket,&byte,1,MSG_PEEK|MSG_DONTWAIT);
    /* New SS+ traffic or FIN cancels sound BEFORE another DSP command executes. */
    return n>=0||(errno!=EAGAIN&&errno!=EWOULDBLOCK);
}
static bool ss_idle(bench_session_t *s)
{
    if(s->socket<0)return true;
    uint8_t byte;int n=recv(s->socket,&byte,1,MSG_PEEK|MSG_DONTWAIT);
    return n<0&&(errno==EAGAIN||errno==EWOULDBLOCK);
}
static esp_err_t sound_read(void *context,unsigned target,uint16_t reg,uint8_t *data,size_t n)
{
    bench_session_t *s=context;
    if(target==0)return read_io(context,reg,data,n,20);
    if(target!=1&&target!=2)return ESP_ERR_INVALID_ARG;
    uint8_t sub=(uint8_t)reg;
    return i2c_master_transmit_receive(target==1?s->board->tas5825m_lr:s->board->tas5825m_sub,&sub,1,data,n,20);
}
static esp_err_t sound_write(void *context,unsigned target,uint16_t reg,const uint8_t *data,size_t n)
{
    bench_session_t *s=context;
    if(target==0)return write_io(context,reg,data,n,20);
    if((target!=1&&target!=2)||n!=1||reg>0x7f)return ESP_ERR_INVALID_ARG;
    uint8_t tx[]={(uint8_t)reg,data[0]};
    return i2c_master_transmit(target==1?s->board->tas5825m_lr:s->board->tas5825m_sub,tx,sizeof(tx),20);
}
static esp_err_t sound_power(void *context,bool lr,bool sub)
{
    (void)context;
    esp_err_t err=gpio_set_level(BOARD_GPIO_SUB_PDN,sub?1:0);
    if(err==ESP_OK)err=gpio_set_level(BOARD_GPIO_LR_PDN,lr?1:0);
    if(err!=ESP_OK) {
        gpio_set_level(BOARD_GPIO_LR_PDN,0);gpio_set_level(BOARD_GPIO_SUB_PDN,0);
        board_support_set_dsp_reset(true);
    }
    return err;
}
static void sound_wait(void *context,unsigned ms){(void)context;vTaskDelay(pdMS_TO_TICKS(ms));}
static int current_tone_command;
static void sound_publish(const bench_sound_t *sound)
{
    bench_web_sound_completed(sound->completed?current_tone_command:0);
    bench_web_sound_status(sound->active,sound->attempted,sound->result,sound->failed_reg);
}
/* These are readbacks captured by the existing 20ms guards. Logging does
 * not add I2C reads, alter mute state, or compete with USB's PCM task. */
/* Low-priority owner only, once/second. Never UART from PCM/decode success
 * paths, never read worker-owned codec memory or add I2C transactions. */
static void bluetooth_monitor_log(void)
{
    chime_bluetooth_status_t s;chime_bluetooth_snapshot(&s);
    ESP_LOGI(TAG,"BT FLOW codec=%s epoch=%lu active=%u primed=%u queue=%u drift_ppm=%d packets=%lu gap_events=%lu gap_blocks=%lu gap_max_blocks=%lu prefill_blocks=%lu underruns=%lu error=%d; gaps are 128-frame zero blocks after priming",
        chime_bluetooth_codec_name(s.codec),(unsigned long)s.epoch,(unsigned)s.output_active,(unsigned)s.primed,s.queued_frames,s.drift_ppm,
        (unsigned long)s.packets,(unsigned long)s.pcm_gap_events,(unsigned long)s.pcm_gap_blocks,(unsigned long)s.pcm_gap_max_blocks,
        (unsigned long)s.prefill_blocks,(unsigned long)s.underruns,s.error);
    ESP_LOGI(TAG,"BT BUFFER epoch=%lu output_id=%lu rate=%u queue_min=%u queue_max=%u clamp_blocks=%lu pcm_frames=%llu consumed_frames=%llu rendered_frames=%llu rx_interval_max_us=%u pcm_interval_max_us=%u target_ms=%u target_frames=%u target_raises=%lu target_decays=%lu; per-output cumulative, watermarks at render, RX interval unavailable with internal SBC",
        (unsigned long)s.epoch,(unsigned long)s.output_id,s.rate,s.queue_min,s.queue_max,(unsigned long)s.clamp_blocks,
        (unsigned long long)s.pcm_frames,(unsigned long long)s.consumed_frames,(unsigned long long)s.rendered_frames,
        s.rx_interval_max_us,s.pcm_interval_max_us,s.target_ms,s.target_frames,(unsigned long)s.target_raises,(unsigned long)s.target_decays);
    /* First block of the latest event is latched in the audio path. Several
     * events between monitor polls coalesce: event is an index, not a trace. */
    static uint32_t logged_output_id,logged_gap_event;
    if(s.pcm_gap_events&&(logged_output_id!=s.output_id||logged_gap_event!=s.pcm_gap_events)){
        ESP_LOGI(TAG,"BT GAP epoch=%lu output_id=%lu event=%lu at_ms=%lu queue=%u needed=%u drift_ppm=%d rx_age_us=%u pcm_age_us=%u; latest event first block, UINT32_MAX age means unseen",
            (unsigned long)s.gap_epoch,(unsigned long)s.output_id,(unsigned long)s.pcm_gap_events,(unsigned long)s.gap_at_ms,
            s.gap_queue,s.gap_needed,s.gap_ppm,s.gap_rx_age_us,s.gap_pcm_age_us);
        logged_output_id=s.output_id;logged_gap_event=s.pcm_gap_events;
    }
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    chime_bt_decoder_stats_t d;
    if(chime_bt_decoder_snapshot(&d))
        ESP_LOGI(TAG,"BT DECODE epoch=%lu packets=%lu slow_packets=%lu packet_us=%u max_us=%u decode_us=%u pcm_us=%u open_us=%u audio_us=%u queue_wait_us=%u max_queue_wait_us=%u raw_after_dequeue=%u raw_max_after_dequeue=%u; completed packet WALL times include preemption, queue sampled after dequeue",
            (unsigned long)d.epoch,(unsigned long)d.packets,(unsigned long)d.slow_packets,d.packet_us,d.max_packet_us,d.decode_us,d.pcm_us,d.open_us,d.audio_us,d.queue_wait_us,d.max_queue_wait_us,d.raw_after_dequeue,d.raw_max_after_dequeue);
#endif
#if CONFIG_CHIME_BT_LHDC_V5
    chime_reference_quality_t q;
    if(s.codec==CHIME_BT_CODEC_LHDC_V5&&chime_reference_quality_snapshot(&q))
        ESP_LOGI(TAG,"LHDC QUALITY attempts=%lu rejected=%lu ch0_errors=%lu ch1_silenced=%lu concealed=%lu nonfinite=%lu clipped_frames=%lu clipped_samples=%lu event_attempt=%lu frame_bytes=%lu flags=0x%lx peak_f32_bits=0x%08lx result=%ld ch0=%ld ch1=%ld; boot cumulative, last exceptional event, raw container peak NOT speaker voltage",
            (unsigned long)q.attempts,(unsigned long)q.rejected,(unsigned long)q.channel0_errors,(unsigned long)q.channel1_silenced,
            (unsigned long)q.peak_concealed,(unsigned long)q.nonfinite_frames,(unsigned long)q.clipped_frames,(unsigned long)q.clipped_samples,
            (unsigned long)q.event_attempt,(unsigned long)q.frame_bytes,(unsigned long)q.flags,(unsigned long)q.peak_f32_bits,
            (long)q.result,(long)q.channel0_result,(long)q.channel1_result);
#endif
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    const char *delay_scope="raw-submit-to-first-PCM";
#else
    const char *delay_scope="PCM-only";
#endif
    ESP_LOGI(TAG,"BT DELAY epoch=%lu output_id=%lu caps=%u/%u estimate_us=%u queue_us=%u residence_us=%u fixed_us=%u samples=%lu request_100us=%u set_100us=%u pending=%u timeout=%u requests=%lu accepts=%lu failures=%lu error=%d scope=%s; estimated/uncalibrated, nominal DMA included; SDK acceptance NOT peer ACK/video sync",
        (unsigned long)s.epoch,(unsigned long)s.output_id,(unsigned)s.delay_caps_known,(unsigned)s.delay_supported,
        s.delay_estimate_us,s.delay_queue_us,s.delay_residence_us,s.delay_fixed_us,(unsigned long)s.delay_samples,
        (unsigned)s.delay_requested_100us,(unsigned)s.delay_accepted_100us,(unsigned)s.delay_pending,(unsigned)s.delay_timed_out,
        (unsigned long)s.delay_requests,(unsigned long)s.delay_accepts,(unsigned long)s.delay_failures,s.delay_error,delay_scope);
    ESP_LOGI(TAG,"BT RECOVERY output_id=%lu state=%u events=%lu resumes=%lu cancels=%lu wait_ms=%u last_wait_ms=%u blocks=%lu discarded_frames=%llu; 0=idle 1=prefill 2=playing 3=gap 4=rebuffering 5=fault",
        (unsigned long)s.output_id,(unsigned)s.stream_state,(unsigned long)s.rebuffer_events,
        (unsigned long)s.rebuffer_resumes,(unsigned long)s.rebuffer_cancels,s.rebuffer_wait_ms,s.rebuffer_last_wait_ms,
        (unsigned long)s.rebuffer_blocks,(unsigned long long)s.discarded_frames);
}
static void sound_monitor_log(const bench_sound_t *sound)
{
    for(unsigned i=0;i<2;++i){
        if(i==0?!(sound->outputs&3U):!(sound->outputs&4U))continue;
        uint8_t mute=sound->amp_automute[i];
        ESP_LOGI(TAG,"AMP FLOW output_mask=%u amp=%s i2c=%02x clk=%06lx state68=%02x automute69=%02x zero_left=%u zero_right=%u; status only, NOT speaker voltage",
            sound->outputs,i==0?"LR":"SUB",i==0?0x4c:0x4d,
            (unsigned long)sound->amp_clocks[i],(unsigned)sound->amp_state[i],(unsigned)mute,
            (unsigned)(mute&1U),(unsigned)((mute>>1)&1U));
    }
}
static void mix_monitor_log(void)
{
    chime_render_stats_t rs;chime_render_snapshot(&rs);
    ESP_LOGI(TAG,"MIX FLOW blocks=%lu usb_short=%lu bt_short=%lu clips=%lu peak_q27=%lu compute_us=%lu/%lu src_us=%lu/%lu write_us=%lu/%lu period_us=%lu/%lu deadline_misses=%lu unpaced_yields=%lu; times=last/max, wall times include preemption; unity sum, NO power limiter",
        (unsigned long)rs.blocks,(unsigned long)rs.usb_short,(unsigned long)rs.bt_short,
        (unsigned long)rs.clips,(unsigned long)rs.peak,
        (unsigned long)rs.last_compute_us,(unsigned long)rs.max_compute_us,
        (unsigned long)rs.last_src_us,(unsigned long)rs.max_src_us,
        (unsigned long)rs.last_write_us,(unsigned long)rs.max_write_us,
        (unsigned long)rs.last_period_us,(unsigned long)rs.max_period_us,
        (unsigned long)rs.deadline_misses,(unsigned long)rs.unpaced_yields);
}

static int execute(void *context,const sigma_request_t *request,uint8_t *read_data)
{
    bench_session_t *s=context;
    /* Reads can coexist with audition in this single owner. Every write
     * revokes authorization BEFORE touching DSP (even a rejected request).
     * Never guess external software-safeload buffer/trigger addresses. */
    if(request->control==SIGMA_CONTROL_WRITE){
        revoke_tuning(s,ESP_OK);
        if(s->sound)sound_publish(s->sound);
        bench_web_dsp_invalidate();
        if(s->external&&request->flag){bench_web_record_request(ESP_ERR_NOT_SUPPORTED);return ESP_ERR_NOT_SUPPORTED;}
    }
    s->request_deadline=now_us(NULL)+DSP_TRANSFER_DEADLINE_US;
    int result=bench_dsp_execute(&s->dsp,request,read_data);
    bench_web_record_request(result);
    ++s->commands;
    if(result!=0 || s->commands<=12 || request->address>=0xf000 || (s->commands%100)==0) {
        ESP_LOGI(TAG,"request=%lu op=%02x wire_chip=%02x flag=%u addr=%04x bytes=%lu result=%d i2c=%d",
            (unsigned long)s->commands,request->control,request->chip_address,request->flag,request->address,
            (unsigned long)request->data_length,result,s->dsp.transfer.diagnostic.io_error);
    }
    /* No extra diagnostic reads during the GUI initialization sequence.
     * In particular, CORE_STATUS reads while GUI changes clocks can fail.
     * The GUI/user can explicitly request F405 once startup is complete. */
    s->request_deadline=0;
    return result;
}
static int send_io(void *context,const uint8_t *data,size_t bytes,uint32_t timeout_ms)
{
    bench_session_t *s=context;
    uint64_t deadline=now_us(NULL)+(uint64_t)timeout_ms*1000ULL;
    while(!cancelled(s)) {
        uint64_t now=now_us(NULL);
        if(now>=deadline) return -1;
        uint64_t remaining=deadline-now;
        if(remaining>200000ULL) remaining=200000ULL;
        struct timeval timeout={.tv_sec=0,.tv_usec=(long)remaining};
        fd_set writable;FD_ZERO(&writable);FD_SET(s->socket,&writable);
        int ready=select(s->socket+1,NULL,&writable,NULL,&timeout);
        if(ready<0) return -1;
        if(ready==0) continue;
        if(cancelled(s)) return -1;
        int sent=(int)send(s->socket,data,bytes,MSG_DONTWAIT);
        if(sent<0 && (errno==EWOULDBLOCK || errno==EAGAIN)) continue;
        return sent;
    }
    return -1;
}
static esp_err_t prepare_dsp(bench_session_t *s)
{
    board_support_safe_shutdown(s->board);
    esp_err_t err=gpio_set_level(BOARD_GPIO_DSP_SELFBOOT,0);
    if(err!=ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));
    board_support_set_dsp_reset(false);
    vTaskDelay(pdMS_TO_TICKS(20));
    static const uint16_t init[][2]={{0xf003,0},{0xf000,0x60},{0xf001,2},{0xf002,1},{0xf003,1}};
    for(size_t i=0;i<sizeof(init)/sizeof(init[0]);++i) {
        uint8_t value[]={(uint8_t)(init[i][1]>>8),(uint8_t)init[i][1]};
        err=write_io(s,init[i][0],value,2,100);
        if(err!=ESP_OK) return err;
    }
    bool locked=false;
    uint64_t start=now_us(NULL);
    while(now_us(NULL)-start<100000ULL) {
        uint8_t value[2];
        err=read_io(s,0xf004,value,2,100);
        if(err!=ESP_OK) return err;
        if((value[1]&1U) && now_us(NULL)-start<100000ULL) {locked=true;break;}
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    if(!locked) return ESP_ERR_TIMEOUT;
    uint8_t kill[]={0,1};
    err=write_io(s,0xf403,kill,2,100);
    uint16_t state=0xffff;
    if(err==ESP_OK) err=core_status(s,&state);
    if(err==ESP_OK && (state&7U)!=0) err=ESP_ERR_INVALID_STATE;
    if(err==ESP_OK) ESP_LOGI(TAG,"DSP READY PLL locked, core stopped; SELFBOOT=0; amps OFF; fresh session");
    return err;
}
static esp_err_t load_default(bench_session_t *s)
{
    chime_usb_output_end();board_support_safe_shutdown(s->board);
    s->socket=-1;s->usb_mode=false;bench_web_set_default_ready(false);
    vTaskDelay(pdMS_TO_TICKS(10));board_support_set_dsp_reset(false);
    vTaskDelay(pdMS_TO_TICKS(20));uint16_t failed=0;
    esp_err_t err=chime_dsp_boot((chime_dsp_boot_io_t){.context=s,.read=read_io,.write=write_io,
        .now_us=now_us,.wait_ms=sound_wait},&failed);
    if(err)board_support_safe_shutdown(s->board);
    bench_web_set_default_ready(err==ESP_OK);
    ESP_LOGW(TAG,"DEFAULT DSP image=" CHIME_DSP_PROFILE " boot=%s result=%d reg=%04x; RAM only, no EEPROM; amps OFF",
        err==ESP_OK?"READY":"FAILED",err,failed);
    return err;
}
/* Existing owner diagnostics only: no extra I2C/GPIO reads or audio changes. */
static void indicator_publish(bool *fault,bool default_ready,const bench_sound_t *sound,
    const bench_session_t *session,const chime_source_t *source)
{
    if(!default_ready&&!session->external)*fault=true;
    /* Output failures latch until successful playback. An explicit DSP restore
     * clears boot failure, but retained output errors still require a good start.
     * STOP/SS cancellation without a failed guard is not a hardware fault. */
    if(sound->active&&sound->result==ESP_OK)*fault=false;
    else if(sound->attempted&&sound->result!=ESP_OK&&
        (sound->failed_reg!=0||!source->stopped))*fault=true;
    chime_indicator_publish((chime_indicator_state_t){.fault=*fault,
        .bluetooth=!session->external&&chime_bluetooth_connected(),
        .stopped=source->stopped&&!sound->active});
}
static void source_callbacks(bench_sound_t *sound,chime_source_id_t source)
{
    sound->io.tone_begin=source==CHIME_SOURCE_MIX?chime_stream_mix_begin:source==CHIME_SOURCE_USB?chime_usb_output_begin:bench_audio_tone_begin;
    sound->io.tone_pump=source==CHIME_SOURCE_MIX?chime_stream_mix_poll:source==CHIME_SOURCE_USB?chime_usb_output_poll:bench_audio_tone_pump;
    sound->io.tone_end=source==CHIME_SOURCE_NONE?bench_audio_tone_end:chime_stream_end;
}
esp_err_t bench_bridge_run(board_context_t *board)
{
    board_support_safe_shutdown(board);
    bench_sound_t sound;
    bench_session_t session={.board=board,.socket=-1,.sound=&sound};
    bench_sound_init(&sound,(bench_sound_io_t){.context=&session,.read=sound_read,.write=sound_write,
        .power=sound_power,.wait_ms=sound_wait,.now_us=now_us,.cancelled=sound_cancelled,
        .tone_begin=bench_audio_tone_begin,.tone_pump=bench_audio_tone_pump,.tone_end=bench_audio_tone_end});
    ESP_LOGW(TAG,"CHIME default firmware; SigmaStudio+ 3.4.0; AMPLIFIERS OFF; built-in DSP + USB audio; manual output modes + finite sine; default L+R+Sub; enabled L/R/SUB gains restored from compiled project");
    ESP_LOGI(TAG,"SINE guard=pll-positive-v1; requires CLKDET39=08 and actual POWER_STATE68=03 before unmute");
    uint8_t *storage=malloc(SIGMA_MAX_FRAME_BYTES);
    if(storage==NULL) return ESP_ERR_NO_MEM;
    esp_err_t result=bench_audio_start();
    if(result!=ESP_OK) {free(storage);return result;}
    bool default_ready=load_default(&session)==ESP_OK;
    bool led_fault=!default_ready;
    chime_indicator_publish((chime_indicator_state_t){.fault=led_fault,.starting=true});
    result=chime_usb_start();
    if(result!=ESP_OK){board_support_safe_shutdown(board);bench_audio_stop();free(storage);return result;}
    result=chime_network_start();
    if(result!=ESP_OK) {chime_usb_stop();chime_network_stop();board_support_safe_shutdown(board);bench_audio_stop();free(storage);return result;}
    /* A failed Bluetooth PoC must leave the proven USB path available. */
    (void)chime_bluetooth_start();
    int listener=-1;
    result=chime_controls_start();if(result!=ESP_OK)goto done;
    listener=socket(AF_INET,SOCK_STREAM,IPPROTO_IP);
    if(listener<0) {result=ESP_FAIL;goto done;}
    int reuse=1;
    setsockopt(listener,SOL_SOCKET,SO_REUSEADDR,&reuse,sizeof(reuse));
    const struct sockaddr_in address={.sin_family=AF_INET,.sin_port=htons(BENCH_PORT),.sin_addr={.s_addr=htonl(INADDR_ANY)}};
    if(bind(listener,(const struct sockaddr *)&address,sizeof(address))!=0 || listen(listener,1)!=0) {
        result=ESP_FAIL;goto done;
    }
    result=bench_web_start(BENCH_PORT);
    if(result!=ESP_OK) goto done;
    ESP_LOGI(TAG,"USB boot board-bound image (compiled L/R/SUB gains); default_ready=%u; SS+ disconnect preserves external RAM; audition needs manual approval",(unsigned)default_ready);
    ESP_LOGI(TAG,"LISTEN TCP :8086; enabled only after STA DHCP chip=70 -> I2C 38 channel=0; LAB chip=00/70 aliases for all supported R/W; exact 1B no-op");
    bench_web_set_default_ready(default_ready);
    uint32_t usb_attempt_epoch=UINT32_MAX; /* external manual USB audition only */
    chime_source_t source;chime_source_init(&source);
    unsigned output_mode=BENCH_OUTPUT_DEFAULT;
    uint64_t audio_monitor_at=0;
    int client=-1;
    bool controls_blocked=false;
    sigma_bridge_t bridge={0};
    while(true) {
        chime_bluetooth_delay_poll(); /* low-priority control plane, never PCM worker */
        bool blocked=chime_controls_standby();
        if(blocked!=controls_blocked){
            controls_blocked=blocked;
            if(blocked){
                revoke_tuning(&session,ESP_OK);sound_publish(&sound);
                usb_attempt_epoch=chime_usb_epoch();
            }else chime_source_retry(&source); /* never clears Web STOP or SS ownership */
        }
        chime_source_update(&source,chime_bluetooth_connected(),chime_usb_epoch(),chime_bluetooth_epoch());
        /* Local mix lives while either input is wanted. External SS audition
         * deliberately remains USB-only and epoch-bound. */
        if(sound.active&&sound.continuous&&!stream_wanted(&session)){
            ESP_LOGI(TAG,"SOURCE SWITCH from=%s selected=%s; stopping old PCM before admission",
                session.mixed_mode?"mix":"usb",chime_source_name(source.selected));
            bench_sound_stop(&sound,ESP_OK);sound_publish(&sound);
            if(session.mixed_mode)bluetooth_monitor_log(); /* Never delay the stop for diagnostics. */
            mix_monitor_log();
            ESP_LOGI(TAG,"SOURCE QUIESCED selected=%s; old PCM stopped",chime_source_name(source.selected));
        }
        if(client>=0 && (cancelled(&session) || !sigma_bridge_poll(&bridge))) goto close_client;
        int tuning=bench_web_take_tuning_request();
        if(tuning>0){
            chime_source_stop(&source);
            session.tuning_armed=0;bench_sound_cancel(&sound,ESP_OK);sound_publish(&sound);
            session.usb_mode=false;usb_attempt_epoch=chime_usb_epoch();
            esp_err_t err=ESP_ERR_INVALID_STATE;
            if(tuning==4){
                if(client>=0){shutdown(client,SHUT_RDWR);close(client);client=-1;}
                bench_web_set_client(false);bridge=(sigma_bridge_t){0};
                session.socket=-1;session.request_deadline=0;session.external=false;
                default_ready=load_default(&session)==ESP_OK;err=default_ready?ESP_OK:ESP_FAIL;
                led_fault=!default_ready; /* explicit recovery, still no auto-play */
            }else if(session.external&&bridge.decoder.used==0&&ss_idle(&session)&&!bench_web_sound_stop_pending()){
                unsigned banks=tuning==1?3U:tuning==2?4U:7U;
                /* Read-only profile/clock check, amps stay OFF. */
                err=bench_sound_check_external(&sound,banks);
                if(err==ESP_OK&&!ss_idle(&session))err=ESP_ERR_INVALID_STATE;
                if(err==ESP_OK)session.tuning_armed=banks;
            }
            bench_web_tuning_publish(session.external,session.tuning_armed,err);sound_publish(&sound);
            if(tuning==4)usb_attempt_epoch=chime_usb_epoch(); /* no play triggered by restore */
            if(err==ESP_OK&&session.tuning_armed)usb_attempt_epoch=UINT32_MAX;
            ESP_LOGW(TAG,"TUNING command=%d external=%u armed=%u result=%d reg=%04x raw=%08lx; no external DSP parameter writes",tuning,(unsigned)session.external,session.tuning_armed,err,sound.failed_reg,(unsigned long)sound.observed);
        }
        int output=bench_web_take_output_request();
        if(output>0){
            bool resume=((sound.active&&sound.continuous)||session.output_interrupted_usb)&&stream_wanted(&session);
            session.output_interrupted_usb=false;
            bench_sound_cancel(&sound,ESP_OK);sound_publish(&sound);
            session.usb_mode=false;
            session.controls_cancelled=false;
            esp_err_t err=(client<0&&default_ready&&bridge.decoder.used==0)?
                bench_sound_route(&sound,(unsigned)output):ESP_ERR_INVALID_STATE;
            if(err==ESP_OK)output_mode=(unsigned)output;
            else bench_sound_stop(&sound,session.controls_cancelled?ESP_OK:err);
            bench_web_output_publish(output_mode,err);sound_publish(&sound);
            /* No new playback on idle/STOP/finite sine; only resume an
             * already-playing USB stream, with a fresh FIFO + fade-in. */
            usb_attempt_epoch=chime_usb_epoch();
            if(resume&&err==ESP_OK&&!bench_web_sound_stop_pending()){
                if(session.external)usb_attempt_epoch=UINT32_MAX;
                else chime_source_resume(&source);
            }
            ESP_LOGW(TAG,"OUTPUT requested=%d applied=%u result=%d; amps OFF during change; no limiter",output,output_mode,err);
        }
        int command=bench_web_take_sound_request();
        if(command==-2){
            if(client<0&&default_ready&&!session.external)chime_source_resume(&source);
        }
        else if(command<0) {chime_source_stop(&source);revoke_tuning(&session,ESP_OK);bench_sound_stop(&sound,ESP_OK);usb_attempt_epoch=chime_usb_epoch();sound_publish(&sound);}
        else if(command>0) {
            chime_source_stop(&source); /* finite diagnostics never auto-resume */
            current_tone_command=command;bench_web_sound_completed(0);
            bench_tone_config_t config=bench_tone_command_config(command);
            if(command<=3&&(output_mode&4U))config=(bench_tone_config_t){100U,-30,3U};
            if(chime_controls_blocked()||client>=0||!default_ready||bridge.decoder.used!=0) {sound.attempted=true;sound.failed_reg=0;sound.observed=0;
                sound.phase="admission";sound.hiz_clocks=0;sound.play_clocks=0;sound.power_state=0xff;
                bench_sound_stop(&sound,ESP_ERR_INVALID_STATE);}
            else {/* Retire the old source before replacing its stop callback. A web
                 * request can race USB admission after HTTP's idle check. */
                bench_sound_cancel(&sound,ESP_OK);
                session.usb_mode=false;source_callbacks(&sound,CHIME_SOURCE_NONE);
                usb_attempt_epoch=chime_usb_epoch();
                session.controls_cancelled=false;
                if(bench_audio_tone_configure(config)==ESP_OK)bench_sound_start(&sound,output_mode);
                else {sound.attempted=true;bench_sound_stop(&sound,ESP_ERR_INVALID_ARG);}
                if(session.controls_cancelled)bench_sound_stop(&sound,ESP_OK);}
            ESP_LOGW(TAG,"SINE request=%d %uHz %ddBFS %us; ROM1 digital=0dB analog=-8dB; selected outputs; result=%d reg=%04x raw=%08lx phase=%s hiz=%06lx play=%06lx state=%02x",
                command,config.frequency_hz,config.level_dbfs,config.seconds,sound.result,sound.failed_reg,(unsigned long)sound.observed,
                sound.phase?sound.phase:"idle",(unsigned long)sound.hiz_clocks,(unsigned long)sound.play_clocks,(unsigned)sound.power_state);
            sound_publish(&sound);
        }
        bool wanted=chime_bluetooth_wanted()||chime_usb_wanted();
        bool local_start=!session.external&&chime_source_admit(&source,
            !chime_controls_standby()&&client<0&&default_ready&&!sound.active&&bridge.decoder.used==0&&!bench_web_sound_stop_pending(),wanted);
        bool external_start=!chime_controls_standby()&&session.external&&session.tuning_armed&&!sound.active&&chime_usb_wanted()&&usb_attempt_epoch!=chime_usb_epoch();
        if(local_start||external_start){
            chime_source_id_t selected=external_start?CHIME_SOURCE_USB:source.selected;
            usb_attempt_epoch=chime_usb_epoch();
            session.usb_mode=true;session.mixed_mode=selected==CHIME_SOURCE_MIX;
            session.stream_epoch=session.mixed_mode?chime_bluetooth_epoch():chime_usb_epoch();
            source_callbacks(&sound,selected);
            /* Old worker is quiesced by cancel/end; amps are OFF. Never let
             * stale DMA from the old source survive a new admission. */
            session.controls_cancelled=false;
            esp_err_t clear=bench_audio_pcm_clear();
            if(clear!=ESP_OK)bench_sound_stop(&sound,clear);
            else if(session.external)bench_sound_start_external(&sound,session.tuning_armed);
            else bench_sound_start_stream(&sound,output_mode);
            if(session.controls_cancelled)bench_sound_stop(&sound,ESP_OK);
            if(sound.result!=ESP_OK&&session.external)revoke_tuning(&session,sound.result);
            ESP_LOGW(TAG,"SOURCE PLAY selected=%s epoch=%lu result=%d phase=%s; NO power limiter",
                chime_source_name(selected),(unsigned long)session.stream_epoch,sound.result,sound.phase?sound.phase:"idle");
            sound_publish(&sound);
        }
        if(sound.active&&sound.continuous&&!stream_wanted(&session)){
            ESP_LOGI(TAG,"SOURCE SWITCH from=%s selected=%s; stopping old PCM before admission",
                session.mixed_mode?"mix":"usb",chime_source_name(source.selected));
            bench_sound_stop(&sound,ESP_OK);sound_publish(&sound);
            if(session.mixed_mode)bluetooth_monitor_log(); /* Never delay the stop for diagnostics. */
            mix_monitor_log();
            ESP_LOGI(TAG,"SOURCE QUIESCED selected=%s; old PCM stopped",chime_source_name(source.selected));
        }
        bool was_active=sound.active;
        session.controls_cancelled=false;
        bench_sound_poll(&sound);
        if(session.controls_cancelled)bench_sound_stop(&sound,ESP_OK);
        if(sound.active){
            uint64_t monitor_now=now_us(NULL);
            if(monitor_now>=audio_monitor_at){sound_monitor_log(&sound);if(session.mixed_mode)bluetooth_monitor_log();
                mix_monitor_log();
                audio_monitor_at=monitor_now+1000000ULL;}
        }else audio_monitor_at=0;
        if(was_active&&!sound.active) {
            if(session.mixed_mode)bluetooth_monitor_log();
            mix_monitor_log();
            if(session.external&&sound.result!=ESP_OK)revoke_tuning(&session,sound.result);
            ESP_LOGW(TAG,"AUDIO STOP result=%d reg=%04x raw=%08lx; amps OFF; listening result NOT_VERIFIED",sound.result,sound.failed_reg,(unsigned long)sound.observed);
            sound_publish(&sound);
        }
        if(bench_web_take_dsp_request()) {
            bench_dsp_status_t status={.result=ESP_ERR_INVALID_STATE};
            /* Never interleave a snapshot with a partial SS+ frame or sound.
             * New SS+ bytes/FIN/network loss/STOP cancel before the next read. */
            if((client>=0||default_ready||session.external)&&!sound.active&&bridge.decoder.used==0) {
                session.usb_mode=false;board_support_set_amp_power(false,false);
                bench_dsp_status_collect(&status,(bench_dsp_status_io_t){.context=&session,
                    .read=read_io,.now_us=now_us,.cancelled=sound_cancelled});
            }
            bench_web_dsp_publish(&status);
            ESP_LOGI(TAG,"DSP STATUS snapshot result=%d complete=%u; read-only; see web registers",
                status.result,(unsigned)status.complete);
        }
        if(!sound.active)board_support_set_amp_power(false,false);
        chime_bluetooth_status_t bt;chime_bluetooth_snapshot(&bt);
        bench_web_source_publish((unsigned)source.selected,
            sound.active&&sound.continuous?(session.external?1U:3U):0U,source.stopped,&bt);
        indicator_publish(&led_fault,default_ready,&sound,&session,&source);
        fd_set readfds;FD_ZERO(&readfds);FD_SET(listener,&readfds);
        int maxfd=listener;
        if(client>=0) {FD_SET(client,&readfds);if(client>maxfd)maxfd=client;}
        struct timeval timeout={.tv_sec=0,.tv_usec=sound.active&&!sound.continuous?0:20000};
        int ready=select(maxfd+1,&readfds,NULL,NULL,&timeout);
        if(ready<0) {result=ESP_FAIL;break;}
        if(ready==0) continue;
        if(FD_ISSET(listener,&readfds)) {
            int incoming=accept(listener,NULL,NULL);
            if(incoming<0) {result=ESP_FAIL;break;}
            if(client>=0 || !chime_network_ready()) {close(incoming);ESP_LOGW(TAG,"rejected extra/unassociated client");}
            else {
                chime_source_stop(&source);
                bench_sound_cancel(&sound,ESP_ERR_INVALID_STATE);chime_usb_output_end();sound_publish(&sound);
                default_ready=false;bench_web_set_default_ready(false);usb_attempt_epoch=chime_usb_epoch();
                session=(bench_session_t){.board=board,.socket=incoming,.network_epoch=chime_network_epoch(),.external=true,.sound=&sound};
                bench_web_tuning_revoke(true,ESP_OK);
                client=incoming;
                bench_web_set_client(true);
                result=prepare_dsp(&session);
                const dsp_transfer_io_t io={.context=&session,.read=read_io,.write=write_io,.now_us=now_us,.wait_us=wait_us,.cancelled=cancelled};
                const sigma_bridge_io_t network={.context=&session,.now_us=now_us,.execute=execute,.send=send_io};
                const sigma_protocol_profile_t profile={SIGMA_PROFILE_PLUS_3_4_0_LAB,0x70,0};
                if(result!=ESP_OK || !bench_dsp_init_external(&session.dsp,io,core_status) ||
                   !sigma_bridge_init(&bridge,profile,network,storage,SIGMA_MAX_FRAME_BYTES)) {
                    ESP_LOGE(TAG,"session preparation failed: %s",esp_err_to_name(result));
                    goto close_client;
                }
                ESP_LOGI(TAG,"CLIENT admitted; downloads/writes stop audition; manual USB audition only; amps OFF");
                /* select's fdset predates this descriptor. */
                continue;
            }
        }
        if(client>=0 && FD_ISSET(client,&readfds)) {
            uint8_t rx[1500];
            int count=recv(client,rx,sizeof(rx),0);
            if(count<=0) {if(count==0) sigma_bridge_finish(&bridge);goto close_client;}
            uint32_t prior=bridge.diagnostic.lab_init_frames;
            if(!sigma_bridge_receive(&bridge,rx,(size_t)count)) {
                ESP_LOGE(TAG,"REJECT error=%d op=%02x wire_chip=%02x addr=%04x length=%lu execution=%d; raw RX prefix:",
                    bridge.diagnostic.error,bridge.diagnostic.control,bridge.diagnostic.chip_address,bridge.diagnostic.address,
                    (unsigned long)bridge.diagnostic.data_length,bridge.diagnostic.operation_error);
                ESP_LOG_BUFFER_HEX_LEVEL(TAG,rx,(size_t)count>32?32:(size_t)count,ESP_LOG_ERROR);
                goto close_client;
            }
            if(prior!=bridge.diagnostic.lab_init_frames) ESP_LOGW(TAG,"captured exact 1B prelude consumed (LAB ONLY); no DSP action, no ACK");
        }
        continue;
close_client:
        bench_sound_cancel(&sound,ESP_ERR_INVALID_STATE);sound_publish(&sound);
        (void)bench_web_take_sound_request();session.socket=-1;
        /* Keep downloaded RAM even on protocol/network failure, but NEVER
         * keep audition authorized. Explicit restore/reboot returns built-in. */
        session.request_deadline=0;session.tuning_armed=0;session.usb_mode=false;
        board_support_set_amp_power(false,false);
        bench_web_tuning_revoke(true,ESP_OK);
        bench_web_set_client(false);
        if(client>=0) {shutdown(client,SHUT_RDWR);close(client);client=-1;}
        ESP_LOGW(TAG,"SESSION CLOSED frames=%lu init=%lu error=%d; external RAM retained, amps OFF; manual audition/restore required",
            (unsigned long)bridge.diagnostic.completed_frames,(unsigned long)bridge.diagnostic.lab_init_frames,bridge.diagnostic.error);
        bridge=(sigma_bridge_t){0};
        default_ready=false;bench_web_set_default_ready(false);
        usb_attempt_epoch=chime_usb_epoch();
    }
    bench_sound_cancel(&sound,ESP_ERR_INVALID_STATE);sound_publish(&sound);
    if(client>=0) {shutdown(client,SHUT_RDWR);close(client);}
    board_support_safe_shutdown(board);
done:
    bench_sound_cancel(&sound,ESP_ERR_INVALID_STATE);sound_publish(&sound);
    board_support_safe_shutdown(board);
    bench_web_stop();
    if(listener>=0) close(listener);
    chime_controls_stop();
    chime_bluetooth_stop();
    chime_usb_stop();
    chime_network_stop();
    esp_err_t audio_stop=bench_audio_stop();
    if(audio_stop!=ESP_OK) {ESP_LOGE(TAG,"I2S stop failed: %s",esp_err_to_name(audio_stop));if(result==ESP_OK)result=audio_stop;}
    free(storage);board_support_safe_shutdown(board);
    ESP_LOGW(TAG,"network/bridge service stopped; SAFE; reset to restart");
    return result;
}

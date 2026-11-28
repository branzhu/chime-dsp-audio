#include "sdkconfig.h"
#include "chime_bt_vendor.h"
#include "chime_bt_decoder.h"
#include "chime_bluetooth.h"
#include "chime_bt_pcm.h"
#include "chime_render.h"
#include "bt_test_types.h"
#include "freertos/FreeRTOS.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
static unsigned render_snapshots;
void chime_render_snapshot(chime_render_stats_t *s){
    memset(s,0,sizeof(*s));++render_snapshots;
    s->src_blocks=54;s->src_total_us=162000;s->src_active=true;s->src_inflight_us=1700;
}
bool chime_bt_decoder_snapshot(chime_bt_decoder_stats_t *s){memset(s,0,sizeof(*s));return false;}
static unsigned calls,fail_at,depth,notification;
static int controller,host;static bool no_profile_ack,disable_error;
static int64_t now;
#if CHIME_BT_CAPTURE_LOG
#include <stdarg.h>
static char fault_pcm[512],fault_start[512],fault_mix[512];
static unsigned fault_headers,fault_bins;
void chime_test_log(const char *tag,const char *format,...){
    (void)tag;assert(!depth);char text[768];va_list args;va_start(args,format);
    int n=vsnprintf(text,sizeof(text),format,args);va_end(args);assert(n>=0&&(size_t)n<sizeof(text));
    if(!strncmp(text,"stream stopped ",15)){
        assert(render_snapshots>fault_headers);++fault_headers;
    }
    if(!strncmp(text,"fault PCM ",10)){assert(n<(int)sizeof(fault_pcm));memcpy(fault_pcm,text,(size_t)n+1);}
    if(!strncmp(text,"fault START ",12)){assert(n<(int)sizeof(fault_start));memcpy(fault_start,text,(size_t)n+1);}
    if(!strncmp(text,"fault MIX ",10)){assert(n<(int)sizeof(fault_mix));memcpy(fault_mix,text,(size_t)n+1);}
    if(!strncmp(text,"fault FLOW ",11))++fault_bins;
}
#endif
static void (*a2dp)(esp_a2d_cb_event_t,esp_a2d_cb_param_t *);
static void (*gap)(esp_bt_gap_cb_event_t,esp_bt_gap_cb_param_t *);
static void (*avrc)(esp_avrc_tg_cb_event_t,esp_avrc_tg_cb_param_t *);
static void (*pcm)(const uint8_t *,uint32_t);
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
static chime_bt_decoder_callbacks_t dec_cb;
static void (*raw)(esp_a2d_conn_hdl_t,esp_a2d_audio_buff_t *);
static bool dec_live,no_sep_ack,sep_failure,dec_stop_failure;
static unsigned freed,submitted;
#endif
static esp_err_t step(void){assert(!depth);return ++calls==fail_at?-99:0;}
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
esp_err_t chime_bt_decoder_start(const chime_bt_decoder_callbacks_t *c){dec_cb=*c;int e=step();if(!e)dec_live=true;return e;}
bool chime_bt_decoder_stop(void){if(dec_stop_failure)return false;dec_live=false;return true;}
void esp_a2d_audio_buff_free(esp_a2d_audio_buff_t *buffer){assert(buffer);++freed;}
void chime_bt_decoder_submit(esp_a2d_audio_buff_t *b,uint32_t epoch,chime_bt_codec_t codec,unsigned rate,unsigned channels){
    assert(dec_live&&rate&&(codec==CHIME_BT_CODEC_SBC||codec==CHIME_BT_CODEC_AAC||codec==CHIME_BT_CODEC_LDAC));++submitted;
    if(dec_cb.valid(epoch)){
        if(b->data_len%(channels*2))dec_cb.fail(epoch,ESP_ERR_INVALID_SIZE);
        else {if(dec_cb.timing)dec_cb.timing(epoch,(uint64_t)now-3000U);dec_cb.pcm(b->data,b->data_len,epoch);}
    }
    esp_a2d_audio_buff_free(b);
}
static void external_pcm(const uint8_t *data,uint32_t size){esp_a2d_audio_buff_t b={.data_len=(uint16_t)size,.data=(uint8_t *)data};raw(0,&b);}
esp_err_t esp_a2d_sink_register_audio_data_callback(void (*c)(esp_a2d_conn_hdl_t,esp_a2d_audio_buff_t *)){raw=c;pcm=external_pcm;return step();}
esp_err_t esp_a2d_sink_register_stream_endpoint(uint8_t seid,const esp_a2d_mcc_t *mcc){
    assert(seid<(CONFIG_CHIME_BT_LHDC_V5?7:6));
    if(!seid){assert(mcc->type==ESP_A2D_MCT_M24&&mcc->cie.m24_info.obj_type==0x60&&mcc->cie.m24_info.ch==0xc);}
    else if(seid==1)assert(mcc->type==ESP_A2D_MCT_SBC&&mcc->cie.sbc_info.max_bitpool==53);
#if CONFIG_CHIME_BT_VENDOR_CODECS
    else {uint8_t wire[CHIME_VENDOR_WIRE_MAX]={0};unsigned n=mcc->cie.vendor[0];assert(n==7||n==8||n==11);wire[0]=(uint8_t)(n+2);wire[2]=255;memcpy(wire+3,mcc->cie.vendor+1,n);assert(chime_vendor_codec(wire)==seid+1);}
#endif
    int e=step();
    if(!e&&!no_sep_ack){esp_a2d_cb_param_t p={0};p.a2d_sep_reg_stat.seid=seid;p.a2d_sep_reg_stat.reg_state=sep_failure?1:ESP_A2D_SEP_REG_SUCCESS;a2dp(ESP_A2D_SEP_REG_STATE_EVT,&p);}
    return e;
}
#endif
void usb_test_enter_critical(portMUX_TYPE *m){assert(!depth&&!*m);++depth;*m=1;}
void usb_test_exit_critical(portMUX_TYPE *m){assert(depth==1&&*m==1);--depth;*m=0;}
int64_t esp_timer_get_time(void){return now;}
void vTaskDelay(uint32_t ms){now+=(int64_t)ms*1000;}
const char *esp_err_to_name(esp_err_t e){(void)e;return "test";}
esp_err_t nvs_flash_init(void){return step();}
int esp_bt_controller_get_status(void){return controller;}
esp_err_t esp_bt_controller_init(esp_bt_controller_config_t *c){assert(c);int e=step();if(!e)controller=1;return e;}
esp_err_t esp_bt_controller_enable(int m){assert(m==ESP_BT_MODE_CLASSIC_BT&&controller==1);int e=step();if(!e)controller=2;return e;}
esp_err_t esp_bt_controller_disable(void){assert(controller==2);controller=1;return 0;}
esp_err_t esp_bt_controller_deinit(void){assert(controller==1);controller=0;return 0;}
int esp_bluedroid_get_status(void){return host;}
esp_err_t esp_bluedroid_init(void){int e=step();if(!e)host=1;return e;}
esp_err_t esp_bluedroid_enable(void){assert(host==1);int e=step();if(!e)host=2;return e;}
esp_err_t esp_bluedroid_disable(void){assert(host==2);if(disable_error)return -88;host=1;return 0;}
esp_err_t esp_bluedroid_deinit(void){assert(host==1);host=0;return 0;}
esp_err_t esp_bt_gap_register_callback(void (*cb)(esp_bt_gap_cb_event_t,esp_bt_gap_cb_param_t *)){gap=cb;return step();}
esp_err_t esp_bt_gap_set_device_name(const char *name){assert(!strcmp(name,"Chime Bluetooth"));return step();}
esp_err_t esp_bt_gap_set_security_param(unsigned type,void *p,unsigned n){assert(type==ESP_BT_SP_IOCAP_MODE&&*(unsigned *)p==ESP_BT_IO_CAP_NONE&&n==sizeof(unsigned));return step();}
esp_err_t esp_bt_gap_set_scan_mode(int c,int d){assert(c==ESP_BT_CONNECTABLE||c==ESP_BT_NON_CONNECTABLE);assert(d==ESP_BT_GENERAL_DISCOVERABLE||d==ESP_BT_NON_DISCOVERABLE);return step();}
esp_err_t esp_bt_gap_ssp_confirm_reply(uint8_t *bda,bool accept){assert(bda&&accept);return 0;}
esp_err_t esp_bt_gap_pin_reply(uint8_t *bda,bool accept,unsigned n,esp_bt_pin_code_t code){assert(bda&&!accept&&!n&&code);return 0;}
esp_err_t esp_avrc_tg_register_callback(void (*cb)(esp_avrc_tg_cb_event_t,esp_avrc_tg_cb_param_t *)){avrc=cb;return step();}
esp_err_t esp_avrc_tg_init(void){return step();}
esp_err_t esp_avrc_tg_deinit(void){return 0;}
bool esp_avrc_rn_evt_bit_mask_operation(int op,esp_avrc_rn_evt_cap_mask_t *m,int event){assert(op==ESP_AVRC_BIT_MASK_OP_SET&&event==ESP_AVRC_RN_VOLUME_CHANGE);m->bits=1;return true;}
esp_err_t esp_avrc_tg_set_rn_evt_cap(const esp_avrc_rn_evt_cap_mask_t *m){assert(m->bits==1);return step();}
esp_err_t esp_avrc_tg_send_rn_rsp(int event,int type,esp_avrc_rn_param_t *value){assert(event==ESP_AVRC_RN_VOLUME_CHANGE&&(type==ESP_AVRC_RN_RSP_INTERIM||type==ESP_AVRC_RN_RSP_CHANGED));notification=value->volume;return 0;}
esp_err_t esp_a2d_register_callback(void (*cb)(esp_a2d_cb_event_t,esp_a2d_cb_param_t *)){a2dp=cb;return step();}
esp_err_t esp_a2d_sink_register_data_callback(void (*cb)(const uint8_t *,uint32_t)){pcm=cb;return step();}
static void connection(unsigned state);
static void format(unsigned sf,unsigned mode);
static void audio(bool playing);
static unsigned delay_calls;static uint16_t requested_delay;
static esp_err_t delay_api_error;static bool delay_ack=true,delay_reject;
esp_err_t esp_a2d_sink_set_delay_value(uint16_t value){
    assert(!depth&&value);++delay_calls;requested_delay=value;
    if(delay_api_error)return delay_api_error;
    if(delay_ack){esp_a2d_cb_param_t p={0};p.a2d_set_delay_value_stat.delay_value=value;
        p.a2d_set_delay_value_stat.set_state=delay_reject?1:ESP_A2D_SET_SUCCESS;
        a2dp(ESP_A2D_SNK_SET_DELAY_VALUE_EVT,&p);}
    return ESP_OK;
}
static void delay_caps(bool supported){esp_a2d_cb_param_t p={0};p.a2d_psc_cfg_stat.psc_mask=supported?ESP_A2D_PSC_DELAY_RPT:0;a2dp(ESP_A2D_SNK_PSC_CFG_EVT,&p);}
static void delay_complete(uint16_t value){esp_a2d_cb_param_t p={0};p.a2d_set_delay_value_stat.delay_value=value;
    p.a2d_set_delay_value_stat.set_state=ESP_A2D_SET_SUCCESS;a2dp(ESP_A2D_SNK_SET_DELAY_VALUE_EVT,&p);}
/* Actual callback/render/poll path. Maintain 96ms-ish PCM with periodic packets,
 * not hidden writes to queue/filter state. RF and Android behavior are not faked. */
static void delay_feed(unsigned blocks){
    static uint8_t silence[512];uint8_t out[512];bool ready;
    for(unsigned i=0;i<blocks;++i){now+=2667;pcm(silence,sizeof(silence));
        assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&ready);chime_bluetooth_delay_poll();}
}
static void test_recovery(void)
{
    static uint8_t data[4096];uint8_t out[512];bool ready;
    memset(data,42,sizeof(data));
    const unsigned outages[]={200000,2000000,30000000,70000000};
    format(ESP_A2D_SBC_CIE_SF_48K,ESP_A2D_SBC_CIE_CH_MODE_STEREO);audio(true);
    assert(chime_bluetooth_output_begin()==ESP_OK);
    uint32_t epoch=chime_bluetooth_epoch();chime_bluetooth_status_t s;
    for(unsigned n=0;n<4;++n){
        for(unsigned i=0;i<6;++i)pcm(data,sizeof(data));
        unsigned blocks=0;
        do {now+=2667;assert(chime_bluetooth_render(out,128,&ready)==ESP_OK);assert(++blocks<60);}while(ready);
        now+=100000;assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&!ready);
        now+=outages[n];assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&!ready);
        chime_bluetooth_snapshot(&s);
        assert(s.stream_state==CHIME_BT_STREAM_REBUFFERING&&s.epoch==epoch&&s.output_active&&!s.error);
        assert(s.rebuffer_events==n+1&&s.rebuffer_resumes==n&&s.rebuffer_wait_ms==outages[n]/1000);
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
        assert(dec_cb.valid(epoch));
#endif
        /* Sparse packets remain silent until the full recovery waterline. */
        for(unsigned i=0;i<5;++i){pcm(data,sizeof(data));now+=100000;
            assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&!ready);
            for(unsigned j=0;j<sizeof(out);++j)assert(out[j]==0);}
        pcm(data,sizeof(data));now+=2667;assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&ready);
        chime_bluetooth_snapshot(&s);assert(s.rebuffer_resumes==n+1&&!s.rebuffer_wait_ms&&s.target_ms==112);
        assert(s.pcm_frames==s.consumed_frames+s.discarded_frames+s.queued_frames);
        for(unsigned j=0;j<sizeof(out);++j)assert(out[j]==42);
        now+=2667;assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&ready);
        chime_bluetooth_snapshot(&s);assert(s.target_ms==112); /* hold restarts after a >60s wait */
        /* Drain the remaining prebuffer before the next iteration. */
        do {now+=2667;assert(chime_bluetooth_render(out,128,&ready)==ESP_OK);}while(ready);
        /* Ending/re-admitting here would mask an epoch bug. Discarded tail is
         * small enough that the next six packets still fit this same output. */
    }
    /* STOP/output end cancels rebuffering without admitting late callbacks. */
    now+=100000;assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&!ready);
    chime_bluetooth_output_end();pcm(data,sizeof(data));
    chime_bluetooth_snapshot(&s);assert(!s.output_active&&s.stream_state==CHIME_BT_STREAM_IDLE&&s.rebuffer_cancels==1&&!s.queued_frames);
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    dec_cb.pcm(data,sizeof(data),epoch);dec_cb.fail(epoch,ESP_FAIL);
    assert(!dec_cb.valid(epoch));
#endif
    assert(chime_bluetooth_render(out,128,&ready)==ESP_ERR_INVALID_STATE);
    /* Pause invalidates the epoch; merely receiving PCM cannot resume. */
    assert(chime_bluetooth_output_begin()==ESP_OK);now+=500000;
    assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&!ready);
    audio(false);pcm(data,sizeof(data));
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    dec_cb.pcm(data,sizeof(data),epoch);dec_cb.fail(epoch,ESP_FAIL);
#endif
    chime_bluetooth_snapshot(&s);
    assert(!s.output_active&&!s.queued_frames&&!chime_bluetooth_wanted()&&s.rebuffer_cancels==1);
    audio(true);assert(chime_bluetooth_output_begin()==ESP_OK);now+=500000;
    assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&!ready);
    /* A hard error must still latch, even while waiting for RF recovery. */
    chime_bluetooth_fail(ESP_FAIL);pcm(data,sizeof(data));chime_bluetooth_snapshot(&s);
    assert(s.stream_state==CHIME_BT_STREAM_FAULT&&s.error==ESP_FAIL&&!s.output_active&&!chime_bluetooth_wanted());
    audio(false);audio(true);assert(chime_bluetooth_output_begin()==ESP_OK);now+=500000;
    assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&!ready);
    format(ESP_A2D_SBC_CIE_SF_44K,ESP_A2D_SBC_CIE_CH_MODE_MONO);
    pcm(data,sizeof(data));chime_bluetooth_snapshot(&s);assert(!s.output_active&&!s.queued_frames&&s.rebuffer_cancels==1);
    assert(chime_bluetooth_output_begin()==ESP_OK);now+=500000;
    assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&!ready);
    connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);pcm(data,sizeof(data));chime_bluetooth_snapshot(&s);
    assert(!s.connected&&!s.output_active&&!s.queued_frames&&s.rebuffer_cancels==1);
    connection(ESP_A2D_CONNECTION_STATE_CONNECTED);audio(true);
    puts("PASS recoverable BT starvation: repeated 0.2/2/30/70s outages, sparse prefill, same epoch/decoder admission, accounting, STOP/pause/format/disconnect/hard-fault cancellation");
}
static void test_delay_reports(void){
    format(ESP_A2D_SBC_CIE_SF_48K,ESP_A2D_SBC_CIE_CH_MODE_STEREO);audio(true);
    assert(chime_bluetooth_output_begin()==ESP_OK);uint8_t silence[512]={0},out[512];bool ready;
    chime_bluetooth_delay_poll();assert(!delay_calls); /* no caps / prefill */
    for(unsigned i=0;i<37;++i)pcm(silence,sizeof(silence));
    assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&ready);
    delay_feed(500);assert(!delay_calls); /* no negotiated delay reporting */
    delay_caps(false);delay_feed(10);assert(!delay_calls);
    delay_caps(true);chime_bluetooth_delay_poll();assert(delay_calls==1&&requested_delay<1200);
    chime_bluetooth_status_t s;chime_bluetooth_snapshot(&s);
    assert(s.delay_caps_known&&s.delay_supported&&s.delay_accepts==1&&!s.delay_pending);
    assert(s.delay_samples>400&&s.delay_estimate_us>90000&&s.delay_fixed_us==10667);
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    assert(s.delay_residence_us==3000);
#else
    assert(!s.delay_residence_us);
#endif
    unsigned before=delay_calls;delay_feed(900);assert(delay_calls==before); /* hysteresis */
    /* One additional 512-frame burst raises ACTUAL latency, not target. */
    for(unsigned i=0;i<4;++i)pcm(silence,sizeof(silence));
    delay_ack=false;delay_feed(900);assert(delay_calls==before+1);uint16_t pending=requested_delay;
    chime_bluetooth_snapshot(&s);assert(s.delay_pending&&s.delay_accepts==1);
    delay_complete((uint16_t)(pending+1));chime_bluetooth_delay_poll();
    chime_bluetooth_snapshot(&s);assert(s.delay_pending&&s.delay_accepts==1&&delay_calls==before+1);
    now+=3000001;chime_bluetooth_delay_poll();chime_bluetooth_snapshot(&s);
    assert(s.delay_timed_out&&s.delay_error==ESP_ERR_TIMEOUT&&s.delay_pending);
    /* Old completion after STOP/re-admission cannot populate fresh metrics. */
    chime_bluetooth_output_end();assert(chime_bluetooth_output_begin()==ESP_OK);
    delay_complete(pending);chime_bluetooth_snapshot(&s);assert(!s.delay_pending&&s.delay_accepts==1&&!s.delay_samples);
    for(unsigned i=0;i<37;++i)pcm(silence,sizeof(silence));
    assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&ready);
    delay_ack=true;delay_api_error=ESP_FAIL;before=delay_calls;delay_feed(500);assert(delay_calls==before+1);
    chime_bluetooth_snapshot(&s);assert(s.delay_error==ESP_FAIL&&!s.delay_pending&&s.output_active&&!s.error);
    delay_api_error=ESP_OK;delay_feed(2000);assert(delay_calls>=before+2);
    /* Callback-level rejection likewise never stops audio; retries are bounded. */
    chime_bluetooth_output_end();assert(chime_bluetooth_output_begin()==ESP_OK);
    for(unsigned i=0;i<37;++i)pcm(silence,sizeof(silence));
    assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&ready);
    delay_reject=true;before=delay_calls;delay_feed(500);assert(delay_calls==before+1);
    chime_bluetooth_snapshot(&s);assert(s.delay_error==ESP_FAIL&&!s.delay_pending&&!s.error);
    delay_reject=false;delay_feed(2000);chime_bluetooth_snapshot(&s);assert(s.delay_error==ESP_OK);
    /* Reconnect with a retired request still pending: do not attribute the
     * old completion to a new phone, nor inherit the previous capability. */
    chime_bluetooth_output_end();assert(chime_bluetooth_output_begin()==ESP_OK);
    for(unsigned i=0;i<37;++i)pcm(silence,sizeof(silence));
    assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&ready);
    delay_ack=false;delay_feed(500);pending=requested_delay;
    connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    connection(ESP_A2D_CONNECTION_STATE_CONNECTING);delay_caps(true);
    connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
    format(ESP_A2D_SBC_CIE_SF_48K,ESP_A2D_SBC_CIE_CH_MODE_STEREO);audio(true);
    assert(chime_bluetooth_output_begin()==ESP_OK);
    for(unsigned i=0;i<37;++i)pcm(silence,sizeof(silence));
    assert(chime_bluetooth_render(out,128,&ready)==ESP_OK&&ready);
    delay_feed(500);chime_bluetooth_snapshot(&s);
    assert(s.delay_supported&&!s.delay_accepts&&!s.delay_requests);
    delay_complete(pending);chime_bluetooth_snapshot(&s);assert(!s.delay_accepts);
    delay_ack=true;chime_bluetooth_delay_poll();chime_bluetooth_snapshot(&s);assert(s.delay_accepts==1);
    audio(false);before=delay_calls;chime_bluetooth_delay_poll();assert(delay_calls==before);
    connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);chime_bluetooth_delay_poll();
    chime_bluetooth_snapshot(&s);assert(!s.delay_caps_known&&!s.delay_supported&&!s.delay_accepted_100us);
    connection(ESP_A2D_CONNECTION_STATE_CONNECTED);
}
esp_err_t esp_a2d_sink_init(void){int e=step();if(!e&&!no_profile_ack){esp_a2d_cb_param_t p={0};p.a2d_prof_stat.init_state=ESP_A2D_INIT_SUCCESS;a2dp(ESP_A2D_PROF_STATE_EVT,&p);}return e;}
esp_err_t esp_a2d_sink_deinit(void){if(!no_profile_ack){esp_a2d_cb_param_t p={0};p.a2d_prof_stat.init_state=ESP_A2D_DEINIT_SUCCESS;a2dp(ESP_A2D_PROF_STATE_EVT,&p);}return 0;}
static void connection(unsigned state){esp_a2d_cb_param_t p={0};p.conn_stat.state=state;a2dp(ESP_A2D_CONNECTION_STATE_EVT,&p);}
static void audio(bool play){esp_a2d_cb_param_t p={0};p.audio_stat.state=play?ESP_A2D_AUDIO_STATE_STARTED:ESP_A2D_AUDIO_STATE_SUSPEND;a2dp(ESP_A2D_AUDIO_STATE_EVT,&p);}
static void format(unsigned rate,unsigned channels){esp_a2d_cb_param_t p={0};p.audio_cfg.mcc.cie.sbc_info.samp_freq=rate;p.audio_cfg.mcc.cie.sbc_info.ch_mode=channels;a2dp(ESP_A2D_AUDIO_CFG_EVT,&p);}
#if CONFIG_CHIME_LDAC_REPLAY_TEST
static void test_replay_session(void)
{
    uint32_t token=0;chime_bluetooth_status_t s;uint8_t data[512]={0};
    esp_a2d_audio_buff_t b={.data_len=sizeof(data),.data=data};
    assert(chime_bluetooth_replay_begin(96000,&token)==ESP_ERR_INVALID_ARG);
    assert(chime_bluetooth_replay_begin(48000,NULL)==ESP_ERR_INVALID_ARG);
    assert(chime_bluetooth_replay_begin(48000,&token)==ESP_OK);
    chime_bluetooth_snapshot(&s);
    assert(s.simulated&&!s.connected&&!s.playing&&!s.output_active&&s.volume==-30*256);
    assert(!chime_bluetooth_connected()&&chime_bluetooth_wanted());
    unsigned n=freed,old_submitted=submitted;
    assert(!chime_bluetooth_replay_submit(&b,token)); /* owner not admitted yet */
    assert(freed==n+1&&submitted==old_submitted);
    assert(chime_bluetooth_output_begin()==ESP_OK&&dec_cb.valid(token));
    assert(chime_bluetooth_replay_begin(44100,&token)==ESP_ERR_INVALID_STATE);
    assert(chime_bluetooth_replay_submit(&b,token)&&submitted==old_submitted+1);
    chime_bluetooth_snapshot(&s);assert(s.packets&&s.queued_frames==128);
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    chime_bluetooth_output_end();assert(chime_bluetooth_output_begin_clocked(true)==ESP_OK);
    for(unsigned i=0;i<128;++i){data[4*i]=0;data[4*i+1]=0x80;data[4*i+2]=0xff;data[4*i+3]=0x7f;}
    for(unsigned i=0;i<36;++i)dec_cb.pcm(data,sizeof(data),token);
    int32_t direct[256];bool direct_ready=false;
    assert(chime_bluetooth_render_q27(direct,128,&direct_ready)==ESP_OK&&direct_ready);
    for(unsigned i=0;i<128;++i)assert(direct[2*i]==-134217728&&direct[2*i+1]==134213632);
    chime_bluetooth_snapshot(&s);assert(s.clock_follow&&s.consumed_frames==128&&s.drift_ppm==0);
    unsigned fixed_target=s.target_frames;now+=90000;dec_cb.pcm(data,sizeof(data),token);
    chime_bluetooth_snapshot(&s);assert(s.target_frames==fixed_target);
    chime_bluetooth_output_end();chime_bluetooth_snapshot(&s);assert(!s.clock_follow);
    assert(chime_bluetooth_output_begin()==ESP_OK);
#endif
    chime_bluetooth_replay_end(token+1,ESP_FAIL);assert(dec_cb.valid(token));
    chime_bluetooth_replay_end(token,ESP_OK);
    assert(!chime_bluetooth_wanted()&&!dec_cb.valid(token));
    dec_cb.pcm(data,sizeof(data),token);chime_bluetooth_snapshot(&s);assert(!s.queued_frames&&!s.simulated);
    assert(!chime_bluetooth_replay_submit(&b,token));
    assert(chime_bluetooth_replay_begin(44100,&token)==ESP_OK);
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    assert(chime_bluetooth_output_begin_clocked(true)==ESP_ERR_INVALID_STATE);
#endif
    assert(chime_bluetooth_output_begin()==ESP_OK);
    connection(ESP_A2D_CONNECTION_STATE_CONNECTED); /* real link preempts */
    chime_bluetooth_snapshot(&s);assert(s.connected&&!s.simulated&&s.epoch!=token&&!s.queued_frames);
    chime_bluetooth_replay_end(token,ESP_FAIL);chime_bluetooth_snapshot(&s);assert(s.connected&&!s.error);
    assert(chime_bluetooth_replay_begin(48000,&token)==ESP_ERR_INVALID_STATE);
    connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
    assert(chime_bluetooth_replay_begin(48000,&token)==ESP_OK);
    assert(chime_bluetooth_output_begin()==ESP_OK);dec_cb.fail(token,ESP_ERR_NO_MEM);
    assert(!dec_cb.valid(token)&&!chime_bluetooth_wanted());
    chime_bluetooth_replay_end(token,ESP_OK); /* STOP revokes faulted session but cannot clear its error */
    chime_bluetooth_snapshot(&s);assert(s.error==ESP_ERR_NO_MEM&&!s.simulated);
    assert(chime_bluetooth_replay_begin(44100,&token)==ESP_OK);
    /* A real disconnect transition revokes the new session too. */
    connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);chime_bluetooth_snapshot(&s);assert(!s.simulated);
}
#endif
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
static void test_pipeline(void)
{
    assert(dec_cb.service);
    uint8_t caps[CHIME_VENDOR_WIRE_MAX],chosen[CHIME_VENDOR_WIRE_MAX];
    chime_vendor_capability(CHIME_VENDOR_LDAC,caps);assert(chime_vendor_select(caps,chosen));
    esp_a2d_cb_param_t vendor={0};vendor.audio_cfg.mcc.type=ESP_A2D_MCT_NON_A2DP;
    vendor.audio_cfg.mcc.cie.vendor[0]=(uint8_t)(chosen[0]-2);
    memcpy(vendor.audio_cfg.mcc.cie.vendor+1,chosen+3,chosen[0]-2U);
    a2dp(ESP_A2D_AUDIO_CFG_EVT,&vendor);audio(true);
    assert(chime_bluetooth_wanted()&&chime_bluetooth_output_begin()==ESP_OK);
    uint32_t epoch=chime_bluetooth_epoch();uint8_t input[4096];memset(input,1,sizeof(input));
    for(unsigned i=0;i<6;++i)dec_cb.pcm(input,1024,epoch);
    int32_t pcm32[256];bool ready=true;chime_bluetooth_status_t st;
    assert(chime_bluetooth_render_q27(pcm32,128,&ready)==ESP_OK&&!ready);
    chime_bluetooth_snapshot(&st);assert(st.pipeline_enabled&&!st.pipeline_produced&&st.queued_frames==1536);
    /* Consumer does not prepare/FIR or consume the input queue. */
    for(unsigned i=0;i<16;++i)dec_cb.pcm(input,1024,epoch);
    dec_cb.service(epoch);chime_bluetooth_snapshot(&st);assert(st.pipeline_produced==1&&st.pipeline_ready_blocks==1);
    assert(chime_bluetooth_render_q27(pcm32,128,&ready)==ESP_OK&&!ready);
    dec_cb.service(epoch);
    assert(chime_bluetooth_render_q27(pcm32,128,&ready)==ESP_OK&&ready);
    for(unsigned i=0;i<8;++i)dec_cb.service(epoch);
    chime_bluetooth_snapshot(&st);assert(st.pipeline_ready_blocks==4&&st.pipeline_high_water==4);
    unsigned produced=st.pipeline_produced,queued=st.queued_frames;
    dec_cb.service(epoch);chime_bluetooth_snapshot(&st);
    assert(st.pipeline_produced==produced&&st.queued_frames==queued);
    for(unsigned i=0;i<4;++i)assert(chime_bluetooth_render_q27(pcm32,128,&ready)==ESP_OK&&ready);
    assert(chime_bluetooth_render_q27(pcm32,128,&ready)==ESP_OK&&!ready);
    chime_bluetooth_snapshot(&st);assert(st.pipeline_underruns==1);
    chime_bluetooth_output_end();assert(chime_bluetooth_render_q27(pcm32,128,&ready)==ESP_ERR_INVALID_STATE);
    assert(chime_bluetooth_output_begin()==ESP_OK);chime_bluetooth_snapshot(&st);
    assert(!st.pipeline_ready_blocks&&!st.pipeline_produced&&!st.pipeline_underruns);
    audio(false);audio(true);assert(chime_bluetooth_output_begin()==ESP_OK);
    dec_cb.service(epoch);chime_bluetooth_snapshot(&st);assert(!st.pipeline_ready_blocks&&!st.pipeline_produced);
    chime_bluetooth_output_end();format(ESP_A2D_SBC_CIE_SF_48K,ESP_A2D_SBC_CIE_CH_MODE_STEREO);
    puts("PASS actual LDAC pipeline callbacks: CPU0 consumption-only, CPU1 service, bounded ready bridge, backpressure, underrun, STOP/restart/stale epoch");
}
#endif
int main(void)
{
    assert(chime_bluetooth_start()==0);unsigned init_calls=calls;chime_bluetooth_stop();
    for(fail_at=1;fail_at<=init_calls;++fail_at){calls=0;assert(chime_bluetooth_start()!=0&&!controller&&!host&&!chime_bluetooth_connected());}
    fail_at=0;calls=0;assert(chime_bluetooth_start()==0);assert(chime_bluetooth_start()==ESP_ERR_INVALID_STATE);
    chime_bluetooth_status_t s;chime_bluetooth_snapshot(&s);assert(s.available&&!s.connected&&!s.playing);
    avrc(ESP_AVRC_TG_CONNECTION_STATE_EVT,&(esp_avrc_tg_cb_param_t){.conn_stat={.connected=true}});
    connection(ESP_A2D_CONNECTION_STATE_CONNECTING);assert(!chime_bluetooth_connected());
    delay_caps(true); /* Actual IDF SETCONFIG ordering: PSC before OPEN/CONNECTED. */
    connection(ESP_A2D_CONNECTION_STATE_CONNECTED);assert(chime_bluetooth_connected()&&!chime_bluetooth_wanted());
    chime_bluetooth_snapshot(&s);assert(s.delay_caps_known&&s.delay_supported);delay_caps(false);
    assert(chime_bluetooth_adjust_volume(2)==ESP_ERR_INVALID_STATE); /* unknown phone baseline */
    uint32_t epoch=chime_bluetooth_epoch();connection(ESP_A2D_CONNECTION_STATE_CONNECTED);assert(chime_bluetooth_epoch()==epoch);
    format(ESP_A2D_SBC_CIE_SF_44K,ESP_A2D_SBC_CIE_CH_MODE_STEREO);audio(true);assert(chime_bluetooth_wanted());
    assert(chime_bluetooth_output_begin()==0);
    chime_bluetooth_snapshot(&s);uint32_t first_output=s.output_id,first_epoch=s.epoch;
    chime_bluetooth_output_end();assert(chime_bluetooth_output_begin()==0);
    chime_bluetooth_snapshot(&s);assert(s.output_id!=first_output&&s.epoch==first_epoch);
    assert(s.target_ms==CHIME_BT_TARGET_START_MS&&s.target_frames==(44100U*CHIME_BT_TARGET_START_MS+999U)/1000U);
    uint8_t data[4096]={0},output[512];bool ready=true;
    assert(chime_bluetooth_render(output,128,&ready)==0&&!ready);
    chime_bluetooth_snapshot(&s);assert(s.prefill_blocks==1&&!s.pcm_gap_events&&!s.pcm_gap_blocks&&!s.primed&&s.output_active);
    for(unsigned i=0;i<5;++i)pcm(data,sizeof(data));memset(data,127,sizeof(data)); /* borrowed input copied */
    assert(chime_bluetooth_render(output,128,&ready)==0&&ready);for(size_t i=0;i<sizeof(output);++i)assert(output[i]==0);
    /* Sub-100ms holes previously inserted zeros without any diagnostic count.
     * Initial prefill is NOT a gap; short recovery preserves PCM. */
    unsigned drained=0;
    do {now+=2667;assert(chime_bluetooth_render(output,128,&ready)==0);assert(++drained<60);}while(ready);
    for(size_t i=0;i<sizeof(output);++i)assert(output[i]==0);
    chime_bluetooth_snapshot(&s);assert(s.primed&&s.pcm_gap_events==1&&s.pcm_gap_blocks==1&&s.pcm_gap_max_blocks==1&&!s.underruns);
    assert(s.target_ms==104&&s.target_raises==1);
    now+=2667;assert(chime_bluetooth_render(output,128,&ready)==0&&!ready);
    chime_bluetooth_snapshot(&s);assert(s.pcm_gap_events==1&&s.pcm_gap_blocks==2&&s.pcm_gap_max_blocks==2);
    now+=50000;pcm(data,sizeof(data));now+=2667;assert(chime_bluetooth_render(output,128,&ready)==0&&ready);
    drained=0;
    do {now+=2667;assert(chime_bluetooth_render(output,128,&ready)==0);assert(++drained<60);}while(ready);
    chime_bluetooth_snapshot(&s);assert(s.pcm_gap_events==2&&s.pcm_gap_blocks==3&&s.pcm_gap_max_blocks==2&&!s.underruns&&!s.error);
    assert(s.gap_epoch==s.epoch&&s.gap_queue<s.gap_needed&&s.gap_needed>0);
    assert(s.gap_pcm_age_us>0&&s.pcm_interval_max_us>0);
    assert(s.pcm_frames==6144&&s.consumed_frames+s.queued_frames==s.pcm_frames);
    assert(s.rendered_frames>0&&s.queue_min<s.gap_needed&&s.queue_max>=1024);
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    assert(s.gap_rx_age_us==s.gap_pcm_age_us&&s.rx_interval_max_us==s.pcm_interval_max_us);
#else
    assert(s.gap_rx_age_us==UINT32_MAX&&!s.rx_interval_max_us);
#endif
    uint64_t saved_frames=s.pcm_frames;
    /* A long hole keeps the SAME admitted epoch and decoder gate alive. */
    now+=100000;assert(chime_bluetooth_render(output,128,&ready)==ESP_OK&&!ready);
    chime_bluetooth_snapshot(&s);assert(!s.underruns&&s.pcm_gap_blocks==4&&s.output_active&&!s.primed&&s.connected&&!s.error);
    assert(s.stream_state==CHIME_BT_STREAM_REBUFFERING&&s.rebuffer_events==1&&chime_bluetooth_wanted());
    assert(s.pcm_frames==saved_frames&&s.gap_epoch==s.epoch&&s.target_ms==112);
    assert(s.consumed_frames+s.discarded_frames==s.pcm_frames&&s.discarded_frames>0);
    uint32_t recovery_epoch=s.epoch,recovery_output=s.output_id;
    now+=30000000;assert(chime_bluetooth_render(output,128,&ready)==ESP_OK&&!ready);
    chime_bluetooth_snapshot(&s);assert(s.rebuffer_wait_ms==30000&&s.rebuffer_blocks==1&&s.prefill_blocks==1);
    pcm(data,sizeof(data));assert(chime_bluetooth_render(output,128,&ready)==ESP_OK&&!ready);
    for(unsigned i=0;i<4;++i)pcm(data,sizeof(data));
    assert(chime_bluetooth_render(output,128,&ready)==ESP_OK&&ready);
    chime_bluetooth_snapshot(&s);
    assert(s.stream_state==CHIME_BT_STREAM_PLAYING&&s.rebuffer_resumes==1&&!s.rebuffer_wait_ms&&s.rebuffer_last_wait_ms==30000);
    assert(s.epoch==recovery_epoch&&s.output_id==recovery_output&&!s.error);
    assert(s.consumed_frames+s.discarded_frames+s.queued_frames==s.pcm_frames);
    saved_frames=s.pcm_frames;
    chime_bluetooth_output_end();chime_bluetooth_snapshot(&s);assert(s.pcm_frames==saved_frames);
    /* New admission starts a fresh set of gap counters, NOT a source switch. */
    audio(false);audio(true);assert(chime_bluetooth_output_begin()==0);
    chime_bluetooth_snapshot(&s);assert(!s.pcm_gap_events&&!s.pcm_gap_blocks&&!s.pcm_gap_max_blocks&&!s.prefill_blocks);
    assert(!s.pcm_frames&&!s.consumed_frames&&!s.rendered_frames&&!s.gap_epoch&&!s.queue_max&&!s.rx_interval_max_us);
    avrc(ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT,&(esp_avrc_tg_cb_param_t){.set_abs_vol={0}});assert(chime_bluetooth_volume()==-96*256);
    avrc(ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT,&(esp_avrc_tg_cb_param_t){.set_abs_vol={127}});assert(chime_bluetooth_volume()==0);
    avrc(ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT,&(esp_avrc_tg_cb_param_t){.reg_ntf={ESP_AVRC_RN_VOLUME_CHANGE}});assert(notification==127);
    assert(chime_bluetooth_adjust_volume(-2)==ESP_OK);chime_bluetooth_volume_notify();assert(notification==125);
    assert(chime_bluetooth_adjust_volume(-2)==ESP_OK);assert(chime_bluetooth_adjust_volume(-2)==ESP_OK);
    chime_bluetooth_volume_notify();assert(notification==125); /* subscription consumed, deltas retained */
    avrc(ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT,&(esp_avrc_tg_cb_param_t){.reg_ntf={ESP_AVRC_RN_VOLUME_CHANGE}});
    chime_bluetooth_volume_notify();assert(notification==121);
    avrc(ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT,&(esp_avrc_tg_cb_param_t){.set_abs_vol={40}});
    assert(chime_bluetooth_adjust_volume(2)==ESP_OK);
    avrc(ESP_AVRC_TG_REGISTER_NOTIFICATION_EVT,&(esp_avrc_tg_cb_param_t){.reg_ntf={ESP_AVRC_RN_VOLUME_CHANGE}});
    chime_bluetooth_volume_notify();assert(notification==42); /* relative to NEW phone value */
    assert(chime_bluetooth_adjust_volume(-1000)==ESP_OK);assert(chime_bluetooth_volume()==-96*256);
    assert(chime_bluetooth_adjust_volume(2)==ESP_OK);assert(chime_bluetooth_volume()>-60*256);
    assert(chime_bluetooth_adjust_volume(1000)==ESP_OK);assert(chime_bluetooth_volume()==0);
    audio(false);assert(chime_bluetooth_connected()&&!chime_bluetooth_wanted());audio(true);assert(chime_bluetooth_wanted());
    assert(chime_bluetooth_output_begin()==0);now+=501000;assert(chime_bluetooth_render(output,128,&ready)==ESP_OK&&!ready);
    chime_bluetooth_snapshot(&s);assert(s.connected&&!s.underruns&&!s.error&&chime_bluetooth_wanted());
    assert(s.stream_state==CHIME_BT_STREAM_REBUFFERING&&s.rebuffer_events==1&&!s.pcm_gap_events);
    for(unsigned i=0;i<5;++i)pcm(data,sizeof(data));
    assert(chime_bluetooth_render(output,128,&ready)==ESP_OK&&ready);
    audio(false);audio(true);assert(chime_bluetooth_wanted());assert(chime_bluetooth_output_begin()==0);
    for(unsigned i=0;i<9;++i)pcm(data,sizeof(data));chime_bluetooth_snapshot(&s);assert(s.overflows==1&&s.connected&&s.error==ESP_ERR_NO_MEM);
#if CHIME_BT_CAPTURE_LOG
    assert(fault_headers&&fault_bins);
    assert(strstr(fault_pcm,"pushed=8192 consumed=0 rendered=0 discarded=0"));
    assert(strstr(fault_start,"first_pcm_us=0 first_render_us=4294967295"));
    assert(strstr(fault_mix,"src_blocks=54 src_avg_us=3000")&&strstr(fault_mix,"src_active=1 src_inflight_us=1700"));
#endif
    format(ESP_A2D_SBC_CIE_SF_48K,ESP_A2D_SBC_CIE_CH_MODE_MONO);assert(chime_bluetooth_wanted());
    assert(chime_bluetooth_output_begin()==0);
    pcm(data,1);chime_bluetooth_snapshot(&s);assert(s.error==ESP_ERR_INVALID_SIZE&&s.connected);
    format(3,ESP_A2D_SBC_CIE_CH_MODE_STEREO);assert(!chime_bluetooth_wanted());chime_bluetooth_snapshot(&s);assert(s.error==ESP_ERR_NOT_SUPPORTED);
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    /* Reproduce startup pressure: before output admission, even a long burst
     * is freed without running a decoder, filling PCM, or latching a fault.
     * STOP/end closes the same gate, but an admitted stream still decodes. */
    format(ESP_A2D_SBC_CIE_SF_48K,ESP_A2D_SBC_CIE_CH_MODE_STEREO);
    uint32_t pending=chime_bluetooth_epoch();
    unsigned dropped_before=freed,submitted_before=submitted;
    esp_a2d_audio_buff_t pending_buffer={.data_len=512,.data=data};
    for(unsigned i=0;i<64;++i)raw(0,&pending_buffer);
    chime_bluetooth_snapshot(&s);
    assert(freed==dropped_before+64&&submitted==submitted_before&&s.queued_frames==0&&s.error==0);
    assert(chime_bluetooth_wanted()&&!dec_cb.valid(pending));
    assert(dec_cb.pcm_capacity&&dec_cb.pcm_capacity(pending)==0);
    assert(chime_bluetooth_output_begin()==0&&dec_cb.valid(pending));
    assert(dec_cb.pcm_capacity(pending)==8192);
    raw(0,&pending_buffer);chime_bluetooth_snapshot(&s);
    assert(submitted==submitted_before+1&&s.queued_frames==128&&s.error==0);
    assert(dec_cb.pcm_capacity(pending)==8192-128);
    chime_bluetooth_output_end();assert(!dec_cb.valid(pending)&&dec_cb.pcm_capacity(pending)==0);
    for(unsigned i=0;i<64;++i)raw(0,&pending_buffer);
    dec_cb.pcm(data,512,pending);dec_cb.fail(pending,ESP_ERR_INVALID_SIZE);
    chime_bluetooth_snapshot(&s);assert(s.queued_frames==0&&s.error==0&&chime_bluetooth_wanted());
    /* Decode begun in an earlier epoch must not enqueue PCM or latch errors. */
    format(ESP_A2D_SBC_CIE_SF_48K,ESP_A2D_SBC_CIE_CH_MODE_STEREO);
    uint32_t old=chime_bluetooth_epoch();audio(false);audio(true);
    assert(dec_cb.pcm_capacity(old)==0);
    dec_cb.pcm(data,sizeof(data),old);dec_cb.fail(old,ESP_FAIL);
    chime_bluetooth_snapshot(&s);assert(s.error==0&&s.queued_frames==0);
    unsigned n=freed,sub=submitted;esp_a2d_audio_buff_t b={.data_len=4,.data=data};raw(77,&b);assert(freed==n+1&&submitted==sub);
    uint32_t current=chime_bluetooth_epoch();
    esp_a2d_cb_param_t retired={0};retired.audio_stat.conn_hdl=77;retired.audio_cfg.conn_hdl=77;retired.conn_stat.conn_hdl=77;
    retired.conn_stat.state=ESP_A2D_CONNECTION_STATE_DISCONNECTED;
    a2dp(ESP_A2D_AUDIO_STATE_EVT,&retired);a2dp(ESP_A2D_AUDIO_CFG_EVT,&retired);a2dp(ESP_A2D_CONNECTION_STATE_EVT,&retired);
    assert(chime_bluetooth_epoch()==current&&chime_bluetooth_wanted());
    esp_a2d_cb_param_t config={0};config.audio_cfg.mcc.type=ESP_A2D_MCT_M24;
    config.audio_cfg.mcc.cie.m24_info=(esp_a2d_cie_m24_t){.obj_type=ESP_A2D_M24_CIE_OBJ_TYPE_4_AAC_LC,.samp_freq2=ESP_A2D_M24_CIE_SF2_48K,.ch=ESP_A2D_M24_CIE_CH_2};
    a2dp(ESP_A2D_AUDIO_CFG_EVT,&config);chime_bluetooth_snapshot(&s);assert(s.codec==CHIME_BT_CODEC_AAC&&s.rate==48000&&s.channels==2&&chime_bluetooth_wanted());
    assert(!strcmp(chime_bluetooth_codec_name(s.codec),"AAC"));
    config.audio_cfg.mcc.cie.m24_info.samp_freq2=ESP_A2D_M24_CIE_SF2_96K;
    a2dp(ESP_A2D_AUDIO_CFG_EVT,&config);assert(!chime_bluetooth_wanted());
    config.audio_cfg.mcc.cie.m24_info.samp_freq2=0;config.audio_cfg.mcc.cie.m24_info.samp_freq1=ESP_A2D_M24_CIE_SF1_44K;
    config.audio_cfg.mcc.cie.m24_info.ch=ESP_A2D_M24_CIE_CH_1;
    a2dp(ESP_A2D_AUDIO_CFG_EVT,&config);chime_bluetooth_snapshot(&s);assert(s.rate==44100&&s.channels==1&&chime_bluetooth_wanted());
    config.audio_cfg.mcc.cie.m24_info.obj_type=ESP_A2D_M24_CIE_OBJ_TYPE_4_HE_AAC;
    a2dp(ESP_A2D_AUDIO_CFG_EVT,&config);assert(!chime_bluetooth_wanted());
#endif
#if CONFIG_CHIME_BT_VENDOR_CODECS
    for(unsigned kind=CHIME_VENDOR_APTX;kind<=(CONFIG_CHIME_BT_LHDC_V5?CHIME_VENDOR_LHDC_V5:CHIME_VENDOR_LDAC);++kind){
        uint8_t caps[CHIME_VENDOR_WIRE_MAX],chosen[CHIME_VENDOR_WIRE_MAX];chime_vendor_capability(kind,caps);assert(chime_vendor_select(caps,chosen));
        esp_a2d_cb_param_t vendor={0};vendor.audio_cfg.mcc.type=ESP_A2D_MCT_NON_A2DP;
        vendor.audio_cfg.mcc.cie.vendor[0]=(uint8_t)(chosen[0]-2);memcpy(vendor.audio_cfg.mcc.cie.vendor+1,chosen+3,chosen[0]-2U);
        a2dp(ESP_A2D_AUDIO_CFG_EVT,&vendor);chime_bluetooth_snapshot(&s);
        assert(s.codec==(chime_bt_codec_t)kind&&s.rate==48000&&s.channels==2&&chime_bluetooth_wanted());
        vendor.audio_cfg.mcc.cie.vendor[7]=0xff; /* unsupported negotiated parameter bits */
        a2dp(ESP_A2D_AUDIO_CFG_EVT,&vendor);assert(!chime_bluetooth_wanted());
    }
#endif
#if CONFIG_CHIME_LDAC_PIPELINE_TEST
    test_pipeline();
#endif
    test_recovery();
    test_delay_reports();
    connection(ESP_A2D_CONNECTION_STATE_DISCONNECTING);assert(chime_bluetooth_connected());
    connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);assert(!chime_bluetooth_connected()&&!chime_bluetooth_wanted());
    assert(chime_bluetooth_adjust_volume(2)==ESP_ERR_INVALID_STATE);
    avrc(ESP_AVRC_TG_CONNECTION_STATE_EVT,&(esp_avrc_tg_cb_param_t){.conn_stat={.connected=false}});
    avrc(ESP_AVRC_TG_CONNECTION_STATE_EVT,&(esp_avrc_tg_cb_param_t){.conn_stat={.connected=true}});
    avrc(ESP_AVRC_TG_SET_ABSOLUTE_VOLUME_CMD_EVT,&(esp_avrc_tg_cb_param_t){.set_abs_vol={30}});
    connection(ESP_A2D_CONNECTION_STATE_CONNECTED); /* AVRCP may arrive FIRST. */
    assert(chime_bluetooth_adjust_volume(2)==ESP_OK);
    assert(chime_bluetooth_volume()==-(int)((127U-32U)*60U*256U/126U));
    avrc(ESP_AVRC_TG_CONNECTION_STATE_EVT,&(esp_avrc_tg_cb_param_t){.conn_stat={.connected=false}});
    assert(chime_bluetooth_adjust_volume(2)==ESP_ERR_INVALID_STATE);
    connection(ESP_A2D_CONNECTION_STATE_DISCONNECTED);
#if CONFIG_CHIME_LDAC_REPLAY_TEST
    test_replay_session();
#endif
    gap(ESP_BT_GAP_PIN_REQ_EVT,&(esp_bt_gap_cb_param_t){0});gap(ESP_BT_GAP_CFM_REQ_EVT,&(esp_bt_gap_cb_param_t){0});
    chime_bluetooth_stop();assert(!host&&!controller);chime_bluetooth_stop();
    assert(chime_bluetooth_start()==0);disable_error=true;chime_bluetooth_stop();
    assert(host==2&&controller==2&&chime_bluetooth_start()==ESP_ERR_INVALID_STATE);
    disable_error=false;chime_bluetooth_stop();assert(!host&&!controller);
    no_profile_ack=true;assert(chime_bluetooth_start()==ESP_ERR_TIMEOUT&&controller==2&&host==2);
    assert(chime_bluetooth_start()==ESP_ERR_INVALID_STATE); /* never reuse live SDK state */
    no_profile_ack=false;chime_bluetooth_stop();assert(!host&&!controller);
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    no_sep_ack=true;assert(chime_bluetooth_start()==ESP_ERR_NOT_SUPPORTED&&!host&&!controller&&!dec_live);no_sep_ack=false;
    sep_failure=true;assert(chime_bluetooth_start()==ESP_ERR_NOT_SUPPORTED&&!host&&!controller&&!dec_live);sep_failure=false;
    assert(chime_bluetooth_start()==0);dec_stop_failure=true;chime_bluetooth_stop();assert(dec_live&&host==2&&chime_bluetooth_start()==ESP_ERR_INVALID_STATE);
    dec_stop_failure=false;chime_bluetooth_stop();assert(!dec_live&&!host&&!controller);
#endif
    puts("PASS actual BT callbacks/lifecycle: init faults, A2DP-only priority, pause ownership, copied PCM, independent AVRCP, prefill/overflow/misalignment errors, malformed format, bounded teardown and retry safety; no RF/RTOS timing simulated");
    return 0;
}

#include "../../main/generated/chime_dsp_parameters.h"
#include "bench_sound.h"
#include "bench_tone.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static uint8_t regs[128],sub_regs[128],route_data[12];
static bool lr_powered,sub_powered,corrupt_route;
static int corrupt_reg=-1,external_bad_reg=-1;
static unsigned route_index(uint16_t reg){
    assert(reg==CHIME_DSP_GAIN_SUB||reg==CHIME_DSP_GAIN_R||reg==CHIME_DSP_GAIN_L);
    return reg==CHIME_DSP_GAIN_SUB?0U:reg==CHIME_DSP_GAIN_R?4U:8U;
}
static unsigned dsp_writes;
static unsigned calls,fail_at,startup_calls,unmutes,pumps,ends,power_ons;
static bool powered,tone_on,cancelled,finish,bad_pll,bad_core,bad_asrc;
static uint64_t now,play_at,cancel_at,fault_at;
static int lock_delay_ms,state_delay_ms;
static bool stuck_hiz;
static unsigned play_reads;
static bool last_pending;
static int step(void){return ++calls==fail_at?-99:0;}
static esp_err_t rd(void *ctx,unsigned target,uint16_t reg,uint8_t *out,size_t n)
{
    (void)ctx;int err=step();if(err)return err;
    if(target==0) {
        out[0]=0;
        if(reg<0xf000){assert(n==4&&!powered);memcpy(out,route_data+route_index(reg),4);if(corrupt_route)out[0]^=1;return 0;}
        if(reg==0xf004)out[1]=bad_pll?0:1;
        else if(reg==0xf405)out[1]=bad_core?0:3;
        else if(reg==0xf580)out[1]=bad_asrc?0:1;
        else {
            assert(n==2);
            uint16_t value=0;
            switch(reg){
                case 0xf401:case 0xf188:case 0xf190:case 0xf201:case 0xf215:case 0xf219:value=2;break;
                case 0xf100:value=1;break;
                case 0xf140:value=5;break;
                case 0xf214:case 0xf218:value=0x9000;break;
                default:assert(reg==0xf200);break;
            }
            if(external_bad_reg==(int)reg)value^=1;
            out[0]=(uint8_t)(value>>8);out[1]=(uint8_t)value;
        }
    } else {
        assert((target==1?lr_powered:target==2&&sub_powered)&&reg+n<=128);
        uint8_t *bank=target==1?regs:sub_regs;memcpy(out,bank+reg,n);
        if(reg==0x37&&n==3) {
            if(bank[3]==0x0b)++play_reads;
            if(lock_delay_ms && (!play_at||lock_delay_ms<0||now-play_at<(uint64_t)lock_delay_ms*1000U))out[2]&=0xf7U;
            last_pending=(out[2]&8)==0;
        }
        if(reg==0x68&&bank[3]==0x0b&&
           (stuck_hiz||(state_delay_ms&&now-play_at<(uint64_t)state_delay_ms*1000U)))out[0]=2;
        if(reg==0x70&&fault_at&&now>=fault_at)out[0]=1;
        if((int)reg==corrupt_reg)out[0]^=1U;
    }
    return 0;
}
static esp_err_t wr(void *ctx,unsigned target,uint16_t reg,const uint8_t *in,size_t n)
{
    (void)ctx;
    int err=step();if(err)return err;
    if(target==0){++dsp_writes;assert(n==4&&!powered);memcpy(route_data+route_index(reg),in,4);return 0;}
    assert((target==1?lr_powered:target==2&&sub_powered)&&n==1&&reg<128);
    uint8_t *bank=target==1?regs:sub_regs;
    if(reg==3&&in[0]==3){
        assert(tone_on&&bank[0x02]==0&&bank[0x4c]==0x30&&bank[0x54]==0x10&&bank[0x40]==1&&bank[0x46]==9);
        assert(bank[0x33]==0x02&&bank[0x34]==0&&bank[0x35]==0x11); /* independent BTL slots */
        ++unmutes;
    }
    if(reg==3&&in[0]==0x0b)play_at=now;
    if(reg==3)bank[0x68]=in[0]&3U;
    bank[reg]=in[0];return 0;
}
static esp_err_t power(void *ctx,bool lr,bool sub)
{
    (void)ctx;int err=step();
    if(!lr&&!sub){powered=lr_powered=sub_powered=false;}
    if(!err&&(lr||sub)){powered=true;lr_powered=lr;sub_powered=sub;++power_ons;}
    return err;
}

static void wait_ms(void *ctx,unsigned ms){(void)ctx;now+=(uint64_t)ms*1000U;}
static uint64_t clock_us(void *ctx){(void)ctx;return now;}
static bool cancel(void *ctx){(void)ctx;return cancelled||(cancel_at&&now>=cancel_at);}
static esp_err_t begin(unsigned channels){assert(!last_pending&&(!lr_powered||regs[3]==0x0b)&&(!sub_powered||sub_regs[3]==0x0b));assert(channels==3);int err=step();if(!err)tone_on=true;return err;}
static esp_err_t pump(bool *done){++pumps;*done=finish;return step();}
static void end(void){++ends;tone_on=false;}
/* Check all four bytes, including fractional non-unity gains, after every route. */
static void expect_route(unsigned mask)
{
    const unsigned masks[]={4U,2U,1U};
    const uint32_t words[]={CHIME_DSP_GAIN_SUB_WORD,CHIME_DSP_GAIN_R_WORD,CHIME_DSP_GAIN_L_WORD};
    for(unsigned i=0;i<3;++i){
        uint32_t word=(mask&masks[i])?words[i]:0U;
        for(unsigned byte=0;byte<4;++byte)
            assert(route_data[i*4+byte]==(uint8_t)(word>>(24U-byte*8U)));
    }
}
static bench_sound_t init(void)
{
    play_at=cancel_at=fault_at=0;lock_delay_ms=state_delay_ms=0;stuck_hiz=false;play_reads=0;last_pending=false;
    external_bad_reg=-1;dsp_writes=0;corrupt_reg=-1;calls=fail_at=unmutes=pumps=ends=power_ons=0;powered=tone_on=cancelled=finish=bad_pll=bad_core=bad_asrc=false;now=0;
    lr_powered=sub_powered=corrupt_route=false;memset(route_data,0,sizeof(route_data));
    memset(sub_regs,0,sizeof(sub_regs));sub_regs[0x37]=9;sub_regs[0x38]=64;sub_regs[0x39]=8;sub_regs[0x50]=7;
    /* Seed stale PBTL mode: startup must explicitly restore BTL on either amp. */
    sub_regs[0x02]=4;sub_regs[0x54]=0x1f; /* stale analog setting must be replaced */
    memset(regs,0,sizeof(regs));regs[0x02]=4;regs[0x54]=0x00;regs[0x37]=9;regs[0x38]=64;regs[0x39]=8;regs[0x50]=7;
    bench_sound_t s;bench_sound_init(&s,(bench_sound_io_t){.read=rd,.write=wr,.power=power,.wait_ms=wait_ms,
        .now_us=clock_us,.cancelled=cancel,.tone_begin=begin,.tone_pump=pump,.tone_end=end});return s;
}
/* Keep timing/fault tests below explicitly LR-only, independent of boot default. */
static bench_sound_t init_lr(void)
{
    bench_sound_t s=init();s.outputs=BENCH_OUTPUT_L|BENCH_OUTPUT_R;return s;
}
static void waveform(void)
{
    for(unsigned mask=1;mask<=3;++mask) {
        bench_tone_t tone;bench_tone_begin(&tone,mask);int32_t samples[256];size_t frames=0;int32_t peak=0;
        while(!bench_tone_finished(&tone)) {
            size_t n=bench_tone_render(&tone,samples,128);assert(n>0&&n<=128);
            for(size_t i=0;i<n;++i) {
                uint32_t f=(uint32_t)(frames+i);
                for(unsigned ch=0;ch<2;++ch) {
                    int32_t v=samples[i*2+ch];assert(((uint32_t)v&255U)==0);
                    if(!(mask&(1U<<ch))||f>=BENCH_TONE_FRAMES||f==0||f==BENCH_TONE_FRAMES-1)assert(v==0);
                    if(v>peak)peak=v;
                }
                if(mask==3)assert(samples[i*2]==samples[i*2+1]);
            }
            frames+=n;
        }
        assert(frames==144512&&peak>270350000&&peak<270353000);
        assert(bench_tone_render(&tone,samples,128)==0);
    }
    bench_tone_t t;bench_tone_begin(&t,3);t.frame=960;int32_t full[96];
    assert(bench_tone_render(&t,full,48)==48);int64_t sum=0;
    for(unsigned i=0;i<48;++i){sum+=full[i*2];assert(full[i*2]==-full[((i+24)%48)*2]);}
    assert(sum==0&&full[0]==0&&full[24]>0&&full[48]==0&&full[72]<0);
    puts("PASS 1kHz/-18dBFS 24-in-32 PCM, all channel masks, exact 3s +512 zero-tail, ramps, period, symmetry and no DC");
}
static void calibration_waveform(void)
{
    const bench_tone_config_t configs[]={{40U,-60,1U},{137U,-48,2U},{1000U,-24,3U},{10000U,-18,1U},{1000U,-6,1U},{1000U,0,1U},{10000U,0,1U}};
    for(unsigned c=0;c<sizeof(configs)/sizeof(configs[0]);++c){
        bench_tone_t tone;bench_tone_config_t cfg=configs[c];
        assert(bench_tone_begin_config(&tone,2,cfg));
        unsigned command=(unsigned)bench_tone_command(2,cfg,63);
        assert(command<=2147483647U&&(command&3U)==2);
        bench_tone_config_t decoded=bench_tone_command_config((int)command);
        assert(decoded.frequency_hz==cfg.frequency_hz&&decoded.level_dbfs==cfg.level_dbfs&&decoded.seconds==cfg.seconds);
        size_t count=0,steady=0;unsigned crossings=0;double squares=0;int32_t previous=0,samples[254];
        while(!bench_tone_finished(&tone)){
            size_t n=bench_tone_render(&tone,samples,127);
            for(size_t i=0;i<n;++i){
                uint32_t f=(uint32_t)(count+i);int32_t value=samples[i*2+1];
                assert(samples[i*2]==0&&((uint32_t)value&255U)==0);
                assert((int64_t)value>=-2147483392LL&&(int64_t)value<=2147483392LL);
                if(f==0||f>=cfg.seconds*48000U-1U)assert(value==0);
                if(f>=480U&&f<cfg.seconds*48000U-480U){
                    double normalized=(double)value/2147483648.0;squares+=normalized*normalized;++steady;
                    if(value>0&&previous<=0)++crossings;
                    previous=value;
                }
            }
            count+=n;
        }
        double expected=pow(10.0,(double)cfg.level_dbfs/20.0)/sqrt(2.0);
        /* Includes finite-window error for low frequencies and table interpolation. */
        assert(fabs(sqrt(squares/(double)steady)/expected-1.0)<0.025);
        assert(fabs((double)crossings-(double)steady*cfg.frequency_hz/48000.0)<2.0);
        assert(count==cfg.seconds*48000U+512U);
    }
    bench_tone_t t={0};
    assert(!bench_tone_begin_config(&t,1,(bench_tone_config_t){39,-48,1}));
    assert(!bench_tone_begin_config(&t,1,(bench_tone_config_t){10001,-48,1}));
    assert(!bench_tone_begin_config(&t,1,(bench_tone_config_t){1000,1,1}));
    assert(!bench_tone_begin_config(&t,1,(bench_tone_config_t){1000,-61,1}));
    assert(!bench_tone_begin_config(&t,1,(bench_tone_config_t){1000,-48,4}));
    assert(!bench_tone_begin_config(&t,0,(bench_tone_config_t){1000,-48,1}));
    puts("PASS bounded calibration PCM: 40/137/1000/10000Hz, -60..0dBFS, 1/2/3s, RMS/frequency, channel isolation, 24bit, ramps/tail, packed command roundtrip and invalid bounds");
}
int main(void)
{
    waveform();calibration_waveform();bench_sound_t s=init();
    assert(BENCH_OUTPUT_DEFAULT==7U&&s.outputs==7U&&!s.active&&!powered&&!tone_on);
    assert(bench_sound_start_stream(&s,s.outputs)==ESP_OK&&lr_powered&&sub_powered&&unmutes==2);
    expect_route(7U);
    bench_sound_stop(&s,0);assert(!powered&&!tone_on);
    puts("PASS boot default L+R+Sub: no autoplay; explicit USB playback enables both amp banks and all compiled-gain branches");
    s=init_lr();
    assert(bench_sound_start_stream(&s,3)==0&&s.continuous&&s.active);
    now+=5000000ULL;bench_sound_poll(&s);assert(s.active&&powered);
    bad_asrc=true;now+=21000;bench_sound_poll(&s);assert(!s.active&&!powered&&s.failed_reg==0xf580);
    puts("PASS continuous USB mode retains runtime guards but not sine duration watchdog");
    s=init_lr();
    assert(!s.attempted&&s.result==0);
    bench_sound_cancel(&s,ESP_ERR_INVALID_STATE);bench_sound_cancel(&s,ESP_ERR_INVALID_STATE);
    assert(!s.attempted&&s.result==0&&!powered&&!tone_on);
    s=init_lr();assert(bench_sound_start(&s,1)==0&&s.active&&powered&&tone_on&&unmutes==1);
    assert(s.attempted&&s.hiz_clocks==0x094008&&s.play_clocks==0x094008&&s.power_state==3);
    startup_calls=calls;
    bench_sound_poll(&s);assert(pumps==1&&s.active);finish=true;
    bench_sound_poll(&s);assert(!s.active&&!powered&&!tone_on&&s.result==0&&s.completed);
    bench_sound_cancel(&s,ESP_ERR_INVALID_STATE);assert(!s.completed&&s.result==0);
    s=init_lr();assert(bench_sound_start(&s,1)==0);finish=true;
    /* Runtime guard reads consume 4 operations, then pump, then PDN-off. */
    fail_at=calls+6;bench_sound_poll(&s);assert(!s.active&&!s.completed&&s.result!=0);
    puts("PASS ROM1/ZROM 48k/24-bit BTL verified before unmute, LR-only digital volume 0dB/analog gain -8dB; both banks verified, clean completion disables PDN first");
    for(unsigned f=1;f<=startup_calls;++f) {
        s=init_lr();fail_at=f;assert(bench_sound_start(&s,2)==-99&&!s.active&&!powered&&!tone_on);
    }
    s=init_lr();lock_delay_ms=25;
    assert(bench_sound_start(&s,1)==0&&s.active&&unmutes==1&&play_reads==5);
    assert(s.hiz_clocks==0x094000&&s.play_clocks==0x094008&&now-play_at==25000);
    unsigned settling_calls=calls;
    bench_sound_stop(&s,0);assert(!s.completed);
    for(unsigned f=1;f<=settling_calls;++f) {
        s=init_lr();lock_delay_ms=25;fail_at=f;
        assert(bench_sound_start(&s,1)==-99&&!powered&&!tone_on);
    }
    s=init_lr();lock_delay_ms=-1;
    assert(bench_sound_start(&s,1)==ESP_ERR_TIMEOUT&&!powered&&!tone_on&&unmutes==0);
    assert(s.failed_reg==0x37&&s.observed==0x094000&&s.play_clocks==0x094000);
    assert(!strcmp(s.phase,"play-muted")&&now-play_at==105000&&play_reads==21);
    s=init_lr();lock_delay_ms=100;
    assert(bench_sound_start(&s,1)==0&&unmutes==1);bench_sound_stop(&s,0);assert(!s.completed);
    s=init_lr();lock_delay_ms=-1;cancel_at=50000;
    assert(bench_sound_start(&s,1)==ESP_ERR_INVALID_STATE&&!powered&&!tone_on&&unmutes==0&&now==50000);
    s=init_lr();lock_delay_ms=-1;fault_at=55000;
    assert(bench_sound_start(&s,1)==ESP_ERR_INVALID_STATE&&s.failed_reg==0x70&&!powered&&unmutes==0);
    for(unsigned bit=0;bit<6;++bit)if(bit!=3) {
        s=init_lr();regs[0x39]=(uint8_t)(1U<<bit);lock_delay_ms=-1;
        assert(bench_sound_start(&s,1)==ESP_ERR_INVALID_STATE&&play_reads==0&&!powered&&unmutes==0);
    }
    puts("PASS positive PLL lock: 09/40/08 passes; 00->08 startup settling; lost lock, timeout/cancel/fault/I2C errors fail closed, other five clock flags never ignored");
    s=init_lr();state_delay_ms=25;
    assert(bench_sound_start(&s,1)==ESP_OK&&unmutes==1&&now-play_at==25000&&s.power_state==3);
    bench_sound_stop(&s,0);assert(!s.completed);
    s=init_lr();stuck_hiz=true;
    assert(bench_sound_start(&s,1)==ESP_ERR_TIMEOUT&&s.failed_reg==0x68&&s.observed==2);
    assert(!powered&&!tone_on&&unmutes==0&&s.play_clocks==0x094008);
    s=init_lr();assert(bench_sound_start(&s,1)==ESP_OK);regs[0x68]=2;
    bench_sound_poll(&s);assert(!s.active&&!powered&&s.failed_reg==0x68&&pumps==0);
    s=init_lr();assert(bench_sound_start(&s,1)==ESP_OK);regs[0x68]=0xff;
    bench_sound_poll(&s);assert(!powered&&s.failed_reg==0x68&&s.observed==0xff);
    puts("PASS actual POWER_STATE must reach Play before PCM/unmute; stuck HiZ times out, non-Play runtime state stops");
    const unsigned checked[]={3,2,0x28,0x31,0x33,0x34,0x35,0x40,0x46,0x4c,0x50,0x54};
    for(size_t i=0;i<sizeof(checked)/sizeof(checked[0]);++i) {
        s=init_lr();corrupt_reg=(int)checked[i];
        assert(bench_sound_start(&s,1)==ESP_ERR_INVALID_STATE&&!powered&&!tone_on&&unmutes==0);
        assert(s.failed_reg==checked[i]);
    }
    s=init_lr();corrupt_reg=0x02;
    assert(bench_sound_start(&s,BENCH_OUTPUT_SUB)==ESP_ERR_INVALID_STATE);
    assert(s.failed_reg==0x02&&!powered&&!tone_on&&unmutes==0);
    puts("PASS every startup power/I2C/readback/tone failure shuts down, no blind retries");
    for(unsigned kind=0;kind<8;++kind) {
        s=init_lr();assert(bench_sound_start(&s,3)==0);
        if(kind==0)bad_pll=true;else if(kind==1)bad_core=true;else if(kind==2)bad_asrc=true;
        else if(kind==3)regs[0x37]=0;else if(kind==4)regs[0x38]=32;else if(kind==5)regs[0x39]=0;
        else if(kind==6)regs[0x70]=1;else regs[0x73]=1;
        bench_sound_poll(&s);assert(!s.active&&!powered&&!tone_on&&s.result!=0&&pumps==0);
    }
    for(unsigned bit=0;bit<6;++bit)if(bit!=3) {
        s=init_lr();assert(bench_sound_start(&s,1)==ESP_OK);regs[0x39]|=(uint8_t)(1U<<bit);
        bench_sound_poll(&s);assert(!powered&&!tone_on&&s.failed_reg==0x37&&pumps==0);
    }
    s=init_lr();regs[0x37]=0x19;assert(bench_sound_start(&s,1)==ESP_ERR_INVALID_STATE&&unmutes==0);
    for(unsigned kind=0;kind<4;++kind) {
        s=init_lr();assert(bench_sound_start(&s,1)==0);
        if(kind==0)cancelled=true;else if(kind==1)now=s.deadline;else if(kind==2)fail_at=calls+1;
        else {s.next_check=now+1;fail_at=calls+1;}
        bench_sound_poll(&s);assert(!s.active&&!powered&&!tone_on&&s.result!=0);
    }
    s=init_lr();cancelled=true;assert(bench_sound_start(&s,1)!=0&&power_ons==0);
    s=init_lr();assert(bench_sound_start(&s,0)!=0&&power_ons==0);
    s=init_lr();bad_asrc=true;assert(bench_sound_start(&s,1)!=0&&power_ons==0);
    puts("PASS missing clocks/ASRC/core, amp faults/warnings, cancellation, 4s watchdog and DMA/I2C errors stop; bad admission never powers amp");
    s=init_lr();lock_delay_ms=-1;
    assert(bench_sound_start(&s,1)==ESP_ERR_TIMEOUT&&s.attempted);
    bench_sound_cancel(&s,ESP_ERR_INVALID_STATE);bench_sound_cancel(&s,ESP_ERR_INVALID_STATE);
    assert(s.result==ESP_ERR_TIMEOUT&&s.failed_reg==0x37&&s.observed==0x094000);
    s=init_lr();assert(bench_sound_start(&s,1)==ESP_OK);bench_sound_cancel(&s,ESP_ERR_INVALID_STATE);
    assert(s.attempted&&!powered&&!tone_on&&!s.active&&s.result==ESP_ERR_INVALID_STATE);
    bench_sound_cancel(&s,ESP_ERR_TIMEOUT);assert(s.result==ESP_ERR_INVALID_STATE);
    s=init_lr();assert(bench_sound_start(&s,1)==ESP_OK);finish=true;bench_sound_poll(&s);
    bench_sound_cancel(&s,ESP_ERR_INVALID_STATE);assert(s.attempted&&s.result==0&&!powered);
    s=init_lr();fail_at=1;bench_sound_cancel(&s,ESP_ERR_INVALID_STATE);
    assert(s.result==-99&&!s.attempted&&!powered); /* do not hide real safe-off errors */
    puts("PASS idle/repeated teardown creates no test error; failed/completed history retained; active cancellation and safe-off failures reported");
    s=init();
    /* Switching/restarting must restore attenuated baseline, never 0x01000000. */
    const unsigned sequence[]={4,7,1,3,2,4,7};
    for(unsigned i=0;i<sizeof(sequence)/sizeof(sequence[0]);++i){
        unsigned mask=sequence[i];
        assert(bench_sound_route(&s,mask)==ESP_OK);expect_route(mask);
        assert(bench_sound_start_stream(&s,mask)==ESP_OK);expect_route(mask);
        bench_sound_stop(&s,0);assert(!powered&&!tone_on);
    }
    const unsigned modes[]={1,2,3,4,7};
    for(unsigned i=0;i<5;++i){
        unsigned mask=modes[i];s=init_lr();
        assert(bench_sound_route(&s,mask)==0&&!powered&&!tone_on&&s.outputs==mask);
        expect_route(mask);
        unsigned before_start=calls;assert(bench_sound_start_stream(&s,mask)==0&&s.active&&s.continuous);
        assert(lr_powered==((mask&3U)!=0)&&sub_powered==((mask&4U)!=0));
        assert(regs[0x02]==((mask&3U)?0:4));
        assert(sub_regs[0x02]==((mask&4U)?0:4));
        if(mask&4U)assert(sub_regs[0x35]==0x11); /* CH32 -> A, CH33 -> B */
        assert(unmutes==(mask==7?2U:1U));
        unsigned complete_calls=calls-before_start;bench_sound_stop(&s,0);assert(!powered&&!tone_on);
        for(unsigned f=1;f<=complete_calls;++f){
            s=init_lr();fail_at=f;assert(bench_sound_start_stream(&s,mask)!=0&&!powered&&!tone_on);
        }
    }
    s=init_lr();corrupt_route=true;assert(bench_sound_start(&s,7)!=0&&!powered&&power_ons==0&&s.failed_reg==CHIME_DSP_GAIN_SUB);
    for(unsigned i=0;i<2;++i){
        s=init_lr();assert(bench_sound_start_stream(&s,7)==0);
        if(i==0)sub_regs[0x70]=1;else sub_regs[0x39]=0;
        bench_sound_poll(&s);assert(!s.active&&!powered&&!tone_on&&s.amp_target==2);
    }
    for(unsigned mask=0;mask<=8;++mask)if(!bench_output_valid(mask)){
        s=init_lr();assert(bench_sound_start(&s,mask)!=0&&!powered&&power_ons==0);
    }
    s=init_lr();assert(bench_sound_start_stream(&s,7)==0);
    assert(s.amp_state[0]==3&&s.amp_state[1]==3&&s.amp_clocks[0]==0x094008&&s.amp_clocks[1]==0x094008);
    assert(s.amp_automute[0]==0&&s.amp_automute[1]==0);
    regs[0x69]=2;sub_regs[0x69]=1;
    unsigned diagnostic_before=calls;bench_sound_poll(&s);
    assert(s.active&&s.amp_automute[0]==2&&s.amp_automute[1]==1);
    assert(calls-diagnostic_before==10); /* 3 DSP + 3 per amp + pump, no extra read */
    sub_regs[0x69]=0;now=s.next_check;bench_sound_poll(&s);
    assert(s.active&&s.amp_automute[1]==0);bench_sound_stop(&s,0);
    assert(bench_sound_start_stream(&s,1)==0&&s.amp_state[1]==0xff&&s.amp_automute[1]==0xff&&s.amp_clocks[1]==0);
    puts("PASS read-only AMP FLOW state68/automute69: independent LR/SUB live cache, zero-data is reported not unmuted, no extra I2C transactions, new start invalidates unused amp cache");
    puts("PASS physical L/R/L+R/Sub/all routing: generated noncontiguous gain addresses only, exact compiled gains (not unity), dual BTL amps, SUB A/B use both I2S slots, stale PBTL cleared, selected PDNs only, every startup fault for all modes and runtime SUB fault stops BOTH amps");
    const unsigned banks[]={3,4,7};
    for(unsigned i=0;i<3;++i){
        unsigned mask=banks[i];s=init_lr();
        assert(bench_sound_check_external(&s,mask)==ESP_OK&&!powered&&!tone_on&&dsp_writes==0&&power_ons==0);
        assert(bench_sound_start_external(&s,mask)==ESP_OK&&s.active&&s.continuous&&dsp_writes==0);
        assert(lr_powered==((mask&3U)!=0)&&sub_powered==((mask&4U)!=0));
        unsigned external_calls=calls;bench_sound_stop(&s,0);
        s=init_lr();assert(bench_sound_start_external(&s,mask)==ESP_OK);unsigned start_calls=calls;bench_sound_stop(&s,0);
        assert(external_calls>start_calls);
        for(unsigned f=1;f<=start_calls;++f){s=init_lr();fail_at=f;assert(bench_sound_start_external(&s,mask)!=0&&!powered&&!tone_on&&dsp_writes==0);}
    }
    const unsigned external_config[]={0xf401,0xf100,0xf140,0xf200,0xf201,0xf188,0xf214,0xf215,0xf190,0xf218,0xf219};
    for(unsigned i=0;i<sizeof(external_config)/sizeof(external_config[0]);++i){
        s=init_lr();external_bad_reg=(int)external_config[i];
        assert(bench_sound_check_external(&s,7)==ESP_ERR_INVALID_STATE&&!powered&&power_ons==0&&dsp_writes==0);
        assert(bench_sound_start_external(&s,7)==ESP_ERR_INVALID_STATE&&!powered&&!tone_on&&power_ons==0&&dsp_writes==0);
    }
    s=init_lr();assert(bench_sound_start_external(&s,1)==ESP_ERR_INVALID_ARG&&!powered&&dsp_writes==0);
    s=init_lr();assert(bench_sound_start_external(&s,2)==ESP_ERR_INVALID_ARG&&!powered&&dsp_writes==0);
    s=init_lr();assert(bench_sound_start_external(&s,7)==ESP_OK);sub_regs[0x70]=1;bench_sound_poll(&s);
    assert(!s.active&&!powered&&!tone_on&&dsp_writes==0);
    puts("PASS external SS USB: no DSP writes, 48k/ASRC/I2S/mux mismatch before PDN, amp-only 3/4/7, all startup I/O errors and runtime SUB fault fail closed");
    return 0;
}

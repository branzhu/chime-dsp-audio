#include "bench_sound.h"
#include "generated/chime_dsp_parameters.h"
#include <string.h>

static esp_err_t read_value(bench_sound_t *s,unsigned target,uint16_t reg,uint8_t *data,size_t n)
{
    s->failed_reg=reg;s->observed=0;
    return s->io.read(s->io.context,target,reg,data,n);
}
static esp_err_t write_byte(bench_sound_t *s,uint8_t reg,uint8_t value)
{
    s->failed_reg=reg;s->observed=0;
    return s->io.write(s->io.context,s->amp_target,reg,&value,1);
}
static esp_err_t verify_byte(bench_sound_t *s,uint8_t reg,uint8_t value)
{
    esp_err_t err=write_byte(s,reg,value);uint8_t actual=0;
    if(err==ESP_OK)err=read_value(s,s->amp_target,reg,&actual,1);
    s->observed=actual;
    return err!=ESP_OK?err:actual==value?ESP_OK:ESP_ERR_INVALID_STATE;
}
static esp_err_t dsp_guard(bench_sound_t *s)
{
    uint8_t data[2];
    esp_err_t err=read_value(s,0,0xf004,data,2);
    if(err!=ESP_OK)return err;
    s->observed=((uint32_t)data[0]<<8)|data[1];
    if(!(data[1]&1U))return ESP_ERR_INVALID_STATE;
    err=read_value(s,0,0xf405,data,2);
    if(err!=ESP_OK)return err;
    s->observed=((uint32_t)data[0]<<8)|data[1];
    unsigned state=data[1]&7U;
    if(state!=1&&state!=3&&state!=4)return ESP_ERR_INVALID_STATE;
    err=read_value(s,0,0xf580,data,2);
    if(err!=ESP_OK)return err;
    s->observed=((uint32_t)data[0]<<8)|data[1];
    return (data[1]&1U)?ESP_OK:ESP_ERR_INVALID_STATE;
}
static esp_err_t amp_guard(bench_sound_t *s,bool allow_pll_pending,bool *pll_pending)
{
    uint8_t clocks[3],faults[4];
    if(pll_pending)*pll_pending=false;
    esp_err_t err=read_value(s,s->amp_target,0x37,clocks,sizeof(clocks));
    if(err!=ESP_OK)return err;
    /* Table 9-19/20/21: 48 kHz, 64 BCLK/frame, no clock detect errors. */
    s->observed=((uint32_t)clocks[0]<<16)|((uint32_t)clocks[1]<<8)|clocks[2];
    uint32_t raw=s->observed;
    s->amp_clocks[s->amp_target-1U]=raw;
    /* Bit 3 is positive PLL lock, unlike the other five error flags.
     * See docs/bench-sine.md: working TAS5825M readback is 09/40/08. */
    bool pending=(clocks[2]&0x08U)==0;
    /* Only the PLL indication may be pending during muted startup. All other
     * clock errors and every fault/warning remain fatal, including in HiZ. */
    if((clocks[0]&0x3fU)!=9U||clocks[1]!=64U||(clocks[2]&0x37U)||
       (!allow_pll_pending&&pending))return ESP_ERR_INVALID_STATE;
    err=read_value(s,s->amp_target,0x70,faults,sizeof(faults));
    if(err!=ESP_OK)return err;
    /* Treat any defined fault OR thermal warning as a stop; never clear/retry. */
    for(unsigned i=0;i<4;++i)if(faults[i]) {
        s->failed_reg=(uint16_t)(0x70U+i);s->observed=faults[i];return ESP_ERR_INVALID_STATE;
    }
    s->failed_reg=0x37;s->observed=raw;
    if(pll_pending)*pll_pending=pending;
    return ESP_OK;
}
static esp_err_t play_state_guard(bench_sound_t *s,bool allow_hiz,bool *pending)
{
    /* DEVICE_CTRL2 is only a request. Verify actual POWER_STATE (table 9-48)
     * before PCM/unmute, and during playback; do not bypass a stuck HiZ. */
    /* 68h/69h are consecutive read-only state registers (TI Tables 9-48/49).
     * Reading both in the existing transaction adds no extra I2C call and
     * retains zero-data auto-mute rather than defeating it for diagnosis. */
    uint8_t data[2]={0xff,0xff};
    esp_err_t err=read_value(s,s->amp_target,0x68,data,sizeof(data));
    if(err!=ESP_OK)return err;
    uint8_t state=data[0];
    s->amp_state[s->amp_target-1U]=state;
    s->amp_automute[s->amp_target-1U]=data[1];
    s->power_state=state;s->observed=state;
    if(state==3)return ESP_OK;
    if(allow_hiz&&state==2){*pending=true;return ESP_OK;}
    return ESP_ERR_INVALID_STATE;
}
static esp_err_t wait_play_clocks(bench_sound_t *s)
{
    /* Bounded startup settling only: no I2C retries, no fault clear, no PCM
     * and no unmute until the strict clock guard can pass. HiZ alone does not
     * prove PLL disable (datasheet says other blocks work normally in HiZ). */
    uint64_t deadline=s->io.now_us(s->io.context)+100000ULL;
    for(;;) {
        if(s->io.cancelled(s->io.context))return ESP_ERR_INVALID_STATE;
        esp_err_t err=dsp_guard(s);if(err!=ESP_OK)return err;
        bool pending=false;
        err=amp_guard(s,true,&pending);
        s->play_clocks=s->observed;
        if(err!=ESP_OK)return err;
        bool pll_pending=pending;
        err=play_state_guard(s,true,&pending);if(err!=ESP_OK)return err;
        if(pll_pending){s->failed_reg=0x37;s->observed=s->play_clocks;}
        if(!pending)return ESP_OK;
        if(s->io.now_us(s->io.context)>=deadline)return ESP_ERR_TIMEOUT;
        s->io.wait_ms(s->io.context,5);
    }
}
void bench_sound_init(bench_sound_t *s,bench_sound_io_t io)
{
    *s=(bench_sound_t){.io=io,.outputs=BENCH_OUTPUT_DEFAULT,.amp_target=1,.power_state=0xff,.amp_state={0xff,0xff},.amp_automute={0xff,0xff}};
}
void bench_sound_stop(bench_sound_t *s,int reason)
{
    s->completed=false;
    /* PDN is the primary immediate stop, even if I2C or the network is broken. */
    esp_err_t err=s->io.power(s->io.context,false,false);
    s->io.tone_end();s->active=false;
    s->result=reason!=ESP_OK?reason:err;
}
void bench_sound_cancel(bench_sound_t *s,int reason)
{
    s->completed=false;
    if(s->active){bench_sound_stop(s,reason);return;}
    /* Idle session cleanup must still enforce PDN low, but is not a failed
     * sound attempt and must not overwrite the last real test diagnostic. */
    esp_err_t err=s->io.power(s->io.context,false,false);
    s->io.tone_end();
    if(err!=ESP_OK&&s->result==ESP_OK){s->result=err;s->failed_reg=0;s->observed=0;}
}
/* Built-in crossover only. Gains are NOT contiguous; never bulk-write the
 * range between them, which contains crossover coefficients/state. */
esp_err_t bench_sound_route(bench_sound_t *s,unsigned outputs)
{
    if(s->active||!bench_output_valid(outputs))return ESP_ERR_INVALID_STATE;
    esp_err_t err=s->io.power(s->io.context,false,false);
    if(err==ESP_OK)err=dsp_guard(s);
    static const uint16_t addresses[]={CHIME_DSP_GAIN_SUB,CHIME_DSP_GAIN_R,CHIME_DSP_GAIN_L};
    static const unsigned masks[]={BENCH_OUTPUT_SUB,BENCH_OUTPUT_R,BENCH_OUTPUT_L};
    static const uint32_t words[]={CHIME_DSP_GAIN_SUB_WORD,CHIME_DSP_GAIN_R_WORD,CHIME_DSP_GAIN_L_WORD};
    for(unsigned i=0;err==ESP_OK&&i<3;++i){
        /* Restore the frozen project's gain, never turn attenuated branches up. */
        uint32_t word=(outputs&masks[i])?words[i]:0U;
        uint8_t data[4]={(uint8_t)(word>>24),(uint8_t)(word>>16),
                         (uint8_t)(word>>8),(uint8_t)word},actual[4]={0};
        if(s->io.cancelled(s->io.context)){err=ESP_ERR_INVALID_STATE;break;}
        s->failed_reg=addresses[i];
        err=s->io.write(s->io.context,0,addresses[i],data,sizeof(data));
        if(err==ESP_OK)err=read_value(s,0,addresses[i],actual,sizeof(actual));
        if(err==ESP_OK&&memcmp(data,actual,sizeof(data)))err=ESP_ERR_INVALID_STATE;
    }
    if(err==ESP_OK&&s->io.cancelled(s->io.context))err=ESP_ERR_INVALID_STATE;
    if(err!=ESP_OK){bench_sound_stop(s,err);return err;}
    s->outputs=outputs;s->failed_reg=0;s->observed=0;
    return ESP_OK;
}
static bool target_enabled(const bench_sound_t *s,unsigned target)
{
    return target==1?(s->outputs&3U)!=0:(s->outputs&BENCH_OUTPUT_SUB)!=0;
}
static esp_err_t prepare_amp(bench_sound_t *s)
{
    s->phase="hiz-config";
    esp_err_t err=write_byte(s,0x00,0);if(err!=ESP_OK)return err;
    err=write_byte(s,0x7f,0);if(err!=ESP_OK)return err;
    err=verify_byte(s,0x03,0x0a);if(err!=ESP_OK)return err;
    s->io.wait_ms(s->io.context,5);
    /* TI SLASEH7H Table 9-9: bit2=0 selects independent BTL channels.
     * SUB has TWO speakers, one on A+/A-, one on B+/B-; never PBTL.
     * DSP CH32/33 carry identical Sub audio into the two I2S slots;
     * SAP_CTRL3=0x11 below preserves left->A and right->B. */
    err=verify_byte(s,0x02,0x00);if(err!=ESP_OK)return err;
    static const uint8_t config[][2]={
        {0x28,0x00},{0x31,0x00},{0x33,0x02},{0x34,0x00},{0x35,0x11},
        {0x40,0x01}, /* ROM1, not a final PPC3 protection profile */
        {0x46,0x09},{0x4c,0x30}, /* 48k/ZROM, digital volume 0dB (TI Table 9-24) */
        {0x54,0x10}, /* AGAIN[4:0]=16 -> analog -8dB; reserved bits 0 (Table 9-30). */
    };
    for(size_t i=0;i<sizeof(config)/sizeof(config[0]);++i){
        err=verify_byte(s,config[i][0],config[i][1]);if(err!=ESP_OK)return err;
    }
    s->io.wait_ms(s->io.context,10);
    err=dsp_guard(s);if(err!=ESP_OK)return err;
    s->phase="hiz-clocks";
    err=amp_guard(s,true,0);s->hiz_clocks=s->observed;if(err!=ESP_OK)return err;
    err=verify_byte(s,0x50,0x07);if(err!=ESP_OK)return err;
    s->phase="play-muted";
    err=verify_byte(s,0x03,0x0b);if(err!=ESP_OK)return err;
    s->io.wait_ms(s->io.context,5);
    return wait_play_clocks(s);
}
static esp_err_t external_guard(bench_sound_t *s,unsigned outputs)
{
    /* Require the board audio contract, not a particular DM layout. */
    static const uint16_t config[][2]={
        {0xf401,2},{0xf100,1},{0xf140,5},{0xf200,0},{0xf201,2},
        {0xf188,2},{0xf214,0x9000},{0xf215,2},
        {0xf190,2},{0xf218,0x9000},{0xf219,2}
    };
    for(unsigned i=0;i<sizeof(config)/sizeof(config[0]);++i){
        if(i>=5&&i<8&&!(outputs&3U))continue;
        if(i>=8&&!(outputs&4U))continue;
        uint8_t data[2];esp_err_t err=read_value(s,0,config[i][0],data,2);
        if(err!=ESP_OK)return err;
        s->observed=((uint32_t)data[0]<<8)|data[1];
        if(s->observed!=config[i][1])return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
esp_err_t bench_sound_check_external(bench_sound_t *s,unsigned outputs)
{
    if(s->active||(outputs!=3U&&outputs!=4U&&outputs!=7U))return ESP_ERR_INVALID_ARG;
    esp_err_t err=s->io.power(s->io.context,false,false);
    if(err==ESP_OK)err=dsp_guard(s);
    if(err==ESP_OK)err=external_guard(s,outputs);
    if(err==ESP_OK&&s->io.cancelled(s->io.context))err=ESP_ERR_INVALID_STATE;
    return err;
}
static esp_err_t start(bench_sound_t *s,unsigned outputs,bool continuous,bool external)
{
    if(s->active)return ESP_ERR_INVALID_STATE;
    s->continuous=continuous;s->completed=false;
    s->attempted=true;s->failed_reg=0;s->observed=0;s->result=ESP_OK;
    s->phase="precheck";s->hiz_clocks=0;s->play_clocks=0;s->power_state=0xff;
    for(unsigned i=0;i<2;++i){s->amp_state[i]=s->amp_automute[i]=0xff;s->amp_clocks[i]=0;}
    esp_err_t err=s->io.power(s->io.context,false,false);
    if(err!=ESP_OK)goto fail;
    s->io.wait_ms(s->io.context,10);
    if(!bench_output_valid(outputs)||s->io.cancelled(s->io.context)){err=ESP_ERR_INVALID_STATE;goto fail;}
    if(external){
        if(outputs!=3U&&outputs!=4U&&outputs!=7U){err=ESP_ERR_INVALID_ARG;goto fail;}
        err=dsp_guard(s);if(err==ESP_OK)err=external_guard(s,outputs);
        if(err==ESP_OK)s->outputs=outputs;
    }else err=bench_sound_route(s,outputs);
    if(err!=ESP_OK)goto fail;
    err=s->io.power(s->io.context,(outputs&3U)!=0,(outputs&4U)!=0);if(err!=ESP_OK)goto fail;
    s->io.wait_ms(s->io.context,10);
    for(unsigned target=1;target<=2;++target)if(target_enabled(s,target)){
        s->amp_target=target;err=prepare_amp(s);if(err!=ESP_OK)goto fail;
    }
    if(s->io.cancelled(s->io.context)){err=ESP_ERR_INVALID_STATE;goto fail;}
    s->phase="unmute";
    /* Preserve both input slots for the SUB (L+R)/2 mix. Output isolation is
     * at the DSP gains, never by removing an input needed by another branch. */
    err=s->io.tone_begin(3U);if(err!=ESP_OK)goto fail;
    for(unsigned target=1;target<=2;++target)if(target_enabled(s,target)){
        s->amp_target=target;
        err=verify_byte(s,0x03,0x03);if(err!=ESP_OK)goto fail;
    }
    s->phase="playing";s->active=true;s->failed_reg=0;s->observed=0;
    s->deadline=s->io.now_us(s->io.context)+4000000ULL;s->next_check=0;
    return ESP_OK;
fail:
    bench_sound_stop(s,err);return err;
}
void bench_sound_poll(bench_sound_t *s)
{
    if(!s->active)return;
    uint64_t now=s->io.now_us(s->io.context);
    if(s->io.cancelled(s->io.context)||(!s->continuous&&now>=s->deadline)){bench_sound_stop(s,ESP_ERR_TIMEOUT);return;}
    if(now>=s->next_check) {
        esp_err_t err=dsp_guard(s);
        for(unsigned target=1;err==ESP_OK&&target<=2;++target)if(target_enabled(s,target)){
            s->amp_target=target;err=amp_guard(s,false,0);
            if(err==ESP_OK)err=play_state_guard(s,false,0);
        }
        if(err!=ESP_OK){bench_sound_stop(s,err);return;}
        s->next_check=now+20000ULL;
    }
    s->failed_reg=0;s->observed=0;
    bool finished=false;
    esp_err_t err=s->io.tone_pump(&finished);
    if(err!=ESP_OK||finished){bench_sound_stop(s,err);s->completed=finished&&s->result==ESP_OK&&!s->continuous;}
}

esp_err_t bench_sound_start(bench_sound_t *s,unsigned channels){return start(s,channels,false,false);}
esp_err_t bench_sound_start_stream(bench_sound_t *s,unsigned outputs){return start(s,outputs,true,false);}

esp_err_t bench_sound_start_external(bench_sound_t *s,unsigned outputs){return start(s,outputs,true,true);}

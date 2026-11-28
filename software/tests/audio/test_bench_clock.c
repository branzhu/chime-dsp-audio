#include "bench_audio.h"
#include "chime_clock_follow.h"
#include "driver/i2s_std.h"
#include "apll_test_hw.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
uint32_t apll_regs[3];apll_hp_t HP_SYS_CLKRST;apll_lp_t LP_CLKRST;apll_i2s_t I2S0;
void chime_usb_test_log(const char *tag,const char *fmt,...){(void)tag;(void)fmt;}
static unsigned refs,writes,deleted;static bool live,fail_disable;
void apll_test_write(unsigned addr,uint32_t value){assert(refs==1&&live&&addr==0);assert((value&0xffc00000U)==0xa5800000U);apll_regs[addr]=value;++writes;}
void esp_clk_tree_apll_acquire(void){assert(!refs);++refs;}
void esp_clk_tree_apll_release(void){assert(refs==1&&(apll_regs[0]&0x3fffffU)==382101);--refs;}
int esp_clk_xtal_freq(void){return 40000000;}
const char *esp_err_to_name(esp_err_t e){(void)e;return "test";}
esp_err_t i2s_new_channel(const i2s_chan_config_t *c,i2s_chan_handle_t *h,i2s_chan_handle_t *rx){assert(!live&&!rx&&c->id==0);*h=&live;return 0;}
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t h,const i2s_std_config_t *c){assert(h==&live&&c->clk_cfg.mclk_multiple==256);return 0;}
esp_err_t i2s_channel_get_info(i2s_chan_handle_t h,i2s_chan_info_t *i){assert(h==&live);memset(i,0,sizeof(*i));return 0;}
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t h,const void *d,size_t n,size_t *loaded){assert(h==&live&&d);*loaded=n;return 0;}
esp_err_t i2s_channel_enable(i2s_chan_handle_t h){assert(h==&live);live=true;return 0;}
esp_err_t i2s_channel_disable(i2s_chan_handle_t h){assert(h==&live);if(fail_disable)return ESP_ERR_INVALID_STATE;live=false;return 0;}
esp_err_t i2s_del_channel(i2s_chan_handle_t h){assert(h==&live&&!live&&!refs);++deleted;return 0;}
esp_err_t i2s_channel_write(i2s_chan_handle_t h,const void *d,size_t n,size_t *w,uint32_t t){assert(h==&live&&d&&t);*w=n;return 0;}
static void setup(unsigned odiv,unsigned bdiv)
{
    memset(&HP_SYS_CLKRST,0,sizeof(HP_SYS_CLKRST));memset(&I2S0,0,sizeof(I2S0));
    HP_SYS_CLKRST.i2s0_tx_ctrl0.reg_i2s0_tx_div_n=2;
    HP_SYS_CLKRST.i2s0_tx_ctrl0.reg_i2s0_tx_clk_src_sel=1;
    LP_CLKRST.apll_div.apll_out_div=odiv;LP_CLKRST.apll_div.val=odiv*8;
    I2S0.tx_conf.tx_bck_div_num=bdiv-1;
    apll_regs[0]=0xa5800000U|382101;apll_regs[1]=123;apll_regs[2]=456;
}
int main(void)
{
    assert(!bench_audio_clock_available()&&bench_audio_clock_reset()==0);
    /* Both equivalent total /8192 plans; normal production is odiv6, /2,/4. */
    for(unsigned plan=0;plan<2;++plan){
        setup(plan?2U:6U,plan?8U:4U);assert(!bench_audio_start()&&refs==1&&bench_audio_clock_available());
        uint32_t nominal=bench_audio_clock_rate_q16();
        assert(!bench_audio_clock_set_ppm(300)&&(apll_regs[0]&0x3fffffU)==382294);
        assert(bench_audio_clock_rate_q16()>nominal);
        unsigned count=writes;assert(!bench_audio_clock_set_ppm(5000)&&writes==count);
        assert(!bench_audio_clock_set_ppm(-300)&&(apll_regs[0]&0x3fffffU)==381908);
        /* Manual tone cannot inherit BT trim; STOP restores before release. */
        assert(!bench_audio_tone_begin(1)&&(apll_regs[0]&0x3fffffU)==382101);
        assert(bench_audio_clock_set_ppm(10)==ESP_ERR_INVALID_STATE);
        assert(bench_audio_clock_reset()==ESP_OK);
        bench_audio_tone_end();assert(!bench_audio_clock_set_ppm(300));
        assert(!bench_audio_stop()&&!refs&&!live&&bench_audio_clock_rate_q16()==CHIME_CLOCK_NOMINAL_Q16);
    }
    setup(6,4);assert(!bench_audio_start());assert(!bench_audio_clock_set_ppm(100));
    fail_disable=true;assert(bench_audio_stop()==ESP_ERR_INVALID_STATE&&refs==1&&live&&(apll_regs[0]&0x3fffffU)==382101);
    fail_disable=false;assert(!bench_audio_stop()&&!refs&&!live);
    setup(6,4);HP_SYS_CLKRST.i2s0_tx_div_ctrl0.reg_i2s0_tx_div_z=1;
    assert(!bench_audio_start()&&!refs&&!bench_audio_clock_available());assert(!bench_audio_stop());
    setup(6,4);assert(!bench_audio_start());assert(!bench_audio_clock_set_ppm(100));
    unsigned count=writes;apll_regs[1]^=1;
    assert(!bench_audio_clock_available()&&bench_audio_clock_set_ppm(200)==ESP_ERR_INVALID_STATE);
    assert(bench_audio_stop()==ESP_ERR_INVALID_STATE&&refs==1&&writes==count); /* fail closed, no foreign write */
    apll_regs[1]^=1;assert(!bench_audio_stop());assert(deleted==5);
    puts("PASS clock adapter: measured integer plans, masked SDM only, saturation/no dither, tone/STOP restore before lease release, unsupported fallback, foreign-owner fail closed");return 0;
}

#include "bench_audio.h"
#include <inttypes.h>
#include "board_support.h"
#include "bench_tone.h"
#include "driver/i2s_std.h"
#include "esp_log.h"
#include "chime_clock_follow.h"
#ifdef ESP_PLATFORM
#include "sdkconfig.h"
#endif

#define DMA_DESCRIPTORS 4U
#define DMA_FRAMES 128U
#define FRAME_BYTES 8U
static const char *TAG="chime_audio";
static i2s_chan_handle_t tx;
static bool enabled;
static bool tone_active;
static bench_tone_t tone;
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
#include <stdatomic.h>
#include "soc/soc.h"
#include "soc/lp_clkrst_reg.h"
#include "soc/lp_clkrst_struct.h"
#include "soc/hp_sys_clkrst_reg.h"
#include "soc/hp_sys_clkrst_struct.h"
#include "hal/i2s_ll.h"
#include "esp_private/esp_clk_tree_common.h"
#include "esp_private/esp_clk.h"
#define SDM_MASK 0x003fffffU
static bool clock_lease;
static uint32_t nominal_sdm,current_sdm,saved_ctrl,saved_div,saved_apll_div,saved_bclk_div;
static atomic_uint clock_rate=CHIME_CLOCK_NOMINAL_Q16;
static bool clock_unchanged(void)
{
    return REG_READ(HP_SYS_CLKRST_I2S0_TX_CTRL0_REG)==saved_ctrl&&
        REG_READ(HP_SYS_CLKRST_I2S0_TX_DIV_CTRL0_REG)==saved_div&&
        LP_CLKRST.apll_div.val==saved_apll_div&&
        I2S0.tx_conf.tx_bck_div_num==saved_bclk_div&&
        !I2S0.tx_conf.tx_slave_mod&&!I2S0.hw_sync_conf.tx_hw_sync_en&&
        (REG_READ(LP_AONCLKRST_APLL_SDM_REG)&SDM_MASK)==current_sdm;
}
static void clock_attach(void)
{
    /* Production has exactly one APLL user. Pin I2S0 rather than assuming
     * I2S_NUM_AUTO returned port0. Only the measured integer /8192 plan is
     * admitted. Other plans leave the original FIR path available. */
    uint32_t m=HP_SYS_CLKRST.i2s0_tx_ctrl0.reg_i2s0_tx_div_n;
    uint32_t b=I2S0.tx_conf.tx_bck_div_num+1U;
    uint32_t total=(LP_CLKRST.apll_div.apll_out_div+2U)*2U*m*b*64U;
    uint32_t sdm=REG_READ(LP_AONCLKRST_APLL_SDM_REG)&SDM_MASK;
    if(esp_clk_xtal_freq()!=40000000||total!=8192U||
       HP_SYS_CLKRST.i2s0_tx_ctrl0.reg_i2s0_tx_clk_src_sel!=1||
       HP_SYS_CLKRST.i2s0_tx_div_ctrl0.reg_i2s0_tx_div_z!=0||
       I2S0.tx_conf.tx_slave_mod||I2S0.hw_sync_conf.tx_hw_sync_en||
       sdm<382099U||sdm>382103U)return;
    nominal_sdm=current_sdm=sdm;saved_apll_div=LP_CLKRST.apll_div.val;
    saved_ctrl=REG_READ(HP_SYS_CLKRST_I2S0_TX_CTRL0_REG);
    saved_div=REG_READ(HP_SYS_CLKRST_I2S0_TX_DIV_CTRL0_REG);
    saved_bclk_div=I2S0.tx_conf.tx_bck_div_num;
    /* Extra reference blocks SDK frequency retuning (including a second
     * I2S init's pre-acquire tune). It is not permission to share APLL:
     * same-frequency clients would also follow this SDM and are forbidden. */
    esp_clk_tree_apll_acquire();clock_lease=true;
    atomic_store(&clock_rate,chime_clock_rate_q16(sdm));
}
bool bench_audio_clock_available(void){return enabled&&!tone_active&&clock_lease&&clock_unchanged();}
esp_err_t bench_audio_clock_set_ppm(int ppm)
{
    if(!enabled||tone_active||!clock_lease||!clock_unchanged())return ESP_ERR_INVALID_STATE;
    uint32_t target=chime_clock_sdm_for_ppm(nominal_sdm,ppm);
    if(target!=current_sdm){
        uint32_t old=REG_READ(LP_AONCLKRST_APLL_SDM_REG);
        REG_WRITE(LP_AONCLKRST_APLL_SDM_REG,(old&~SDM_MASK)|target);
#ifdef ESP_PLATFORM
        __asm__ volatile("fence iorw, iorw" ::: "memory");
#else
        atomic_thread_fence(memory_order_seq_cst);
#endif
        current_sdm=target;
        if(!clock_unchanged())return ESP_ERR_INVALID_STATE;
        atomic_store(&clock_rate,chime_clock_rate_q16(target));
    }
    return ESP_OK;
}
esp_err_t bench_audio_clock_reset(void)
{
    if(!clock_lease)return ESP_OK;
    if(!clock_unchanged())return ESP_ERR_INVALID_STATE;
    /* Repeated idle STOP/render teardown during a manual tone is harmless:
     * tone admission already restored nominal, so no write is necessary. */
    return current_sdm==nominal_sdm?ESP_OK:bench_audio_clock_set_ppm(0);
}
uint32_t bench_audio_clock_rate_q16(void){return atomic_load(&clock_rate);}
#else
bool bench_audio_clock_available(void){return false;}
esp_err_t bench_audio_clock_set_ppm(int ppm){(void)ppm;return ESP_ERR_NOT_SUPPORTED;}
esp_err_t bench_audio_clock_reset(void){return ESP_OK;}
uint32_t bench_audio_clock_rate_q16(void){return CHIME_CLOCK_NOMINAL_Q16;}
#endif

/* Initialize every byte of every cyclic DMA descriptor BEFORE enable.
 * Default is zero-only cyclic DMA. During a manual test the bridge owner
 * pumps bounded tone chunks; auto-clear prevents stale nonzero replay. */
static const uint8_t silence[DMA_DESCRIPTORS*DMA_FRAMES*FRAME_BYTES]={0};

esp_err_t bench_audio_stop(void)
{
    tone_active=false;
    esp_err_t restore=bench_audio_clock_reset();
    if(restore!=ESP_OK)return restore; /* Foreign clock owner: do not overwrite. */
    if(tx==NULL) return ESP_OK;
    if(enabled) {
        esp_err_t err=i2s_channel_disable(tx);
        if(err!=ESP_OK) return err; /* Never delete a channel still running. */
        enabled=false;
    }
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    if(clock_lease){esp_clk_tree_apll_release();clock_lease=false;}
    atomic_store(&clock_rate,CHIME_CLOCK_NOMINAL_Q16);
#endif
    esp_err_t err=i2s_del_channel(tx);
    if(err==ESP_OK) tx=NULL;
    return err;
}

esp_err_t bench_audio_start(void)
{
    if(tx!=NULL) return enabled?ESP_OK:ESP_ERR_INVALID_STATE;
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    const int port=I2S_NUM_0;
#else
    const int port=I2S_NUM_AUTO;
#endif
    i2s_chan_config_t channel=I2S_CHANNEL_DEFAULT_CONFIG(port,I2S_ROLE_MASTER);
    channel.dma_desc_num=DMA_DESCRIPTORS;
    channel.dma_frame_num=DMA_FRAMES;
    channel.auto_clear_after_cb=true;
    channel.auto_clear_before_cb=false;
    esp_err_t err=i2s_new_channel(&channel,&tx,NULL);
    if(err!=ESP_OK) return err;
    const i2s_std_config_t standard={
        /* ESP32-S31 DEFAULT is 40MHz XTAL, not an audio-rate PLL.
         * Use APLL explicitly for the clock comparison; no XTAL fallback.
         * Internal MCLK only: the board's DSP MCLK source is unchanged. */
        .clk_cfg={.sample_rate_hz=48000,.clk_src=I2S_CLK_SRC_APLL,
            .mclk_multiple=I2S_MCLK_MULTIPLE_256},
        /* Standard I2S: left WS low, 1 BCLK MSB delay; non-inverted BCLK.
         * This matches saved DSP SERIAL_BYTE_0 negative polarity semantics.
         * 32-bit zero words also satisfy its 24 valid bits / 32-bit slot. */
        .slot_cfg=I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT,I2S_SLOT_MODE_STEREO),
        .gpio_cfg={.mclk=I2S_GPIO_UNUSED,.bclk=BOARD_GPIO_TDM_BCLK,
            .ws=BOARD_GPIO_TDM_FS,.dout=BOARD_GPIO_TDM_DOUT,.din=I2S_GPIO_UNUSED,
            .invert_flags={.mclk_inv=false,.bclk_inv=false,.ws_inv=false}},
    };
    err=i2s_channel_init_std_mode(tx,&standard);
    if(err!=ESP_OK) goto fail;
    i2s_chan_info_t info={0};
    err=i2s_channel_get_info(tx,&info);
    if(err!=ESP_OK) goto fail;
    size_t loaded=0;
    err=i2s_channel_preload_data(tx,silence,sizeof(silence),&loaded);
    if(err!=ESP_OK) goto fail;
    if(loaded!=sizeof(silence)) {err=ESP_ERR_INVALID_SIZE;goto fail;}
    err=i2s_channel_enable(tx);
    if(err!=ESP_OK) goto fail;
    enabled=true;
#if CONFIG_CHIME_LDAC_APLL_FOLLOW
    clock_attach();
    ESP_LOGI(TAG,"LDAC48 clock follow available=%u limit=300ppm; rate is SDM model, not measured",(unsigned)clock_lease);
#endif
    /* get_info.bclk_hz is the REQUESTED rate in this IDF, not actual readback.
     * Derive achieved estimates from its rounded internal MCLK and fixed /4,
     * /256 divisors. Still NOT a measurement at the DSP pins. */
    ESP_LOGI(TAG,"CLOCK apll-v1 src=%d sclk=%" PRIu32 "Hz internal_mclk=%" PRIu32
        "Hz bclk_requested=%" PRIu32 "Hz bclk_calc=%" PRIu32 "Hz fs_calc=%" PRIu32
        "Hz; driver-calculated, NOT measured",
        (int)info.clk_src,info.sclk_hz,info.mclk_hz,info.bclk_hz,info.mclk_hz/4U,info.mclk_hz/256U);
    ESP_LOGI(TAG,"SILENCE I2S master Fs=48000 2x32bit BCLK=3072000Hz; BCLK=IO10 WS=IO11 DATA=IO9; no MCLK output/RX; default silence, manual tone available; amps OFF; waveform NOT_TESTED");
    return ESP_OK;
fail:
    {
        esp_err_t cleanup=bench_audio_stop();
        if(cleanup!=ESP_OK) ESP_LOGE(TAG,"I2S cleanup failed: %s; reset required",esp_err_to_name(cleanup));
    }
    ESP_LOGE(TAG,"silence clock init failed: %s; do not proceed to audio",esp_err_to_name(err));
    return err;
}

/* Only the bridge owner calls these; never from the HTTP task. */
static bench_tone_config_t tone_config={1000U,-18,3U};
esp_err_t bench_audio_tone_configure(bench_tone_config_t config)
{
    if(tone_active||!bench_tone_config_valid(config))return ESP_ERR_INVALID_ARG;
    tone_config=config;return ESP_OK;
}
esp_err_t bench_audio_tone_begin(unsigned channels)
{
    if(!enabled||channels<1||channels>3)return ESP_ERR_INVALID_STATE;
    esp_err_t restore=bench_audio_clock_reset();if(restore!=ESP_OK)return restore;
    if(!bench_tone_begin_config(&tone,channels,tone_config))return ESP_ERR_INVALID_ARG;
    tone_active=true;
    return ESP_OK;
}
void bench_audio_tone_end(void){tone_active=false;}
esp_err_t bench_audio_tone_pump(bool *finished)
{
    if(!enabled||!tone_active||finished==NULL)return ESP_ERR_INVALID_STATE;
    int32_t samples[DMA_FRAMES*2U];
    size_t frames=bench_tone_render(&tone,samples,DMA_FRAMES),written=0;
    esp_err_t err=i2s_channel_write(tx,samples,frames*FRAME_BYTES,&written,20);
    if(err!=ESP_OK||written!=frames*FRAME_BYTES) {
        tone_active=false;return err==ESP_OK?ESP_ERR_INVALID_SIZE:err;
    }
    *finished=bench_tone_finished(&tone);
    if(*finished)tone_active=false;
    return ESP_OK;
}

esp_err_t bench_audio_pcm_write(const int32_t *stereo,size_t frames)
{
    if(!enabled||tone_active||!stereo||frames!=DMA_FRAMES)return ESP_ERR_INVALID_STATE;
    size_t written=0;esp_err_t err=i2s_channel_write(tx,stereo,frames*FRAME_BYTES,&written,20);
    return err!=ESP_OK?err:written==frames*FRAME_BYTES?ESP_OK:ESP_ERR_INVALID_SIZE;
}

/* Owner only, with amps OFF and the shared worker quiesced. Queue enough
 * zeros to retire every old nonzero DMA descriptor without a clock change. */
esp_err_t bench_audio_pcm_clear(void)
{
    const int32_t zeros[DMA_FRAMES*2U]={0};
    for(unsigned i=0;i<DMA_DESCRIPTORS;++i){
        esp_err_t err=bench_audio_pcm_write(zeros,DMA_FRAMES);if(err!=ESP_OK)return err;
    }
    return ESP_OK;
}

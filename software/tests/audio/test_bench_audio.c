#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bench_audio.h"
#include "driver/i2s_std.h"

static unsigned fail,stage,disables,deletes;
static bool short_load,live,initialized;
static char order[32];static size_t used;
static esp_err_t step(char op)
{
    assert(used<sizeof(order)-1);order[used++]=op;++stage;
    return stage==fail?-99:ESP_OK;
}
const char *esp_err_to_name(esp_err_t err) {(void)err;return "test";}
esp_err_t i2s_new_channel(const i2s_chan_config_t *c,i2s_chan_handle_t *tx,i2s_chan_handle_t *rx)
{
    assert(!live && rx==NULL && c->id==I2S_NUM_AUTO && c->role==I2S_ROLE_MASTER);
    assert(c->dma_desc_num==4 && c->dma_frame_num==128);
    assert(c->auto_clear_after_cb && !c->auto_clear_before_cb);
    esp_err_t err=step('N');if(err==0){*tx=&stage;live=true;}return err;
}
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t tx,const i2s_std_config_t *c)
{
    assert(tx==&stage && live && c->clk_cfg.sample_rate_hz==48000);
    assert(c->clk_cfg.clk_src==I2S_CLK_SRC_APLL && c->clk_cfg.mclk_multiple==256);
    assert(c->slot_cfg.data_bit_width==32 && c->slot_cfg.slot_mode==2 && c->slot_cfg.ws_width==32);
    assert(!c->slot_cfg.ws_pol && c->slot_cfg.bit_shift);
    assert(c->gpio_cfg.mclk==-1 && c->gpio_cfg.bclk==10 && c->gpio_cfg.ws==11 && c->gpio_cfg.dout==9 && c->gpio_cfg.din==-1);
    assert(!c->gpio_cfg.invert_flags.mclk_inv && !c->gpio_cfg.invert_flags.bclk_inv && !c->gpio_cfg.invert_flags.ws_inv);
    esp_err_t err=step('I');if(err==0)initialized=true;return err;
}
esp_err_t i2s_channel_get_info(i2s_chan_handle_t tx,i2s_chan_info_t *info)
{
    assert(tx==&stage && initialized && info);
    *info=(i2s_chan_info_t){.clk_src=I2S_CLK_SRC_APLL,.sclk_hz=24576000,
        .mclk_hz=12288000,.bclk_hz=3072000};
    return step('G');
}
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t tx,const void *data,size_t n,size_t *loaded)
{
    assert(tx==&stage && initialized && n==4096);
    for(size_t i=0;i<n;++i)assert(((const unsigned char *)data)[i]==0);
    *loaded=short_load?n-1:n;return step('P');
}
esp_err_t i2s_channel_enable(i2s_chan_handle_t tx) {assert(tx==&stage && initialized);return step('E');}
esp_err_t i2s_channel_disable(i2s_chan_handle_t tx) {assert(tx==&stage && live);++disables;return step('S');}
esp_err_t i2s_del_channel(i2s_chan_handle_t tx)
{
    assert(tx==&stage && live);++deletes;esp_err_t err=step('D');if(err==0){live=false;initialized=false;}return err;
}
esp_err_t i2s_channel_write(i2s_chan_handle_t tx,const void *data,size_t bytes,size_t *written,uint32_t timeout)
{
    assert(tx==&stage&&live&&data&&bytes==1024&&timeout==20);
    *written=short_load?bytes-1:bytes;return step('W');
}
static void reset(void)
{
    fail=0;stage=0;short_load=false;disables=0;deletes=0;used=0;memset(order,0,sizeof(order));
    assert(!live);
}
int main(void)
{
    reset();assert(bench_audio_stop()==0 && stage==0);
    assert(bench_audio_start()==0 && strcmp(order,"NIGPE")==0);
    assert(bench_audio_start()==0 && stage==5);
    int32_t usb_pcm[256]={0};
    assert(bench_audio_pcm_write(usb_pcm,128)==0);
    assert(bench_audio_pcm_write(usb_pcm,127)==ESP_ERR_INVALID_STATE);
    --stage;--used;order[used]=0; /* pcm write exercised; restore lifecycle trace */
    assert(bench_audio_stop()==0 && strcmp(order,"NIGPESD")==0 && !live);
    assert(bench_audio_stop()==0 && stage==7);
    puts("PASS explicit APLL/256fs, driver clock info, 48k/2x32 Philips I2S, pins9/10/11, no MCLK/RX/inversion; full zero preload before enable, idempotent lifecycle");
    for(unsigned f=1;f<=5;++f) {
        reset();fail=f;assert(bench_audio_start()==-99 && !live);
        assert(disables==0 && deletes==(f==1?0U:1U));
        fail=0;assert(bench_audio_start()==0);assert(bench_audio_stop()==0);
    }
    reset();short_load=true;
    assert(bench_audio_start()==ESP_ERR_INVALID_SIZE && !live && strcmp(order,"NIGPD")==0);
    puts("PASS each new/init/clock-info/preload/enable fault rolls back; short DMA preload never enables; retry after cleanup");
    reset();assert(bench_audio_start()==0);fail=6;
    assert(bench_audio_stop()==-99 && live && deletes==0);
    fail=0;assert(bench_audio_stop()==0 && !live);
    reset();assert(bench_audio_start()==0);fail=7;
    assert(bench_audio_stop()==-99 && live && disables==1 && deletes==1);
    assert(bench_audio_start()==ESP_ERR_INVALID_STATE);
    fail=0;assert(bench_audio_stop()==0 && !live && disables==1 && deletes==2);
    puts("PASS disable failure never deletes running DMA; delete failure retains ownership and blocks restart until cleanup");
    reset();assert(bench_audio_start()==0);
    assert(bench_audio_tone_begin(0)==ESP_ERR_INVALID_STATE);
    assert(bench_audio_tone_begin(4)==ESP_ERR_INVALID_STATE);
    assert(bench_audio_tone_configure((bench_tone_config_t){1000,1,3})==ESP_ERR_INVALID_ARG);
    assert(bench_audio_tone_configure((bench_tone_config_t){137,-48,2})==0);
    assert(bench_audio_tone_begin(1)==0);bool finished=true;
    assert(bench_audio_tone_configure((bench_tone_config_t){1000,-48,3})==ESP_ERR_INVALID_ARG);
    assert(bench_audio_pcm_write(usb_pcm,128)==ESP_ERR_INVALID_STATE);
    assert(bench_audio_tone_pump(&finished)==0&&!finished);
    short_load=true;assert(bench_audio_tone_pump(&finished)==ESP_ERR_INVALID_SIZE);
    assert(bench_audio_tone_pump(&finished)==ESP_ERR_INVALID_STATE);
    short_load=false;assert(bench_audio_tone_begin(2)==0);fail=stage+1;
    assert(bench_audio_tone_pump(&finished)==-99);fail=0;
    assert(bench_audio_tone_begin(3)==0);bench_audio_tone_end();
    assert(bench_audio_tone_pump(&finished)==ESP_ERR_INVALID_STATE);
    assert(bench_audio_stop()==0&&bench_audio_tone_begin(1)==ESP_ERR_INVALID_STATE);
    puts("PASS manual tone pump, invalid channels, short write/timeout abort, explicit end and disabled-channel guards");
    reset();assert(bench_audio_start()==0);assert(bench_audio_pcm_clear()==0);
    assert(strcmp(order,"NIGPEWWWW")==0); /* all four DMA descriptors retired */
    fail=stage+1;assert(bench_audio_pcm_clear()==-99);fail=0;assert(bench_audio_stop()==0);
    assert(bench_audio_pcm_clear()==ESP_ERR_INVALID_STATE);
    puts("PASS source-switch DMA silence: four bounded writes, write error and disabled channel fail closed");
    puts("All bench audio host groups passed; waveform/sound NOT measured on board.");return 0;
}

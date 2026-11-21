#pragma once
#include "esp_err.h"
typedef void *i2s_chan_handle_t;
#define I2S_NUM_AUTO -1
#define I2S_ROLE_MASTER 1
#define I2S_GPIO_UNUSED -1
#define I2S_DATA_BIT_WIDTH_32BIT 32
#define I2S_SLOT_MODE_STEREO 2
#define I2S_CLK_SRC_APLL 9
#define I2S_MCLK_MULTIPLE_256 256
typedef struct {
    int id,role;unsigned dma_desc_num,dma_frame_num;
    bool auto_clear_after_cb,auto_clear_before_cb;
} i2s_chan_config_t;
#define I2S_CHANNEL_DEFAULT_CONFIG(n,r) {.id=(n),.role=(r),.dma_desc_num=6,.dma_frame_num=240}
typedef struct {
    struct {unsigned sample_rate_hz;int clk_src;unsigned mclk_multiple;} clk_cfg;
    struct {unsigned data_bit_width,slot_mode,ws_width;bool ws_pol,bit_shift;} slot_cfg;
    struct {int mclk,bclk,ws,dout,din;struct {bool mclk_inv,bclk_inv,ws_inv;} invert_flags;} gpio_cfg;
} i2s_std_config_t;
#define I2S_STD_CLK_DEFAULT_CONFIG(rate) {.sample_rate_hz=(rate)}
#define I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(bits,mode) {.data_bit_width=(bits),.slot_mode=(mode),.ws_width=(bits),.ws_pol=false,.bit_shift=true}
typedef struct {int clk_src;uint32_t sclk_hz,mclk_hz,bclk_hz;} i2s_chan_info_t;
esp_err_t i2s_channel_get_info(i2s_chan_handle_t,i2s_chan_info_t *);
esp_err_t i2s_new_channel(const i2s_chan_config_t *,i2s_chan_handle_t *,i2s_chan_handle_t *);
esp_err_t i2s_channel_init_std_mode(i2s_chan_handle_t,const i2s_std_config_t *);
esp_err_t i2s_channel_preload_data(i2s_chan_handle_t,const void *,size_t,size_t *);
esp_err_t i2s_channel_enable(i2s_chan_handle_t);
esp_err_t i2s_channel_disable(i2s_chan_handle_t);
esp_err_t i2s_del_channel(i2s_chan_handle_t);

esp_err_t i2s_channel_write(i2s_chan_handle_t,const void *,size_t,size_t *,uint32_t);

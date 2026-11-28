#pragma once
#include <stdint.h>
#define I2S_NUM_0 0
extern uint32_t apll_regs[3];
#define LP_AONCLKRST_APLL_SDM_REG 0
#define HP_SYS_CLKRST_I2S0_TX_CTRL0_REG 1
#define HP_SYS_CLKRST_I2S0_TX_DIV_CTRL0_REG 2
#define REG_READ(addr) (apll_regs[(addr)])
void apll_test_write(unsigned addr,uint32_t value);
#define REG_WRITE(addr,value) apll_test_write((addr),(value))
typedef struct {struct {uint32_t reg_i2s0_tx_div_n,reg_i2s0_tx_clk_src_sel;} i2s0_tx_ctrl0;
 struct {uint32_t reg_i2s0_tx_div_z;} i2s0_tx_div_ctrl0;} apll_hp_t;
extern apll_hp_t HP_SYS_CLKRST;
typedef struct {struct {uint32_t apll_out_div,val;} apll_div;} apll_lp_t;
extern apll_lp_t LP_CLKRST;
typedef struct {struct {uint32_t tx_bck_div_num,tx_slave_mod;} tx_conf;
 struct {uint32_t tx_hw_sync_en;} hw_sync_conf;} apll_i2s_t;
extern apll_i2s_t I2S0;
void esp_clk_tree_apll_acquire(void);
void esp_clk_tree_apll_release(void);
int esp_clk_xtal_freq(void);

#include "bench_dsp_status.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static unsigned calls,fail_at,cancel_at;
static uint64_t now,cost;
static bool pll_locked;
static int rd(void *ctx,uint16_t address,uint8_t *data,size_t size,uint32_t timeout)
{
    (void)ctx;assert(size==2&&timeout>0&&timeout<=20);
    unsigned i=calls++;assert(i<BENCH_DSP_STATUS_COUNT);
    if(calls==fail_at)return -99;
    if(!pll_locked)assert(address>=0xf000&&address<=0xf004);
    if(i==0)assert(address==0xf004);
    uint16_t v=address==0xf004?(pll_locked?1U:0U):address==0xf580?0U:(uint16_t)(address^0x1234U);
    data[0]=(uint8_t)(v>>8);data[1]=(uint8_t)v;now+=cost;return ESP_OK;
}
static uint64_t clock_us(void *ctx){(void)ctx;return now;}
static bool cancelled(void *ctx){(void)ctx;return cancel_at&&calls>=cancel_at;}
static bench_dsp_status_io_t init(void)
{
    calls=fail_at=cancel_at=0;now=0;cost=1000;pll_locked=true;
    return (bench_dsp_status_io_t){.read=rd,.now_us=clock_us,.cancelled=cancelled};
}
int main(void)
{
    bench_dsp_status_t s;bench_dsp_status_io_t io=init();
    bench_dsp_status_collect(&s,io);
    assert(s.complete&&s.result==ESP_OK&&calls==BENCH_DSP_STATUS_COUNT&&s.captured_ms==BENCH_DSP_STATUS_COUNT);
    for(unsigned i=0;i<BENCH_DSP_STATUS_COUNT;++i){assert(s.registers[i].valid&&s.registers[i].error==0);}
    assert(s.registers[14].value==0); /* ASRC unlocked is real data, not a read failure. */
    assert(s.registers[5].value==(0xf020^0x1234));
    assert(bench_dsp_status_registers[17].address==0xf188&&s.registers[17].value==(0xf188^0x1234));
    assert(bench_dsp_status_registers[18].address==0xf190&&s.registers[18].value==(0xf190^0x1234));
    for(unsigned i=1;i<=BENCH_DSP_STATUS_COUNT;++i) {
        io=init();fail_at=i;bench_dsp_status_collect(&s,io);
        assert(s.result==-99&&!s.complete&&calls==i&&!s.registers[i-1].valid&&s.registers[i-1].error==-99);
        for(unsigned j=i;j<BENCH_DSP_STATUS_COUNT;++j)assert(!s.registers[j].valid&&s.registers[j].error==0);
    }
    io=init();pll_locked=false;bench_dsp_status_collect(&s,io);
    assert(!s.complete&&s.result==ESP_ERR_INVALID_STATE&&calls==5&&s.registers[0].valid);
    for(unsigned i=5;i<BENCH_DSP_STATUS_COUNT;++i)assert(!s.registers[i].valid);
    io=init();cancel_at=3;bench_dsp_status_collect(&s,io);
    assert(!s.complete&&s.result==ESP_ERR_INVALID_STATE&&calls==3&&!s.registers[3].valid);
    io=init();cost=20000;bench_dsp_status_collect(&s,io);
    assert(!s.complete&&s.result==ESP_ERR_TIMEOUT&&calls==10&&now==200000);
    io=init();bench_dsp_status_collect(&s,(bench_dsp_status_io_t){0});assert(s.result==ESP_ERR_INVALID_ARG&&calls==0);
    puts("PASS DSP snapshot: fixed 19 read-only registers (including LR/SUB output mux), BE16, PLL first/unlocked skips core, real ASRC zero preserved, every I2C failure no retry, cancellation and 200ms deadline");
    return 0;
}

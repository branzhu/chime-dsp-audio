#include "bench_dsp_status.h"

const bench_dsp_status_register_t bench_dsp_status_registers[BENCH_DSP_STATUS_COUNT]={
    {0xf004,"PLL_LOCK"},
    {0xf000,"PLL_CTRL0"},{0xf001,"PLL_CTRL1"},{0xf002,"PLL_CLK_SRC"},{0xf003,"PLL_ENABLE"},
    {0xf020,"CLK_GEN1_M"},{0xf021,"CLK_GEN1_N"},{0xf050,"POWER_ENABLE0"},
    {0xf100,"ASRC_INPUT0"},{0xf140,"ASRC_OUT_RATE0"},
    {0xf200,"SERIAL_BYTE_0_0"},{0xf201,"SERIAL_BYTE_0_1"},
    {0xf401,"START_PULSE"},{0xf405,"CORE_STATUS"},
    {0xf580,"ASRC_LOCK"},{0xf581,"ASRC_MUTE"},{0xf582,"ASRC0_RATIO"},
    {0xf188,"SOUT_SOURCE8_LR"},{0xf190,"SOUT_SOURCE16_SUB"},
};
void bench_dsp_status_collect(bench_dsp_status_t *out,bench_dsp_status_io_t io)
{
    if(out==NULL)return;
    *out=(bench_dsp_status_t){0};
    if(!io.read||!io.now_us||!io.cancelled){out->result=ESP_ERR_INVALID_ARG;return;}
    uint64_t deadline=io.now_us(io.context)+200000ULL;
    unsigned count=0;
    for(unsigned i=0;i<BENCH_DSP_STATUS_COUNT;++i) {
        /* The four PLL config registers are accessible even without lock. */
        if(i>4&&!(out->registers[0].value&1U))continue;
        if(io.cancelled(io.context)){out->result=ESP_ERR_INVALID_STATE;break;}
        uint64_t now=io.now_us(io.context);
        if(now>=deadline){out->result=ESP_ERR_TIMEOUT;break;}
        uint32_t timeout=(uint32_t)((deadline-now)/1000ULL);
        if(timeout==0){out->result=ESP_ERR_TIMEOUT;break;}
        if(timeout>20)timeout=20;
        uint8_t data[2]={0};
        int err=io.read(io.context,bench_dsp_status_registers[i].address,data,2,timeout);
        out->registers[i].error=err;
        if(err!=ESP_OK){out->result=err;break;}
        out->registers[i].value=(uint16_t)(((uint16_t)data[0]<<8)|data[1]);
        out->registers[i].valid=true;++count;
        if(io.now_us(io.context)>=deadline){out->result=ESP_ERR_TIMEOUT;break;}
    }
    out->complete=count==BENCH_DSP_STATUS_COUNT&&out->result==ESP_OK;
    if(!out->complete&&out->result==ESP_OK)out->result=ESP_ERR_INVALID_STATE;
    out->captured_ms=(uint32_t)(io.now_us(io.context)/1000ULL);
}

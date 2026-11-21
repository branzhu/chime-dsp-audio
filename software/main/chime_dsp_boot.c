#include "chime_dsp_boot.h"
#include <string.h>
#include "generated/chime_dsp_image.inc"
static int reg_write(chime_dsp_boot_io_t *io,uint16_t reg,uint16_t value,uint16_t *failed)
{
    uint8_t b[2]={(uint8_t)(value>>8),(uint8_t)value};*failed=reg;
    return io->write(io->context,reg,b,2,20);
}
static int reg_read(chime_dsp_boot_io_t *io,uint16_t reg,uint16_t *v,uint16_t *failed)
{
    uint8_t b[2];*failed=reg;int err=io->read(io->context,reg,b,2,20);
    if(!err)*v=(uint16_t)(((uint16_t)b[0]<<8)|b[1]);
    return err;
}
static int segment(chime_dsp_boot_io_t *io,uint16_t address,const uint8_t *data,size_t size,uint16_t *failed)
{
    /* Verify every byte before core start, never read PM while running. */
    for(size_t offset=0;offset<size;offset+=128) {
        uint8_t actual[128];size_t n=size-offset;if(n>128)n=128;
        uint16_t reg=(uint16_t)(address+offset/4);*failed=reg;
        int err=io->write(io->context,reg,data+offset,n,20);if(err)return err;
        err=io->read(io->context,reg,actual,n,20);if(err)return err;
        if(memcmp(actual,data+offset,n))return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}
int chime_dsp_boot(chime_dsp_boot_io_t io,uint16_t *failed)
{
    if(!failed||!io.read||!io.write||!io.wait_ms||!io.now_us)return ESP_ERR_INVALID_ARG;
    *failed=0;int err;uint16_t v=0;
    /* ADI Rev.D Table 18; hardware reset precedes this fixed host-load path.
     * MCLK_OUT disabled: the board has its own DSP reference oscillator. */
    static const uint16_t pll[][2]={{0xf003,0},{0xf000,0x60},{0xf001,2},{0xf002,1},{0xf003,1}};
    for(unsigned i=0;i<sizeof(pll)/sizeof(pll[0]);++i) {
        err=reg_write(&io,pll[i][0],pll[i][1],failed);if(err)return err;
    }
    uint64_t deadline=io.now_us(io.context)+100000;
    do {
        err=reg_read(&io,0xf004,&v,failed);if(err)return err;
        if(v&1)break;
        io.wait_ms(io.context,2);
    }while(io.now_us(io.context)<deadline);
    if(!(v&1))return ESP_ERR_TIMEOUT;
    err=reg_write(&io,0xf400,1,failed);if(err)return err;
    io.wait_ms(io.context,1);
    err=reg_write(&io,0xf403,1,failed);if(err)return err;
    err=reg_read(&io,0xf405,&v,failed);if(err)return err;
    if(v&7)return ESP_ERR_INVALID_STATE;
    /* Board-bound compiler image: 48k, ASRC0 serial0/1 -> core, SIN0 slave, SOUT1/2
     * master 24-in-32 Philips. Rev.D Table 88: SOUT_SOURCE resets to 0
     * (disabled); 2 routes the corresponding DSP core pair to the pin.
     * F188: CH16/17 -> SDATA_OUT1 (L/R); F190: CH32/33 -> SDATA_OUT2
     * (SUB signal path only; amplifier remains OFF). Other pairs unused.
     * No GPIO/multipurpose, master-control-port or EEPROM configuration. */
    static const uint16_t config[][2]={
        {0xf005,0},{0xf020,6},{0xf021,1},{0xf050,0x1d71},{0xf051,0},
        {0xf100,1},{0xf140,5},{0xf188,2},{0xf190,2},
        {0xf200,0},{0xf201,2},
        {0xf210,0},{0xf211,2},{0xf214,0x9000},{0xf215,2},
        {0xf218,0x9000},{0xf219,2},{0xf581,0x0700},
        {0xf404,0},{0xf401,2}
    };
    for(unsigned i=0;i<sizeof(config)/sizeof(config[0]);++i) {
        err=reg_write(&io,config[i][0],config[i][1],failed);if(err)return err;
        err=reg_read(&io,config[i][0],&v,failed);if(err)return err;
        if(v!=config[i][1])return ESP_ERR_INVALID_STATE;
    }
    err=segment(&io,0xc000,image_program,IMAGE_PROGRAM_BYTES,failed);if(err)return err;
    err=segment(&io,0x0000,image_dmem0,IMAGE_DMEM0_BYTES,failed);if(err)return err;
    err=segment(&io,0x6000,image_dmem1,IMAGE_DMEM1_BYTES,failed);if(err)return err;
    /* Compiler image contains all project gains and processing coefficients. Load/verify it verbatim; NO legacy overlay. */
    static const uint16_t start[][2]={{0xf403,0},{0xf402,0},{0xf402,1},{0xf400,0}};
    for(unsigned i=0;i<sizeof(start)/sizeof(start[0]);++i) {
        err=reg_write(&io,start[i][0],start[i][1],failed);if(err)return err;
    }
    deadline=io.now_us(io.context)+200000;
    do {
        err=reg_read(&io,0xf004,&v,failed);if(err)return err;
        if(!(v&1))return ESP_ERR_INVALID_STATE;
        err=reg_read(&io,0xf405,&v,failed);if(err)return err;
        bool running=(v&7)==1||(v&7)==3||(v&7)==4;
        err=reg_read(&io,0xf580,&v,failed);if(err)return err;
        if(running&&(v&1)){*failed=0;return ESP_OK;}
        io.wait_ms(io.context,5);
    }while(io.now_us(io.context)<deadline);
    return ESP_ERR_TIMEOUT;
}

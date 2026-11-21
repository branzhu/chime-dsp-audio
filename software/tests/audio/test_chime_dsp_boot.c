#include "chime_dsp_boot.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "../../main/generated/chime_dsp_image.inc"
#include "../../main/generated/chime_dsp_parameters.h"
static uint8_t regs[0x1000][2],dm0[sizeof(image_dmem0)],dm1[sizeof(image_dmem1)],pm[sizeof(image_program)];
static unsigned calls,fail_at,corrupt_at;static bool verification_reads[4096];static uint64_t now;static bool lock,asrc,started;static unsigned starts;
static unsigned route_writes,route_reads;static uint16_t ignore_write;
static uint8_t *memory(uint16_t addr,size_t n)
{
    if(addr>=0xf000){assert(n==2);return regs[addr-0xf000];}
    if(addr>=0xc000){assert((size_t)(addr-0xc000)*4+n<=sizeof(pm));return pm+(addr-0xc000)*4;}
    if(addr>=0x6000){assert((size_t)(addr-0x6000)*4+n<=sizeof(dm1));return dm1+(addr-0x6000)*4;}
    assert((size_t)addr*4+n<=sizeof(dm0));return dm0+addr*4;
}
static int rd(void *ctx,uint16_t addr,uint8_t *b,size_t n,uint32_t timeout)
{
    (void)ctx;assert(timeout==20);if(++calls==fail_at)return -99;
    if(addr==0xf004){b[0]=0;b[1]=lock?1:0;}
    else if(addr==0xf405){b[0]=0;b[1]=started?3:0;}
    else if(addr==0xf580){b[0]=0;b[1]=asrc?1:0;}
    else {memcpy(b,memory(addr,n),n);if(calls<4096)verification_reads[calls]=true;}
    if((addr==0xf188||addr==0xf190)&&b[0]==0&&b[1]==2)
        route_reads|=addr==0xf188?1U:2U;
    if(calls==corrupt_at)b[0]^=1;
    now+=100;return ESP_OK;
}
static int wr(void *ctx,uint16_t addr,const uint8_t *b,size_t n,uint32_t timeout)
{
    (void)ctx;assert(timeout==20);if(++calls==fail_at)return -99;
    if(addr==0xf188||addr==0xf190) {
        assert(!started&&n==2&&b[0]==0&&b[1]==2);
        route_writes|=addr==0xf188?1U:2U;
    }
    if(addr==0xf402&&b[1]==1){
        assert(memcmp(dm0,image_dmem0,sizeof(dm0))==0);
        assert(memcmp(dm1,image_dmem1,IMAGE_DMEM1_BYTES)==0);
        assert(memcmp(pm,image_program,sizeof(pm))==0);
        /* Rev.D Table 88: reset=disabled, DSP source=2. Both used pairs
         * must be configured AND read back before the core can start. */
        assert(route_writes==3&&route_reads==3);
        assert(regs[0x188][0]==0&&regs[0x188][1]==2);
        assert(regs[0x190][0]==0&&regs[0x190][1]==2);
        started=true;++starts;
    }
    if(addr<0xf000)assert(!started); /* every RAM operation must precede start */
    if(addr!=ignore_write)memcpy(memory(addr,n),b,n);
    now+=100;return ESP_OK;
}
static uint64_t clock_us(void *ctx){(void)ctx;return now;}
static void wait_ms(void *ctx,unsigned ms){(void)ctx;now+=ms*1000U;}
static chime_dsp_boot_io_t init(void)
{
    memset(regs,0,sizeof(regs));memset(dm0,0xff,sizeof(dm0));memset(dm1,0xff,sizeof(dm1));memset(pm,0xff,sizeof(pm));
    calls=fail_at=corrupt_at=starts=route_writes=route_reads=0;ignore_write=0xffff;now=0;lock=asrc=true;started=false;
    return (chime_dsp_boot_io_t){.read=rd,.write=wr,.now_us=clock_us,.wait_ms=wait_ms};
}
int main(void)
{
    uint16_t failed;chime_dsp_boot_io_t io=init();assert(chime_dsp_boot(io,&failed)==0&&failed==0&&starts==1);
    unsigned successful=calls;assert(successful<4096);assert(sizeof(pm)==IMAGE_PROGRAM_BYTES&&sizeof(dm0)==IMAGE_DMEM0_BYTES&&sizeof(dm1)==(IMAGE_DMEM1_BYTES?IMAGE_DMEM1_BYTES:1U));
    const unsigned gains[]={CHIME_DSP_GAIN_R,CHIME_DSP_GAIN_SUB,CHIME_DSP_GAIN_L};
    const uint32_t expected[]={CHIME_DSP_GAIN_R_WORD,CHIME_DSP_GAIN_SUB_WORD,CHIME_DSP_GAIN_L_WORD};
    for(unsigned i=0;i<3;++i){
        unsigned address=gains[i];
        unsigned word=((unsigned)memory((uint16_t)address,4)[0]<<24)|((unsigned)memory((uint16_t)address,4)[1]<<16)|((unsigned)memory((uint16_t)address,4)[2]<<8)|memory((uint16_t)address,4)[3];
        assert(word==expected[i]); /* Exact compiler 8.24 word, no unity overlay. */
    }
    assert(route_writes==3&&route_reads==3);
    assert(regs[0x214][0]==0x90&&regs[0x215][1]==2&&regs[0x50][0]==0x1d);
    for(unsigned i=1;i<=successful;++i){io=init();fail_at=i;assert(chime_dsp_boot(io,&failed)==-99&&calls==i);}
    for(unsigned i=1;i<=successful;++i)if(verification_reads[i]){io=init();corrupt_at=i;assert(chime_dsp_boot(io,&failed)==ESP_ERR_INVALID_STATE&&!started);}
    /* A device ACK alone is insufficient: a stuck/reset output selector
     * must fail readback with the exact address, without starting DSP. */
    io=init();ignore_write=0xf188;
    assert(chime_dsp_boot(io,&failed)==ESP_ERR_INVALID_STATE&&failed==0xf188&&!started);
    io=init();ignore_write=0xf190;
    assert(chime_dsp_boot(io,&failed)==ESP_ERR_INVALID_STATE&&failed==0xf190&&!started);
    io=init();ignore_write=0x0020;
    assert(chime_dsp_boot(io,&failed)==ESP_ERR_INVALID_STATE&&failed==0x0020&&!started);
    io=init();lock=false;assert(chime_dsp_boot(io,&failed)==ESP_ERR_TIMEOUT&&!started&&now<150000);
    io=init();asrc=false;assert(chime_dsp_boot(io,&failed)==ESP_ERR_TIMEOUT&&started&&now<300000);
    io=init();corrupt_at=11;assert(chime_dsp_boot(io,&failed)==ESP_ERR_INVALID_STATE&&!started);
    assert(chime_dsp_boot((chime_dsp_boot_io_t){0},&failed)==ESP_ERR_INVALID_ARG);
    puts("PASS frozen DSP: SOUT1/2 DSP-source write/readback before start, fixed RAM/register scope, verbatim compiler image with variable sizes, compiled project gains, no legacy overlay, every chunk verified before start, every I/O fault stops without retry, PLL/ASRC bounded timeout; no EEPROM");
}

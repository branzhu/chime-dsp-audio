#include "../../main/generated/chime_dsp_parameters.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "bench_dsp.h"
#include "sigma_tcp_bridge.h"

typedef struct {
    bench_dsp_t dsp;
    uint8_t mem[65536*4];
    uint16_t addresses[1024];
    bool writes[1024];
    size_t calls, fail_at, status_calls;
    uint16_t state;
    int status_error;
    uint64_t now;
    bool cancel;
    uint8_t sent[SIGMA_MAX_FRAME_BYTES];
    size_t sent_bytes;
} fixture_t;
static uint64_t now_us(void *p) {return ((fixture_t *)p)->now;}
static void wait_us(void *p,uint32_t duration) {((fixture_t *)p)->now+=duration;}
static bool cancelled(void *p) {return ((fixture_t *)p)->cancel;}
static int status(void *p,uint16_t *s) {fixture_t *f=p;++f->status_calls;*s=f->state;return f->status_error;}
static int io(fixture_t *f,uint16_t address,uint8_t *rd,const uint8_t *wr,size_t n,uint32_t timeout)
{
    assert(f->calls<1024 && n<=DSP_TRANSFER_CHUNK_BYTES && timeout>0 && timeout<=100);
    f->addresses[f->calls]=address;f->writes[f->calls]=wr!=NULL;++f->calls;
    if(f->fail_at==f->calls) return -99;
    size_t width=address>=0xf000 ? 2U:4U;
    for(size_t i=0;i<n;++i) {
        size_t offset=((size_t)address+i/width)*4+i%width;
        if(wr) f->mem[offset]=wr[i];else rd[i]=f->mem[offset];
    }
    return 0;
}
static int rd(void *p,uint16_t a,uint8_t *d,size_t n,uint32_t t) {return io(p,a,d,NULL,n,t);}
static int wr(void *p,uint16_t a,const uint8_t *d,size_t n,uint32_t t) {return io(p,a,NULL,d,n,t);}
static void reg(fixture_t *f,uint16_t a,uint16_t v) {f->mem[(size_t)a*4]=(uint8_t)(v>>8);f->mem[(size_t)a*4+1]=(uint8_t)v;}
static fixture_t *create(void)
{
    fixture_t *f=calloc(1,sizeof(*f));assert(f);
    const dsp_transfer_io_t hooks={f,rd,wr,now_us,wait_us,cancelled};
    assert(bench_dsp_init(&f->dsp,hooks,status));
    reg(f,0xf000,0x60);reg(f,0xf001,2);reg(f,0xf002,1);reg(f,0xf003,1);reg(f,0xf004,1);reg(f,0xf401,2);
    return f;
}
static int request(fixture_t *f,bool write,uint8_t flag,uint16_t a,uint32_t n,uint8_t *data)
{
    const sigma_request_t r={.control=write?SIGMA_CONTROL_WRITE:SIGMA_CONTROL_READ,
        .flag=flag,.chip_address=0x70,.address=a,.data_length=n,.data=data};
    return bench_dsp_execute(&f->dsp,&r,data);
}
static void test_raw(void)
{
    fixture_t *f=create();uint8_t data[512]={0};
    f->status_error=-98;
    assert(request(f,false,0,0xf000,10,data)==0 && f->status_calls==0 && f->calls==2);
    assert(data[1]==0x60 && data[9]==1);
    data[0]=0;data[1]=1;
    assert(request(f,true,0,0xf890,2,data)==0 && f->status_calls==0);
    assert(request(f,true,0,0xf214,4,data)==0);
    f->status_error=0;
    memset(data,0xab,sizeof(data));
    size_t before=f->calls;
    assert(request(f,true,0,0,512,data)==0 && f->calls>before && f->calls<=before+16);
    memset(data,0,sizeof(data));assert(request(f,false,0,0,512,data)==0);
    for(size_t i=0;i<sizeof(data);++i) assert(data[i]==0xab);
    free(f);puts("PASS bench PLL-off register access; split F000..F004 read; GUI soft-reset/serial; segmented DM0 erase/read");
}
static void test_policy(void)
{
    const struct {uint16_t a;uint32_t n;bool write;} bad[]={
        {0xf004,2,true},{0xf003,4,true},{0xf007,2,true},{0xe000,2,true},
        {0xffff,4,true},{0x1fff,8,true},{0x7fff,8,false},{0xdfff,8,true},
        {0xf890,4,true},{0xf20c,8,true},{0,65540,true},{0,3,true},{0,0,true}};
    uint8_t data[32]={0};
    for(size_t i=0;i<sizeof(bad)/sizeof(bad[0]);++i) {
        fixture_t *f=create();assert(request(f,bad[i].write,0,bad[i].a,bad[i].n,data)!=0);
        assert(f->calls==0 && f->status_calls==0);free(f);
    }
    fixture_t *f=create();f->state=1;
    assert(request(f,true,0,0xc000,4,data)!=0 && f->calls==0);
    assert(request(f,false,0,0xc000,4,data)!=0 && f->calls==0);
    assert(request(f,true,0,CHIME_DSP_GAIN_L,4,data)==0 && f->calls==1);
    size_t before=f->calls;assert(request(f,true,0,CHIME_DSP_GAIN_L,24,data)!=0 && f->calls==before);
    f->status_error=-77;assert(request(f,false,0,0,4,data)==-77 && f->calls==before);
    free(f);puts("PASS bench full-frame authorization: RO/gaps/master/overflow/width/size rejected before IO; running RAM denied");
}
static void test_safe(void)
{
    uint8_t data[24]={1,2,3,4};fixture_t *f=create();f->state=3;
    assert(request(f,true,1,CHIME_DSP_GAIN_L,4,data)==0);
    assert(f->calls==5 && f->addresses[0]==0xf000 && f->addresses[1]==0xf401);
    assert(f->addresses[2]==CHIME_DSP_SAFE_DATA && f->addresses[3]==CHIME_DSP_SAFE_ADDRESS && f->addresses[4]==CHIME_DSP_SAFE_COUNT);
    assert(f->mem[CHIME_DSP_SAFE_ADDRESS*4+3]==CHIME_DSP_GAIN_L && f->mem[CHIME_DSP_SAFE_COUNT*4+3]==1 && f->now>=25);
    assert(request(f,true,1,CHIME_DSP_GAIN_L,4,data)==0 && f->now>=50);free(f);
    const uint16_t badstates[]={0,2,5,6,7};
    for(size_t i=0;i<sizeof(badstates)/sizeof(badstates[0]);++i) {
        f=create();f->state=badstates[i];assert(request(f,true,1,CHIME_DSP_GAIN_L,4,data)!=0 && f->calls==0);free(f);
    }
    f=create();f->state=1;
    assert(request(f,true,1,0x14,4,data)!=0 && f->calls==0); /* old mixer is now framework */
    assert(request(f,true,1,0x16,12,data)!=0 && f->calls==0); /* old gains must never be written */
    assert(request(f,true,1,0x2e,4,data)!=0 && f->calls==0); /* crossover polarity is not scalar gain */
    assert(request(f,true,1,CHIME_DSP_GAIN_L,8,data)!=0 && f->calls==0);
    assert(request(f,true,1,CHIME_DSP_GAIN_L,24,data)!=0 && f->calls==0);
    reg(f,0xf401,3);assert(request(f,true,1,CHIME_DSP_GAIN_L,4,data)==-107 && f->calls==2);
    for(size_t i=0;i<f->calls;++i) assert(!f->writes[i]);
    free(f);
    f=create();f->state=1;reg(f,0xf001,3);
    assert(request(f,true,1,CHIME_DSP_GAIN_L,4,data)==-107 && f->calls==2);free(f);
    for(size_t fail=1;fail<=5;++fail) {
        f=create();f->state=1;f->fail_at=fail;
        assert(request(f,true,1,CHIME_DSP_GAIN_L,4,data)==DSP_TRANSFER_IO && f->calls==fail);
        assert(f->dsp.transfer.diagnostic.io_error==-99);free(f);
    }
    f=create();f->cancel=true;assert(request(f,true,0,0xf000,2,data)==DSP_TRANSFER_CANCELLED && f->calls==0);free(f);
    puts("PASS bench software Safeload: project targets/count/state/48k checks; ordered staging+25us; every IO fault no retry/cancel");
}

static int bridge_execute(void *p,const sigma_request_t *r,uint8_t *data)
{
    return bench_dsp_execute(&((fixture_t *)p)->dsp,r,data);
}
static int bridge_send(void *p,const uint8_t *data,size_t n,uint32_t timeout)
{
    fixture_t *f=p;assert(timeout>0 && f->sent_bytes+n<=sizeof(f->sent));
    memcpy(f->sent+f->sent_bytes,data,n);f->sent_bytes+=n;return (int)n;
}
static void test_zero_chip_wire(void)
{
    uint8_t storage[SIGMA_MAX_FRAME_BYTES];
    const sigma_protocol_profile_t profile={SIGMA_PROFILE_PLUS_3_4_0_LAB,0x70,0};
    const uint8_t read_zero[]={0x0a,0,0,0,0x0e,0,0,0,0,2,0xf0,0,0,0};
    const uint8_t expected[]={0x0b,0,0,0,0x10,0,0,0,0,2,0xf0,0,0,0,0,0x60};
    fixture_t *f=create();sigma_bridge_t bridge;
    sigma_bridge_io_t net={f,now_us,bridge_execute,bridge_send};
    assert(sigma_bridge_init(&bridge,profile,net,storage,sizeof(storage)));
    assert(sigma_bridge_receive(&bridge,read_zero,sizeof(read_zero)));
    assert(f->calls==1 && f->addresses[0]==0xf000 && !f->writes[0] && f->status_calls==0);
    assert(f->sent_bytes==sizeof(expected) && memcmp(f->sent,expected,sizeof(expected))==0);free(f);
    /* Real board policy, not synthetic Fxxx ranges: reserved F007 is denied. */
    uint8_t reserved[sizeof(read_zero)];memcpy(reserved,read_zero,sizeof(reserved));reserved[11]=7;
    f=create();net.context=f;
    assert(sigma_bridge_init(&bridge,profile,net,storage,sizeof(storage)));
    assert(!sigma_bridge_receive(&bridge,reserved,sizeof(reserved)));
    assert(bridge.diagnostic.operation_error==-105 && f->calls==0 && f->sent_bytes==0);free(f);

    const uint8_t reset_exit[]={9,0,0,0,0,0,16,0,0,0,0,2,0xf8,0x90,0,1};
    f=create();net.context=f;
    assert(sigma_bridge_init(&bridge,profile,net,storage,sizeof(storage)));
    assert(sigma_bridge_receive(&bridge,reset_exit,sizeof(reset_exit)));
    assert(f->calls==1 && f->addresses[0]==0xf890 && f->writes[0] && f->status_calls==0);
    assert(f->mem[0xf890U*4U]==0 && f->mem[0xf890U*4U+1U]==1 && f->sent_bytes==0);free(f);
    /* Chip00 control writes retain real RO/reserved/master access checks. */
    const uint16_t denied[]={0xf004,0xf007,0xf8f0};
    for(size_t i=0;i<sizeof(denied)/sizeof(denied[0]);++i) {
        uint8_t bad[sizeof(reset_exit)];memcpy(bad,reset_exit,sizeof(bad));
        bad[12]=(uint8_t)(denied[i]>>8);bad[13]=(uint8_t)denied[i];
        f=create();net.context=f;
        assert(sigma_bridge_init(&bridge,profile,net,storage,sizeof(storage)));
        assert(!sigma_bridge_receive(&bridge,bad,sizeof(bad)));
        assert(bridge.diagnostic.operation_error==-105 && f->calls==0 && f->sent_bytes==0);free(f);
    }
    /* Valid prefix F003 followed by read-only F004: authorize entire frame first. */
    const uint8_t cross[]={9,0,0,0,0,0,18,0,0,0,0,4,0xf0,3,0,1,0,1};
    f=create();net.context=f;
    assert(sigma_bridge_init(&bridge,profile,net,storage,sizeof(storage)));
    assert(!sigma_bridge_receive(&bridge,cross,sizeof(cross)) && f->calls==0);free(f);

    puts("PASS exact chip00 F000 wire frame -> real bench policy -> one read -> exact 0060 reply; reserved F007 denied before IO");
    puts("PASS chip00 F890=0001 exact write -> real bench policy; RO/reserved/master/cross-RO denied before IO");
}

/* Send the same operation to either wire ID through the actual decoder,
 * bridge, bench policy and transfer layer. Only response chip may differ. */
static void wire_be32(uint8_t *p,uint32_t v)
{
    p[0]=(uint8_t)(v>>24);p[1]=(uint8_t)(v>>16);p[2]=(uint8_t)(v>>8);p[3]=(uint8_t)v;
}
static fixture_t *wire_operation(uint8_t chip,bool write,uint8_t flag,uint16_t a,
                                 uint32_t n,uint16_t state,size_t fail_at,
                                 bool bad_clock,int expected_error)
{
    fixture_t *f=create();f->state=state;f->fail_at=fail_at;
    if(bad_clock) reg(f,0xf001,3);
    uint8_t *storage=calloc(1,SIGMA_MAX_FRAME_BYTES),*frame=calloc(1,SIGMA_MAX_FRAME_BYTES);
    assert(storage && frame && n<=SIGMA_MAX_PAYLOAD_BYTES);
    size_t length=SIGMA_HEADER_BYTES+(write?n:0U);
    frame[0]=write?SIGMA_CONTROL_WRITE:SIGMA_CONTROL_READ;
    wire_be32(frame+(write?3:1),(uint32_t)length);
    frame[write?7:5]=chip;wire_be32(frame+(write?8:6),n);
    frame[write?12:10]=(uint8_t)(a>>8);frame[write?13:11]=(uint8_t)a;
    if(write) {frame[1]=flag;for(size_t i=14;i<length;++i) frame[i]=(uint8_t)(i*17U);}
    const sigma_protocol_profile_t profile={SIGMA_PROFILE_PLUS_3_4_0_LAB,0x70,0};
    const sigma_bridge_io_t net={f,now_us,bridge_execute,bridge_send};sigma_bridge_t bridge;
    assert(sigma_bridge_init(&bridge,profile,net,storage,SIGMA_MAX_FRAME_BYTES));
    /* Fragmented chip/header/payload; policies are not run before completion. */
    assert(sigma_bridge_receive(&bridge,frame,6) && f->calls==0 && f->status_calls==0);
    assert(sigma_bridge_receive(&bridge,frame+6,7) && f->calls==0 && f->status_calls==0);
    bool ok=sigma_bridge_receive(&bridge,frame+13,length-13);
    assert(ok==(expected_error==0));
    assert(bridge.diagnostic.operation_error==expected_error);
    assert(bridge.diagnostic.chip_address==chip);
    if(ok) {
        assert(sigma_bridge_finish(&bridge));
        if(write) assert(f->sent_bytes==0); /* No fabricated write ACK. */
        else {
            assert(f->sent_bytes==SIGMA_HEADER_BYTES+n && f->sent[0]==0x0b && f->sent[5]==chip);
            assert(f->sent[10]==(uint8_t)(a>>8) && f->sent[11]==(uint8_t)a);
        }
    } else assert(f->sent_bytes==0 && bridge.diagnostic.error==SIGMA_BRIDGE_EXECUTION);
    free(frame);free(storage);return f;
}
static void assert_wire_alias(bool write,uint8_t flag,uint16_t a,uint32_t n,
                               uint16_t state,size_t fail_at,bool bad_clock,int error)
{
    fixture_t *zero=wire_operation(0,write,flag,a,n,state,fail_at,bad_clock,error);
    fixture_t *normal=wire_operation(0x70,write,flag,a,n,state,fail_at,bad_clock,error);
    assert(zero->calls==normal->calls && zero->status_calls==normal->status_calls);
    assert(zero->now==normal->now && zero->sent_bytes==normal->sent_bytes);
    assert(memcmp(zero->addresses,normal->addresses,sizeof(zero->addresses))==0);
    assert(memcmp(zero->writes,normal->writes,sizeof(zero->writes))==0);
    assert(memcmp(zero->mem,normal->mem,sizeof(zero->mem))==0);
    if(zero->sent_bytes) {
        normal->sent[5]=0;assert(memcmp(zero->sent,normal->sent,zero->sent_bytes)==0);
    }
    if(error==-105 || error==-108 || error==-102) assert(zero->calls==0);
    if(flag==1 && error==0) {
        assert(zero->calls==5 && zero->now>=25);
        assert(zero->addresses[2]==CHIME_DSP_SAFE_DATA && zero->addresses[3]==CHIME_DSP_SAFE_ADDRESS && zero->addresses[4]==CHIME_DSP_SAFE_COUNT);
    }
    free(zero);free(normal);
}
static void test_zero_chip_all_operations(void)
{
    /* Registers, DM0 (including DSP address 0000), DM1, program RAM: 00/70 parity. */
    const uint16_t targets[]={0xf000,0xf890,0,CHIME_DSP_GAIN_L,0x6000,0xc000};
    for(size_t i=0;i<sizeof(targets)/sizeof(targets[0]);++i) {
        uint32_t n=targets[i]>=0xf000?2U:20U;
        assert_wire_alias(true,0,targets[i],n,0,0,false,0);
        assert_wire_alias(false,0,targets[i],n,0,0,false,0);
    }
    assert_wire_alias(true,0,0,32768,0,0,false,0);
    assert_wire_alias(false,0,0,32768,0,0,false,0);
    for(uint16_t state=1;state<=4;++state) {
        if(state==2) continue;
        for(uint32_t words=1;words<=1;++words) assert_wire_alias(true,1,CHIME_DSP_GAIN_L,words*4,state,0,false,0);
        assert_wire_alias(true,1,CHIME_DSP_GAIN_R,4,state,0,false,0);
        assert_wire_alias(true,1,CHIME_DSP_GAIN_SUB,4,state,0,false,0);
        assert_wire_alias(true,1,CHIME_DSP_GAIN_L,4,state,0,false,0);
    }
    /* The alias cannot bypass any access, alignment, stop-state or clock gate. */
    const struct {uint16_t a;uint32_t n;bool write;} denied[]={
        {0xf004,2,true},{0xf007,2,false},{0xf8f0,2,true},{0xf003,4,true},
        {0xffff,4,false},{0,3,true},{0xf000,1,false},{0x1fff,8,true},
        {0x7fff,8,false},{0xdfff,8,true},{0xe000,4,true}};
    for(size_t i=0;i<sizeof(denied)/sizeof(denied[0]);++i)
        assert_wire_alias(denied[i].write,0,denied[i].a,denied[i].n,0,0,false,-105);
    const uint16_t ram[]={0,0x6000,0xc000};
    for(size_t i=0;i<sizeof(ram)/sizeof(ram[0]);++i) {
        assert_wire_alias(true,0,ram[i],4,1,0,false,-108);
        assert_wire_alias(false,0,ram[i],4,1,0,false,-108);
    }
    assert_wire_alias(true,1,CHIME_DSP_GAIN_L,4,0,0,false,-102);
    assert_wire_alias(true,1,CHIME_DSP_GAIN_L,4,2,0,false,-102);
    assert_wire_alias(true,1,CHIME_DSP_GAIN_L,8,1,0,false,-102);
    assert_wire_alias(true,1,CHIME_DSP_GAIN_L,4,1,0,true,-107);
    assert_wire_alias(true,0,0,512,0,1,false,DSP_TRANSFER_IO);
    assert_wire_alias(false,0,0xc000,512,0,1,false,DSP_TRANSFER_IO);
    for(size_t fail=1;fail<=5;++fail) assert_wire_alias(true,1,CHIME_DSP_GAIN_L,4,1,fail,false,DSP_TRANSFER_IO);
    puts("PASS chip00/70 parity through decoder + real bench executor: registers/DM0 including address 0000/DM1/PM/32KB frames/Safeload; same RO/gap/width/overflow/state/clock/IO failures");
}

static void test_external(void)
{
    fixture_t *f=create();
    const dsp_transfer_io_t hooks={f,rd,wr,now_us,wait_us,cancelled};
    assert(bench_dsp_init_external(&f->dsp,hooks,status));f->state=1;
    uint8_t data[16]={1,2,3,4};
    assert(request(f,true,0,0x120,16,data)==0&&f->calls==1);
    memset(data,0,16);assert(request(f,false,0,0x120,16,data)==0&&data[0]==1&&data[3]==4);
    size_t before=f->calls;
    assert(request(f,true,1,CHIME_DSP_GAIN_L,4,data)!=0&&f->calls==before);
    assert(request(f,true,1,0x120,4,data)!=0&&f->calls==before);
    assert(request(f,true,0,0x6000,4,data)!=0&&f->calls==before);
    assert(request(f,true,0,0xc000,4,data)!=0&&f->calls==before);
    assert(request(f,true,0,0x1fff,8,data)!=0&&f->calls==before);
    assert(request(f,true,0,0xf004,2,data)!=0&&f->calls==before);
    f->state=0;
    assert(request(f,true,0,0x6000,4,data)==0);
    assert(request(f,true,0,0xc000,4,data)==0);
    free(f);
    puts("PASS external SS policy: arbitrary DM0 direct parameters without fixed gain map; unknown safeload rejected before I/O; PM/DM1 require stopped core; RO/bounds still enforced; owner must stop amps before writes");
}

int main(void) {test_zero_chip_all_operations();test_zero_chip_wire();test_raw();test_policy();test_safe();test_external();puts("All 6 bench DSP test groups passed.");return 0;}

#include "bench_dsp.h"
#include "generated/chime_dsp_parameters.h"

/* Bench policy: conservative 8k-word banks and documented register addresses.
 * Only explicitly bound compiler scalar parameters are live-writable; NOT a signed manifest.
 * Never routes I2C to amps/EEPROM; downloaded DSP code itself remains trusted. */
static const dsp_memory_region_t regions[] = {
    CHIME_DSP_RAM_REGIONS,
#include "bench_registers.inc"
};

bool bench_dsp_init(bench_dsp_t *dsp, dsp_transfer_io_t io,
                    int (*status)(void *, uint16_t *))
{
    if (dsp == NULL || status == NULL) return false;
    *dsp = (bench_dsp_t){.status=status, .context=io.context};
    return dsp_transfer_init(&dsp->transfer,io,regions,sizeof(regions)/sizeof(regions[0]));
}

static const dsp_memory_region_t external_regions[] = {
    {0x0000,0x1fff,4,DSP_REGION_READ|DSP_REGION_WRITE},
    {0x6000,0x7fff,4,DSP_REGION_READ|DSP_REGION_WRITE|DSP_REGION_STOPPED},
    {0xc000,0xdfff,4,DSP_REGION_READ|DSP_REGION_WRITE|DSP_REGION_STOPPED},
#include "bench_registers.inc"
};
bool bench_dsp_init_external(bench_dsp_t *dsp,dsp_transfer_io_t io,int (*status)(void *,uint16_t *))
{
    if(!bench_dsp_init(dsp,io,status))return false;
    dsp->external=true;
    return dsp_transfer_init(&dsp->transfer,io,external_regions,sizeof(external_regions)/sizeof(external_regions[0]));
}
static const dsp_memory_region_t *region_at(const bench_dsp_t *dsp,uint32_t address)
{
    const dsp_memory_region_t *map=dsp->external?external_regions:regions;
    size_t count=dsp->external?sizeof(external_regions)/sizeof(external_regions[0]):sizeof(regions)/sizeof(regions[0]);
    for(size_t i=0;i<count;++i)
        if(address>=map[i].first&&address<=map[i].last)return map+i;
    return NULL;
}

static int safeload(bench_dsp_t *dsp, const sigma_request_t *r, uint16_t state)
{
    /* Explicit experimental flag=1 mapping to this project's software safeload.
     * Fail closed on other targets, paused/stopped cores or non-48k clocks. */
#if !CHIME_DSP_HAS_SAFELOAD
    (void)dsp;(void)r;(void)state;
    return -102;
#else
    unsigned words=r->data_length/4;
    const dsp_memory_region_t *target=region_at(dsp,r->address);
    if (r->data_length == 0 || r->data_length % 4 != 0 || words>5U ||
        target==NULL || !(target->permissions&DSP_REGION_SAFELOAD_TARGET) ||
        (uint32_t)r->address+words-1U>target->last ||
        (state & 7U) == 0 || (state & 7U) == 2 || (state & 7U) > 4) return -102;
    uint8_t clock[8], pulse[2];
    dsp_transfer_result_t result=dsp_transfer_read(&dsp->transfer,0xf000,clock,sizeof(clock));
    if(result!=DSP_TRANSFER_OK) return (int)result;
    result=dsp_transfer_read(&dsp->transfer,0xf401,pulse,sizeof(pulse));
    if(result!=DSP_TRANSFER_OK) return (int)result;
    const uint8_t expected[]={0,0x60,0,2,0,1,0,1};
    for(size_t i=0;i<sizeof(expected);++i) if(clock[i]!=expected[i]) return -107;
    if(pulse[0]!=0 || pulse[1]!=2) return -107;
    static const dsp_memory_region_t safe_regions[] = {CHIME_DSP_SAFE_REGIONS};
    dsp_transfer_t safe;
    if (!dsp_transfer_init(&safe,dsp->transfer.io,safe_regions,sizeof(safe_regions)/sizeof(safe_regions[0]))) return -103;
    safe.core_running=true;
    const dsp_safeload_map_t map={CHIME_DSP_SAFE_DATA,CHIME_DSP_SAFE_ADDRESS,CHIME_DSP_SAFE_COUNT};
    result=dsp_transfer_safeload(&safe,map,r->address,r->data,r->data_length/4);
    dsp->transfer.diagnostic=safe.diagnostic;
    return (int)result;
#endif
}

int bench_dsp_execute(bench_dsp_t *dsp, const sigma_request_t *r, uint8_t *read_data)
{
    if (dsp == NULL || r == NULL || !dsp->transfer.initialized) return -100;
    bool write = r->control == SIGMA_CONTROL_WRITE;
    if(dsp->external&&r->flag)return -104; /* external safeload map unknown */
    if ((!write && r->control != SIGMA_CONTROL_READ) || (write && r->data == NULL) ||
        (!write && read_data == NULL) || r->data_length==0 ||
        r->data_length>DSP_TRANSFER_MAX_BYTES || r->flag>1 || (!write && r->flag)) return -100;
    dsp->transfer.diagnostic=(dsp_transfer_diagnostic_t){.failed_address=r->address};
    /* Authorize the WHOLE request before IO, including cross-policy DM0 erases.
     * This avoids writing a valid prefix before discovering a forbidden tail. */
    size_t remaining=r->data_length;
    uint32_t address=r->address;
    bool requires_stop=false;
    while(remaining) {
        const dsp_memory_region_t *region=region_at(dsp,address);
        if(region==NULL || (write && !(region->permissions&DSP_REGION_WRITE)) ||
           (!write && !(region->permissions&DSP_REGION_READ)) ||
           remaining%region->word_bytes!=0) return -105;
        requires_stop|=(region->permissions&DSP_REGION_STOPPED)!=0;
        size_t count=((size_t)region->last-address+1)*region->word_bytes;
        if(count>remaining) count=remaining;
        remaining-=count;address+=(uint32_t)(count/region->word_bytes);
    }
    uint16_t state=0;
    /* Register control must work even while the PLL is disabled. Read CORE_STATUS
     * only for RAM access; no speculative F405 reads during GUI PLL startup. */
    if(r->address<0xe000) {
        int err=dsp->status(dsp->context,&state);
        if(err!=0) return err;
    }
    dsp->transfer.core_running=(state&7U)!=0;
    if(write && r->flag==1) return safeload(dsp,r,state);
    if(r->flag) return -104;
    if(requires_stop && dsp->transfer.core_running) return -108;
    remaining=r->data_length;address=r->address;
    size_t offset=0;
    while(remaining) {
        const dsp_memory_region_t *region=region_at(dsp,address);
        size_t count=((size_t)region->last-address+1)*region->word_bytes;
        if(count>remaining) count=remaining;
        dsp_transfer_result_t result=write
            ? dsp_transfer_write(&dsp->transfer,(uint16_t)address,r->data+offset,count)
            : dsp_transfer_read(&dsp->transfer,(uint16_t)address,read_data+offset,count);
        if(result!=DSP_TRANSFER_OK) return (int)result;
        address+=(uint32_t)(count/region->word_bytes);remaining-=count;offset+=count;
    }
    return 0;
}

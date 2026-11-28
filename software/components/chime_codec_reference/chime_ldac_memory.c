#include "sdkconfig.h"
#include "chime_ldac_memory.h"
#if CONFIG_CHIME_BT_VENDOR_CODECS
#include <stdatomic.h>
#include <stdbool.h>
#include <string.h>
/* Ordinary .bss on S31 is HP SRAM, not RTC/LP RAM. Keep all LDAC handles,
 * spectra and overlap state here even when the generic heap is fragmented.
 * 8 slots include the upstream per-struct OOM cleanup/fallback path. */
_Alignas(16) static unsigned char ldac_fast_storage[CHIME_LDAC_FAST_BYTES];
typedef struct { size_t offset, size; } slot_t;
static slot_t slots[8];
static atomic_uint used, peak, failures;
void *chime_ldac_fast_malloc(size_t bytes)
{
    if (!bytes || bytes > sizeof(ldac_fast_storage)) goto failed;
    size_t size=(bytes+15U)&~(size_t)15U, offset=0;
    unsigned vacant=8;
    for(unsigned i=0;i<8;++i)if(!slots[i].size){vacant=i;break;}
    if(vacant==8)goto failed;
    /* First fit among at most eight live allocations. Frees may occur out of
     * order (pool reinit while handles survive); do not use a bump allocator. */
    for(unsigned pass=0;pass<=8;++pass){
        if(offset>sizeof(ldac_fast_storage)-size)goto failed;
        bool overlap=false;
        for(unsigned i=0;i<8;++i)if(slots[i].size &&
            offset<slots[i].offset+slots[i].size && slots[i].offset<offset+size){
            offset=slots[i].offset+slots[i].size;overlap=true;break;
        }
        if(overlap)continue;
        slots[vacant]=(slot_t){offset,size};
        unsigned total=atomic_fetch_add(&used,(unsigned)size)+(unsigned)size;
        if(total>atomic_load(&peak))atomic_store(&peak,total);
        return ldac_fast_storage+offset;
    }
failed:
    atomic_fetch_add(&failures,1);return NULL;
}
void *chime_ldac_fast_calloc(size_t count,size_t bytes)
{
    if(!count || !bytes || count>SIZE_MAX/bytes){atomic_fetch_add(&failures,1);return NULL;}
    void *p=chime_ldac_fast_malloc(count*bytes);
    if(p)memset(p,0,count*bytes);
    return p;
}
void chime_ldac_fast_free(void *pointer)
{
    if(!pointer)return;
    for(unsigned i=0;i<8;++i)if(slots[i].size && ldac_fast_storage+slots[i].offset==pointer){
        atomic_fetch_sub(&used,(unsigned)slots[i].size);slots[i].size=0;return;
    }
    /* A foreign/double free must never reach the system heap. */
    atomic_fetch_add(&failures,1);
}
void chime_ldac_memory_snapshot(chime_ldac_memory_stats_t *out)
{
    if(!out)return;
    *out=(chime_ldac_memory_stats_t){sizeof(ldac_fast_storage),atomic_load(&used),
        atomic_load(&peak),atomic_load(&failures),(uintptr_t)ldac_fast_storage};
}
#endif

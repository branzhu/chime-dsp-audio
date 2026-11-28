#include "chime_ldac_memory.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    chime_ldac_memory_stats_t s;
    chime_ldac_memory_snapshot(&s);assert(s.reserved_bytes==16384 && !s.used_bytes);
    void *a[8];
    for(unsigned i=0;i<8;++i){a[i]=chime_ldac_fast_calloc(1,17);assert(a[i] && !((uintptr_t)a[i]&15));
        for(unsigned j=0;j<17;++j)assert(!((unsigned char *)a[i])[j]);}
    assert(!chime_ldac_fast_malloc(1));
    void *hole=a[3];chime_ldac_fast_free(hole);a[3]=chime_ldac_fast_malloc(17);assert(a[3]==hole);
    for(unsigned i=0;i<8;++i)chime_ldac_fast_free(a[7-i]);
    assert(!chime_ldac_fast_calloc(SIZE_MAX,2));assert(!chime_ldac_fast_malloc(SIZE_MAX));
    assert(!chime_ldac_fast_malloc(0));
    chime_ldac_fast_free(NULL);
    for(unsigned i=0;i<1000;++i){
        void *handle=chime_ldac_fast_malloc(128), *pool=chime_ldac_fast_calloc(1,12000);
        assert(handle && pool);memset(pool,0xff,12000);chime_ldac_fast_free(pool);
        pool=chime_ldac_fast_calloc(1,12000);assert(pool);
        for(unsigned j=0;j<12000;++j)assert(!((unsigned char *)pool)[j]);
        chime_ldac_fast_free(handle);chime_ldac_fast_free(pool);
    }
    void *all=chime_ldac_fast_malloc(16384);assert(all);assert(!chime_ldac_fast_malloc(1));
    chime_ldac_fast_free(all);
    chime_ldac_memory_snapshot(&s);assert(!s.used_bytes && s.peak_bytes==16384 && s.failures==5);
    puts("PASS LDAC SRAM allocator: bounds, alignment, overflow, slots, reuse, zeroing, lifecycle");
}

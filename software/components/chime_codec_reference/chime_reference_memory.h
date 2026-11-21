#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
/* The reference hooks require 8-byte alignment; IDF TLSF only guarantees 4.
 * Store the original pointer bytewise: even the metadata need not be pointer
 * aligned in allocator fault tests. This same path is tested on the host. */
static inline void *chime_reference_calloc8(size_t bytes) {
    const size_t extra=sizeof(void *)+7u;
    if(!bytes || bytes>SIZE_MAX-extra)return NULL;
    void *raw=calloc(1,bytes+extra);
    if(!raw)return NULL;
    uintptr_t aligned=((uintptr_t)raw+extra)&~(uintptr_t)7u;
    memcpy((void *)(aligned-sizeof(raw)),&raw,sizeof(raw));
    return (void *)aligned;
}
static inline void chime_reference_free8(void *bytes) {
    if(!bytes)return;
    void *raw;memcpy(&raw,(uint8_t *)bytes-sizeof(raw),sizeof(raw));free(raw);
}

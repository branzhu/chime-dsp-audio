#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CHIME_REPLAY_BATCH_MAX 8192U
#define CHIME_REPLAY_PACKET_MAX 2048U
#define CHIME_REPLAY_BATCH_PACKETS 8U
/* Wire: u32 LE first INPUT sample-frame index, then u16 LE bytes, u8 LDAC
 * frame count, raw LDAC frames. No A2DP header (added only at submission).
 * Pure validation: no allocation/queue/decoder side effects on malformed batch. */
static inline uint32_t chime_replay_u32(const uint8_t *p)
{return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);}
static inline uint64_t chime_replay_offset_us(uint32_t frames,unsigned rate,int ppm)
{
    return ((uint64_t)frames*1000000ULL/rate)*1000000ULL/(unsigned)(1000000+ppm);
}
static inline bool chime_replay_batch(const uint8_t *p,size_t n,uint32_t expected,
    uint32_t limit,unsigned *packets,uint32_t *frames)
{
    if(!p||!packets||!frames||n<10||n>CHIME_REPLAY_BATCH_MAX||chime_replay_u32(p)!=expected)return false;
    size_t off=4;unsigned count=0;uint32_t samples=0;
    while(off<n){
        if(n-off<3||++count>CHIME_REPLAY_BATCH_PACKETS)return false;
        unsigned bytes=(unsigned)p[off]|((unsigned)p[off+1]<<8),nf=p[off+2];off+=3;
        if(bytes<3||bytes+1>CHIME_REPLAY_PACKET_MAX||!nf||nf>15||bytes>n-off)return false;
        samples+=nf*128U;off+=bytes;
    }
    if(expected>limit||samples>limit-expected)return false;
    *packets=count;*frames=samples;return count>0;
}

#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define BENCH_TONE_FRAMES 144000U
#define BENCH_TONE_TAIL 512U
#define BENCH_TONE_RAMP 480U
typedef struct { unsigned frequency_hz; int level_dbfs; unsigned seconds; } bench_tone_config_t;
/* Peak dBFS at the ESP source, NOT a DSP limiter detector or SPL reading. */
static inline bool bench_tone_config_valid(bench_tone_config_t c)
{
    return c.frequency_hz>=40U&&c.frequency_hz<=10000U&&
        c.level_dbfs>=-60&&c.level_dbfs<=0&&c.seconds>=1U&&c.seconds<=3U;
}
/* Positive 31-bit atomic mailbox: channels[1:0], Hz[16:2], dB+60[22:17],
 * seconds-1[24:23], rolling request tag[30:25]. Legacy 1..3 keeps old defaults. */
static inline int bench_tone_command(unsigned channels,bench_tone_config_t c,unsigned tag)
{
    return (int)((channels&3U)|(c.frequency_hz<<2)|((unsigned)(c.level_dbfs+60)<<17)|
        ((c.seconds-1U)<<23)|((tag&63U)<<25));
}
static inline bench_tone_config_t bench_tone_command_config(int command)
{
    unsigned v=(unsigned)command;
    if(v<=3U)return (bench_tone_config_t){1000U,-18,3U};
    return (bench_tone_config_t){(v>>2)&32767U,(int)((v>>17)&63U)-60,((v>>23)&3U)+1U};
}
typedef struct {
    uint32_t frame,total_frames;
    unsigned channels,frequency_hz;
    uint32_t gain_q24;
} bench_tone_t;
/* 48 kHz, 24 valid MSBs in signed 32-bit I2S slots; 10 ms ramps + zero tail. */
void bench_tone_begin(bench_tone_t *tone,unsigned channels);
bool bench_tone_begin_config(bench_tone_t *tone,unsigned channels,bench_tone_config_t config);
size_t bench_tone_render(bench_tone_t *tone,int32_t *stereo,size_t frames);
bool bench_tone_finished(const bench_tone_t *tone);

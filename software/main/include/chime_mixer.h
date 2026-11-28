#pragma once
#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#define CHIME_MIX_FRAMES 128U
#define CHIME_GAIN_ONE 1073741824 /* Q2.30 unity */
#define CHIME_PCM_ONE 134217728 /* signed Q5.27; unity is 2^27 */
typedef struct {int32_t current,start,target;unsigned position;int32_t last[2];} chime_mix_lane_t;
typedef struct {
    chime_mix_lane_t lane[2],master;
    uint32_t clipped_samples,blocks;uint32_t peak;
} chime_mixer_t;
void chime_mixer_init(chime_mixer_t *m);
/* Gains use Q2.30 (2^30 unity). Input is Q5.27, output is PCM24 in 32-bit slots.
 * Unity summing: no fixed attenuation or automatic gain. Source volumes provide
 * operating headroom. Final saturation is only a last-resort boundary, not a limiter. */
void chime_mixer_process(chime_mixer_t *m,const int32_t *usb,const int32_t *bt,
    bool usb_ready,bool bt_ready,int32_t usb_gain,int32_t bt_gain,bool mute,
    int32_t *out,size_t frames);
void chime_pcm16_q27(const uint8_t *in,int32_t *out,size_t frames);

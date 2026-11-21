#pragma once
#include <stddef.h>
#include <stdint.h>
/* Worker-owned instance, 44.1/48 kHz stereo -> interleaved PCM16. */
void *chime_ldac_open(unsigned rate, unsigned channels);
void chime_ldac_close(void *decoder);
/* Full length incl. three-byte frame header; zero on truncated/bad format. */
size_t chime_ldac_frame_bytes(const uint8_t *data, size_t available, unsigned rate);
/* Exactly one complete frame, returns 512 PCM bytes or zero on error. */
size_t chime_ldac_decode(void *decoder, const uint8_t *data, size_t length,
                         uint8_t *pcm, size_t capacity);

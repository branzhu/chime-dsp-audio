#pragma once
#include <stddef.h>
#include <stdint.h>
void *chime_lhdc_open(unsigned rate,unsigned channels);
void chime_lhdc_close(void *handle);
/* Raw V5 [u16 LE length/flags][payload], not the A2DP two-byte payload header. */
size_t chime_lhdc_frame_bytes(const uint8_t *data,size_t available);
size_t chime_lhdc_decode(void *handle,const uint8_t *data,size_t length,uint8_t *pcm,size_t capacity);

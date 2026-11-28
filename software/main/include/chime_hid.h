#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef struct {int pending;bool release;} chime_hid_t;
void chime_hid_reset(chime_hid_t *s);
void chime_hid_adjust(chime_hid_t *s,int steps);
bool chime_hid_peek(const chime_hid_t *s,uint16_t *usage);
void chime_hid_sent(chime_hid_t *s);

#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { CHIME_SOURCE_NONE=0, CHIME_SOURCE_USB=1, CHIME_SOURCE_BLUETOOTH=2, CHIME_SOURCE_MIX=3 } chime_source_id_t;
typedef struct {
    chime_source_id_t selected;
    uint32_t generation,attempted,usb_epoch,bt_epoch;
    bool stopped;
} chime_source_t;
void chime_source_init(chime_source_t *s);
/* Both stream epochs enable retry when idle; a live renderer isolates changes. */
void chime_source_update(chime_source_t *s,bool bt_connected,uint32_t usb_epoch,uint32_t bt_epoch);
void chime_source_stop(chime_source_t *s);
void chime_source_resume(chime_source_t *s);
void chime_source_retry(chime_source_t *s); /* does not clear STOP */
bool chime_source_admit(chime_source_t *s,bool allowed,bool wanted);
const char *chime_source_name(chime_source_id_t source);

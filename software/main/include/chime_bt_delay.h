#pragma once
#include <stdbool.h>
#include <stdint.h>
/* Pure C, caller serialized. Estimates first-PCM-frame residence, NOT end-to-end
 * phone latency. Filter across packet-boundary samples, never target_ms. */
#define CHIME_BT_DELAY_DMA_US 10667U /* nominal 4 * 128 / 48000, not DMA readback */
#define CHIME_BT_DELAY_WARM_US 1000000ULL
#define CHIME_BT_DELAY_FRESH_US 1000000ULL
#define CHIME_BT_DELAY_REPORT_US 2000000ULL
#define CHIME_BT_DELAY_CHANGE_100US 50U
#define CHIME_BT_DELAY_MAX_US 6553500U
#ifndef CONFIG_CHIME_BT_DELAY_EXTRA_MS
#define CONFIG_CHIME_BT_DELAY_EXTRA_MS 0
#endif
typedef struct {
    bool valid,attempted;
    uint32_t samples,filtered_us,queue_us,residence_us;
    uint64_t first_us,last_us,attempt_us;
    uint16_t accepted_100us;
} chime_bt_delay_t;
void chime_bt_delay_reset(chime_bt_delay_t *d);
void chime_bt_delay_sample(chime_bt_delay_t *d,uint64_t now_us,unsigned queue_frames,
    unsigned rate,unsigned residence_us,unsigned fixed_us);
bool chime_bt_delay_candidate(const chime_bt_delay_t *d,uint64_t now_us,uint16_t *value);

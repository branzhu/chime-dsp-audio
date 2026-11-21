#pragma once
#include "dsp_transfer.h"
#include "sigma_protocol.h"

/* Test-only access policy for ADAU1451, not an authenticated project manifest.
 * No amp/EEPROM device address is ever routed. See bench-bridge.md limitations. */
typedef struct {
    dsp_transfer_t transfer;
    int (*status)(void *context, uint16_t *status);
    void *context;
    bool external;
} bench_dsp_t;
bool bench_dsp_init(bench_dsp_t *dsp, dsp_transfer_io_t io,
                    int (*status)(void *, uint16_t *));
int bench_dsp_execute(bench_dsp_t *dsp, const sigma_request_t *request, uint8_t *read_data);

/* External trusted SS project, DM0 direct parameter writes while core runs.
 * Owner MUST stop/revoke audition before every write. No guessed safeload. */
bool bench_dsp_init_external(bench_dsp_t *,dsp_transfer_io_t,int (*)(void *,uint16_t *));

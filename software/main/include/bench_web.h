#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "bench_dsp_status.h"
#include "chime_bluetooth.h"
/* Status and AP-only provisioning; never accesses DSP/GPIO/I2C. */
esp_err_t bench_web_start(uint16_t tcp_port);
void bench_web_set_client(bool connected);
void bench_web_record_request(int result);
void bench_web_stop(void);

/* Atomic command mailbox: -2=manual resume, -1=stop, positive=packed tone command (legacy 1/2/3), 0=none.
 * HTTP queues only; bridge owner performs ALL hardware operations. */
int bench_web_take_sound_request(void);
/* Owner publishes only naturally completed finite tests; 0 invalidates. */
void bench_web_sound_completed(int command);
void bench_web_sound_status(bool active,bool attempted,int result,uint16_t failed_reg);

bool bench_web_sound_stop_pending(void);

/* Fixed DSP diagnostic mailbox/cache; HTTP never touches hardware.
 * take changes queued->running; publish completes it, including rejected reads. */
bool bench_web_take_dsp_request(void);
void bench_web_dsp_publish(const bench_dsp_status_t *);
void bench_web_dsp_invalidate(void); /* SS+ write makes prior snapshot stale. */

void bench_web_set_default_ready(bool ready);

/* Output mailbox: positive queued, negative in-flight; owner is sole I2C writer.
 * Publish acknowledges applied (not merely requested) mode; failure keeps old. */
int bench_web_take_output_request(void);
bool bench_web_output_change_pending(void);
void bench_web_output_publish(unsigned mode,int result);

/* SS audition mailbox: 1=LR amps, 2=SUB amps, 3=both, 4=restore built-in.
 * Positive queued, negative in flight; HTTP never accesses hardware. */
int bench_web_take_tuning_request(void);
bool bench_web_tuning_pending(void);
void bench_web_tuning_publish(bool external,unsigned armed,int result);

bool bench_web_tuning_queued(void);
/* Cancel queued auditions on write/STOP/disconnect, but preserve queued restore. */
void bench_web_tuning_revoke(bool external,int result);

/* Owner publishes only cached Bluetooth state; HTTP never touches the stack. */
void bench_web_source_publish(unsigned selected,unsigned rendering,bool stopped,const chime_bluetooth_status_t *bt);

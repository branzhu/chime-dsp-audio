#pragma once
#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>
esp_err_t chime_usb_start(void);
void chime_usb_stop(void);
bool chime_usb_wanted(void);
esp_err_t chime_usb_adjust_volume(int steps); /* HID volume keys; input task only, no local UAC mutation */
bool chime_usb_muted(void); /* atomic host mute, no audio side effects */
uint32_t chime_usb_epoch(void);
esp_err_t chime_usb_output_begin(unsigned unused);
esp_err_t chime_usb_output_poll(bool *finished);
void chime_usb_output_end(void);
esp_err_t chime_usb_error(void);

void chime_usb_hid_poll(void); /* input task only */
int chime_usb_volume(void);
uint16_t chime_usb_fifo(void);
uint16_t chime_usb_read(void *pcm,uint16_t bytes);
void chime_usb_clear(void);
void chime_usb_fail(esp_err_t err);

/* Approximate atomic counters; coherent means no observed stream epoch change,
 * not a USB-frame-atomic snapshot. No FIFO access, IO, reset or playback. */
typedef struct {
    uint32_t epoch,rx_packets,rx_bytes,rx_zlp;
    bool installed,streaming,suspended,muted,wanted,coherent;
    int error;
} chime_usb_stats_t;
void chime_usb_snapshot(chime_usb_stats_t *stats);

#pragma once
#include "chime_provision.h"
#include "esp_err.h"
#include "esp_netif.h"
typedef struct {
    char ssid[33];
    int rssi;
    bool secure,supported;
} chime_access_point_t;
typedef struct {
    bool setup,connecting,online,configured,scanning;
    char phase[24],ip[16],ssid[33],ap_ssid[33],token[33];
    int error,scan_error;
    uint32_t generation;
    size_t count;
    chime_access_point_t networks[CHIME_SCAN_MAX];
} chime_network_status_t;
esp_err_t chime_network_start(void);
void chime_network_stop(void);
void chime_network_snapshot(chime_network_status_t *status);
void chime_network_request_scan(void);
esp_err_t chime_network_submit(uint32_t generation,unsigned index,const char *password);
bool chime_network_ready(void);
bool chime_network_led_setup(void); /* first/active setup, not automatic fallback AP */
uint32_t chime_network_epoch(void);
bool chime_network_setup_request(int socket);

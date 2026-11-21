#pragma once
#include <stdint.h>
#include "esp_err.h"
typedef struct {int marker;} esp_netif_t;
typedef struct {uint8_t octet[4];} esp_ip4_addr_t;
typedef struct {esp_ip4_addr_t ip;} esp_netif_ip_info_t;
#define IPSTR "%u.%u.%u.%u"
#define IP2STR(p) (unsigned)(p)->octet[0],(unsigned)(p)->octet[1],(unsigned)(p)->octet[2],(unsigned)(p)->octet[3]
esp_err_t esp_netif_get_ip_info(esp_netif_t *netif,esp_netif_ip_info_t *ip);

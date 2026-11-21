#pragma once
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
typedef struct {const char *key;const char *value;} mdns_txt_item_t;
#define MDNS_NAME_BUF_LEN 65
esp_err_t mdns_init(void);
esp_err_t mdns_hostname_get(char *hostname);
void mdns_free(void);
esp_err_t mdns_hostname_set(const char *hostname);
esp_err_t mdns_instance_name_set(const char *name);
esp_err_t mdns_service_add(const char *instance,const char *service,const char *protocol,uint16_t port,mdns_txt_item_t txt[],size_t count);

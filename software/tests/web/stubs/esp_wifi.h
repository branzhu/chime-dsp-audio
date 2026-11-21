#pragma once
#include "esp_err.h"
typedef struct {int num;} wifi_sta_list_t;
esp_err_t esp_wifi_ap_get_sta_list(wifi_sta_list_t *list);

#pragma once
#include <stdio.h>
#define ESP_LOGI(tag,...) do {(void)(tag);if(0)printf(__VA_ARGS__);}while(0)
#if CHIME_BT_CAPTURE_LOG
void chime_test_log(const char *tag,const char *format,...);
#define ESP_LOGE(tag,...) chime_test_log(tag,__VA_ARGS__)
#else
#define ESP_LOGE(tag,...) ESP_LOGI(tag,__VA_ARGS__)
#endif
#define ESP_LOGW(tag,...) ESP_LOGI(tag,__VA_ARGS__)

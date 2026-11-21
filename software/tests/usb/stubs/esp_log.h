#pragma once
void chime_usb_test_log(const char *tag,const char *format,...);
#define ESP_LOGI(tag, ...) chime_usb_test_log(tag,__VA_ARGS__)
#define ESP_LOGW(tag, ...) ESP_LOGI(tag,__VA_ARGS__)
#define ESP_LOGE(tag, ...) ESP_LOGI(tag,__VA_ARGS__)

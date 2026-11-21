#include "board_support.h"
#include "bench_bridge.h"
#include "chime_indicator.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    board_context_t board={0};
    esp_err_t result=board_support_init(&board);
    esp_err_t led_result=chime_indicator_start();
    if(led_result!=ESP_OK)ESP_LOGW("chime","LED unavailable: %s; audio initialization unchanged",esp_err_to_name(led_result));
    const esp_app_desc_t *app=esp_app_get_description();
    ESP_LOGI("chime","firmware=%s version=%s IDF=%s reset_reason=%d",app->project_name,app->version,app->idf_ver,(int)esp_reset_reason());
    if(result==ESP_OK)result=bench_bridge_run(&board);
    chime_indicator_fail();
    board_support_safe_shutdown(&board);
    ESP_LOGE("chime","initialization/service stopped: %s; remains SAFE",esp_err_to_name(result));
    while(true)vTaskDelay(pdMS_TO_TICKS(5000));
}

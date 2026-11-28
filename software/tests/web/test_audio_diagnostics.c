#include "sdkconfig.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "bench_status.h"
#include "chime_usb.h"
#include "chime_bluetooth.h"
#include "chime_render.h"
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
#include "chime_bt_decoder.h"
#endif
#include "cJSON.h"

typedef struct { int unused; } httpd_req_t;
typedef struct { uint8_t app_elf_sha256[32];const char *version,*idf_ver; } esp_app_desc_t;
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
#define MALLOC_CAP_DMA 4
#define MALLOC_CAP_RTCRAM 8
static const char audio_boot_id[]="0123456789abcdef";
static bool snapshot_available=true,stats_available;
static unsigned decoder_calls;
static int64_t esp_timer_get_time(void){return 1000;}
static const esp_app_desc_t *esp_app_get_description(void){
    static const esp_app_desc_t app={{0},"host-test","test-idf"};return &app;
}
static unsigned esp_clk_cpu_freq(void){return 360000000;}
static size_t heap_caps_get_free_size(unsigned caps){(void)caps;return 1234;}
static size_t heap_caps_get_minimum_free_size(unsigned caps){(void)caps;return 1000;}
static size_t heap_caps_get_largest_free_block(unsigned caps){(void)caps;return 512;}
static esp_err_t snapshot(bench_status_t *s){memset(s,0,sizeof(*s));return snapshot_available?ESP_OK:ESP_FAIL;}
void chime_usb_snapshot(chime_usb_stats_t *s){memset(s,0,sizeof(*s));s->coherent=true;}
void chime_bluetooth_snapshot(chime_bluetooth_status_t *s){memset(s,0,sizeof(*s));}
void chime_render_snapshot(chime_render_stats_t *s){memset(s,0,sizeof(*s));}
int chime_usb_volume(void){return 0;}
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
bool chime_bt_decoder_snapshot(chime_bt_decoder_stats_t *s){
    ++decoder_calls;
    if(!stats_available)return false;
    *s=(chime_bt_decoder_stats_t){.epoch=17,.packets=23};return true;
}
#endif
static esp_err_t error_response(httpd_req_t *req,const char *status,const char *body){
    (void)req;assert(strcmp(status,"503 Service Unavailable")==0);
    assert(strstr(body,"source_unavailable"));return ESP_FAIL;
}
static esp_err_t send_json(httpd_req_t *req,cJSON *root){
    (void)req;assert(root);
    cJSON *d=cJSON_GetObjectItemCaseSensitive(root,"decoder");assert(cJSON_IsObject(d));
    cJSON *valid=cJSON_GetObjectItemCaseSensitive(d,"valid");
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    assert(cJSON_IsTrue(valid)==stats_available);
    if(stats_available){
        assert(cJSON_GetObjectItemCaseSensitive(d,"epoch")->valueint==17);
        assert(cJSON_GetObjectItemCaseSensitive(d,"packets")->valueint==23);
    }else assert(cJSON_GetArraySize(d)==1);
#else
    assert(cJSON_IsFalse(valid)&&cJSON_GetArraySize(d)==1);
#endif
    assert(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root,"usb")));
    assert(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root,"bt")));
    assert(cJSON_IsObject(cJSON_GetObjectItemCaseSensitive(root,"render")));
    cJSON_Delete(root);return ESP_OK;
}
#include "audio_get.inc"
int main(void){
    httpd_req_t req={0};
    stats_available=false;assert(audio_get(&req)==ESP_OK);
    stats_available=true;assert(audio_get(&req)==ESP_OK);
#if CONFIG_BT_A2DP_USE_EXTERNAL_CODEC
    assert(decoder_calls==2);
#else
    /* No external-decoder symbol is provided or needed in SDK SBC mode. */
    assert(decoder_calls==0);
#endif
    snapshot_available=false;assert(audio_get(&req)==ESP_FAIL);
    puts("PASS production audio diagnostics: SDK SBC/external link boundary, unavailable stats and JSON schema");
    return 0;
}

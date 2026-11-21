#include "chime_network.h"
#include "chime_setup_guard.h"
#include "chime_led.h"
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "lwip/sockets.h"
#include "dhcpserver/dhcpserver.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

static const char *TAG="chime_wifi";
static esp_netif_t *sta_netif,*ap_netif;
static SemaphoreHandle_t mutex;
static QueueHandle_t submissions;
static TaskHandle_t worker_handle,dns_handle;
static int dns_socket=-1;
static chime_network_status_t state;
static atomic_bool ip_up,ready,scan_done,scan_requested,led_setup;
static atomic_uint epoch,disconnect_reason,scan_status;
static const uint64_t CONNECT_US=30000000ULL;
static bool trying,validating,keep_saved,ap_enabled;
static uint64_t deadline,ap_close_at,reconnect_at;
static chime_scan_clock_t scan_clock;
static chime_credentials_t saved;
static bool have_saved;

static uint64_t now(void) {return (uint64_t)esp_timer_get_time();}
static void lock(void) {xSemaphoreTake(mutex,portMAX_DELAY);}
static void unlock(void) {xSemaphoreGive(mutex);}
static void phase(const char *name,int error)
{
    lock();snprintf(state.phase,sizeof(state.phase),"%s",name);state.error=error;
    state.connecting=trying;state.online=atomic_load(&ready);unlock();
}
bool chime_network_led_setup(void){return atomic_load(&led_setup);}
bool chime_network_ready(void) {return atomic_load(&ready);}
uint32_t chime_network_epoch(void) {return atomic_load(&epoch);}
static void event(void *arg,esp_event_base_t base,int32_t id,void *data)
{
    (void)arg;
    if(base==IP_EVENT && id==IP_EVENT_STA_GOT_IP) atomic_store(&ip_up,true);
    if(base==IP_EVENT && id==IP_EVENT_STA_LOST_IP) {
        atomic_store(&ip_up,false);atomic_store(&ready,false);atomic_fetch_add(&epoch,1);
    }
    if(base==WIFI_EVENT && id==WIFI_EVENT_STA_DISCONNECTED) {
        atomic_store(&ip_up,false);atomic_store(&ready,false);atomic_fetch_add(&epoch,1);
        atomic_store(&disconnect_reason,((wifi_event_sta_disconnected_t *)data)->reason);
    }
    if(base==WIFI_EVENT && id==WIFI_EVENT_SCAN_DONE) {
        const wifi_event_sta_scan_done_t *done=data;
        atomic_store(&scan_status,done->status);atomic_store(&scan_done,true);
        ESP_LOGI(TAG,"SCAN event status=%lu found=%u id=%u",(unsigned long)done->status,(unsigned)done->number,(unsigned)done->scan_id);
    }
}
static esp_err_t load(void)
{
    nvs_handle_t h;esp_err_t err=nvs_open("chime_wifi",NVS_READONLY,&h);
    if(err!=ESP_OK)return err;
    size_t size=sizeof(saved);err=nvs_get_blob(h,"credentials",&saved,&size);nvs_close(h);
    if(err==ESP_OK && (size!=sizeof(saved)||!chime_credentials_valid(&saved)))err=ESP_ERR_INVALID_SIZE;
    return err;
}
static esp_err_t save(const chime_credentials_t *credentials)
{
    nvs_handle_t h;esp_err_t err=nvs_open("chime_wifi",NVS_READWRITE,&h);
    if(err!=ESP_OK)return err;
    err=nvs_set_blob(h,"credentials",credentials,sizeof(*credentials));
    if(err==ESP_OK)err=nvs_commit(h);
    nvs_close(h);return err;
}
static esp_err_t connect_to(const chime_credentials_t *credentials,bool candidate)
{
    /* The worker alone owns Wi-Fi configuration. Old credentials remain in NVS
     * until the candidate has a DHCP lease and its single blob commit succeeds. */
    atomic_store(&ready,false);atomic_fetch_add(&epoch,1);
    esp_wifi_disconnect();atomic_store(&ip_up,false);atomic_store(&disconnect_reason,0);
    wifi_config_t config={0};
    memcpy(config.sta.ssid,credentials->ssid,credentials->ssid_len);
    memcpy(config.sta.password,credentials->password,credentials->password_len);
    config.sta.threshold.authmode=credentials->password_len?WIFI_AUTH_WPA2_PSK:WIFI_AUTH_OPEN;
    config.sta.sae_pwe_h2e=WPA3_SAE_PWE_BOTH;
    esp_err_t err=esp_wifi_set_config(WIFI_IF_STA,&config);
    memset(&config,0,sizeof(config));
    if(err==ESP_OK)err=esp_wifi_connect();
    if(err==ESP_OK) {
        trying=true;validating=candidate;keep_saved=false;deadline=now()+CONNECT_US;reconnect_at=now()+3000000ULL;
        lock();memcpy(state.ssid,credentials->ssid,credentials->ssid_len);state.ssid[credentials->ssid_len]=0;unlock();
        phase("connecting",0);
    }
    return err;
}
static esp_err_t open_setup(void)
{
    if(ap_enabled)return ESP_OK;
    atomic_store(&ready,false);atomic_fetch_add(&epoch,1);
    wifi_config_t config={0};
    lock();memcpy(config.ap.ssid,state.ap_ssid,sizeof(config.ap.ssid));unlock();
    snprintf((char *)config.ap.password,sizeof(config.ap.password),"%s",CHIME_SETUP_PASSWORD);
    config.ap.ssid_len=(uint8_t)strlen((char *)config.ap.ssid);
    config.ap.channel=6;config.ap.max_connection=2;config.ap.authmode=WIFI_AUTH_WPA2_PSK;
    esp_err_t err=esp_wifi_set_mode(WIFI_MODE_APSTA);
    if(err==ESP_OK)err=esp_wifi_set_config(WIFI_IF_AP,&config);
    if(err!=ESP_OK)return err;
    ap_enabled=true;lock();state.setup=true;unlock();
    phase("setup",0);atomic_store(&scan_requested,true);
    ESP_LOGW(TAG,"SETUP SSID=%s PASSWORD=%s URL=http://" CHIME_AP_IP "/ (captive portal; amps OFF)",config.ap.ssid,CHIME_SETUP_PASSWORD);
    return ESP_OK;
}
static bool supported(wifi_auth_mode_t auth)
{
    return auth==WIFI_AUTH_OPEN||auth==WIFI_AUTH_WPA2_PSK||auth==WIFI_AUTH_WPA_WPA2_PSK||
           auth==WIFI_AUTH_WPA3_PSK||auth==WIFI_AUTH_WPA2_WPA3_PSK;
}
static void finish_scan(void)
{
    wifi_ap_record_t records[CHIME_SCAN_MAX];uint16_t count=CHIME_SCAN_MAX;
    esp_err_t err=atomic_load(&scan_status)==0?esp_wifi_scan_get_ap_records(&count,records):ESP_FAIL;
    if(err!=ESP_OK)esp_wifi_clear_ap_list();
    chime_scan_finish(&scan_clock);
    lock();state.scanning=false;state.scan_error=err;
    if(err==ESP_OK) {
        state.count=0;++state.generation;
        for(unsigned i=0;i<count;++i) {
            records[i].ssid[32]=0;size_t len=strnlen((char *)records[i].ssid,32);if(!len)continue;
            bool duplicate=false;
            for(size_t j=0;j<state.count;++j)if(strcmp(state.networks[j].ssid,(char *)records[i].ssid)==0)duplicate=true;
            if(duplicate)continue;
            chime_access_point_t *ap=&state.networks[state.count++];*ap=(chime_access_point_t){0};
            memcpy(ap->ssid,records[i].ssid,len);ap->rssi=records[i].rssi;
            ap->secure=records[i].authmode!=WIFI_AUTH_OPEN;ap->supported=supported(records[i].authmode);
        }
    }
    size_t visible=state.count;uint32_t generation=state.generation;unlock();
    ESP_LOGI(TAG,"SCAN complete result=%s(%d) records=%u visible=%u generation=%lu",esp_err_to_name(err),(int)err,(unsigned)count,(unsigned)visible,(unsigned long)generation);
}
void chime_network_snapshot(chime_network_status_t *out)
{
    lock();*out=state;unlock();
    out->online=atomic_load(&ready);
    esp_netif_ip_info_t ip={0};
    esp_netif_t *netif=atomic_load(&ip_up)?sta_netif:ap_netif;
    if(esp_netif_get_ip_info(netif,&ip)==ESP_OK)snprintf(out->ip,sizeof(out->ip),IPSTR,IP2STR(&ip.ip));
    else snprintf(out->ip,sizeof(out->ip),"0.0.0.0");
}
void chime_network_request_scan(void) {atomic_store(&scan_requested,true);}
esp_err_t chime_network_submit(uint32_t generation,unsigned index,const char *password)
{
    if(password==NULL)return ESP_ERR_INVALID_ARG;
    size_t length=strnlen(password,65);
    chime_credentials_t candidate={.version=1};
    lock();
    if(!state.setup||state.connecting||strcmp(state.phase,"connected")==0) {unlock();return ESP_ERR_INVALID_STATE;}
    if(generation!=state.generation||index>=state.count||!state.networks[index].supported||
       !chime_password_valid(password,length,state.networks[index].secure)) {unlock();return ESP_ERR_INVALID_ARG;}
    candidate.ssid_len=(uint8_t)strlen(state.networks[index].ssid);candidate.password_len=(uint8_t)length;
    memcpy(candidate.ssid,state.networks[index].ssid,candidate.ssid_len);memcpy(candidate.password,password,length);
    state.connecting=true;
    if(xQueueSend(submissions,&candidate,0)!=pdTRUE){state.connecting=false;unlock();memset(&candidate,0,sizeof(candidate));return ESP_ERR_INVALID_STATE;}
    atomic_store(&led_setup,chime_led_setup_requested(false,true,false));
    unlock();memset(&candidate,0,sizeof(candidate));return ESP_OK;
}
bool chime_network_setup_request(int socket)
{
    lock();bool setup=state.setup;unlock();if(!setup)return false;
    /* HTTPD uses an IPv6 dual-stack listener when LWIP_IPV6 is enabled.
     * Its IPv4 clients have a LOCAL ::ffff:192.168.4.1 address; sockaddr_in
     * both truncated that result and interpreted sin6_flowinfo as sin_addr. */
    struct sockaddr_storage local={0};socklen_t size=sizeof(local);
    return getsockname(socket,(struct sockaddr *)&local,&size)==0 &&
           chime_setup_address_is_ap((const struct sockaddr *)&local,(size_t)size);
}
static void dns_task(void *arg)
{
    (void)arg;uint8_t query[512],response[512];const uint8_t ip[]={192,168,4,1};
    int sock=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);dns_socket=sock;
    const struct sockaddr_in bind_to={.sin_family=AF_INET,.sin_port=htons(53),.sin_addr={.s_addr=htonl(INADDR_ANY)}};
    if(sock<0 || bind(sock,(const struct sockaddr *)&bind_to,sizeof(bind_to))!=0) {
        ESP_LOGE(TAG,"captive DNS unavailable; use http://" CHIME_AP_IP "/");if(sock>=0)close(sock);dns_socket=-1;dns_handle=NULL;vTaskDelete(NULL);return;
    }
    for(;;) {
        struct sockaddr_in peer;socklen_t len=sizeof(peer);
        int bytes=recvfrom(sock,query,sizeof(query),0,(struct sockaddr *)&peer,&len);
        if(bytes<=0)continue;
        lock();bool setup=state.setup;unlock();
        if(!setup || (ntohl(peer.sin_addr.s_addr)&0xffffff00U)!=0xc0a80400U)continue;
        size_t count=chime_dns_reply(query,(size_t)bytes,ip,response,sizeof(response));
        if(count)sendto(sock,response,count,0,(struct sockaddr *)&peer,len);
    }
}
static void worker(void *arg)
{
    (void)arg;chime_credentials_t candidate={0};
    for(;;) {
        chime_credentials_t submitted;
        if(xQueueReceive(submissions,&submitted,pdMS_TO_TICKS(100))==pdTRUE) {
            esp_wifi_scan_stop();esp_wifi_clear_ap_list();chime_scan_finish(&scan_clock);scan_clock.pending=false;
            atomic_store(&scan_requested,false);lock();state.scanning=false;unlock();atomic_store(&scan_done,false);
            candidate=submitted;memset(&submitted,0,sizeof(submitted));
            esp_err_t err=connect_to(&candidate,true);
            if(err!=ESP_OK){trying=false;phase("failed",err);memset(&candidate,0,sizeof(candidate));}
        }
        uint64_t time=now();
        if(trying && atomic_load(&ip_up)) {
            esp_err_t err=validating?save(&candidate):ESP_OK;
            trying=false;
            if(err==ESP_OK) {
                if(validating){saved=candidate;have_saved=true;}
                atomic_store(&led_setup,chime_led_setup_requested(false,false,true));
                keep_saved=true;lock();state.configured=have_saved;unlock();
                if(ap_enabled){ap_close_at=time+15000000ULL;phase("connected",0);}
                else {atomic_store(&ready,true);phase("online",0);}
                ESP_LOGI(TAG,"Wi-Fi DHCP ready; credentials %s; http://chime.local/",validating?"saved":"loaded");
            } else {esp_wifi_disconnect();atomic_store(&ip_up,false);phase("save_failed",err);}
            memset(&candidate,0,sizeof(candidate));
        }
        if(trying && time>=deadline) {
            trying=false;esp_wifi_disconnect();atomic_store(&ip_up,false);
            int reason=(int)atomic_load(&disconnect_reason);
            esp_err_t err=open_setup();phase("failed",err==ESP_OK?reason:err);
            memset(&candidate,0,sizeof(candidate));
        } else if(trying && !atomic_load(&ip_up) && time>=reconnect_at) {
            esp_wifi_connect();reconnect_at=time+3000000ULL;
        }
        if(ap_close_at && !atomic_load(&ip_up)) {
            ap_close_at=0;keep_saved=false;phase("failed",(int)atomic_load(&disconnect_reason));
        }
        if(ap_close_at && time>=ap_close_at) {
            esp_err_t err=esp_wifi_set_mode(WIFI_MODE_STA);
            if(err==ESP_OK) {ap_enabled=false;ap_close_at=0;lock();state.setup=false;unlock();atomic_store(&ready,true);phase("online",0);}
            else {ap_close_at=time+3000000ULL;phase("failed",err);}
        }
        if(!trying && keep_saved && !atomic_load(&ip_up) && !ap_enabled) {
            esp_err_t err=connect_to(&saved,false);
            if(err!=ESP_OK) {keep_saved=false;open_setup();phase("failed",err);}
        }
        if(atomic_exchange(&scan_done,false)) {
            if(scan_clock.active)finish_scan();else esp_wifi_clear_ap_list();
        }
        if(atomic_exchange(&scan_requested,false))chime_scan_request(&scan_clock);
        chime_scan_action_t scan_action=chime_scan_poll(&scan_clock,time,ap_enabled&&!trying&&!ap_close_at);
        if(scan_action==CHIME_SCAN_TIMEOUT) {
            esp_err_t stop=esp_wifi_scan_stop();esp_wifi_clear_ap_list();atomic_store(&scan_done,false);
            lock();state.scanning=false;state.scan_error=ESP_ERR_TIMEOUT;unlock();
            ESP_LOGW(TAG,"SCAN timeout after 15s; stop=%s; select rescan to retry",esp_err_to_name(stop));
        } else if(scan_action==CHIME_SCAN_START) {
            /* NULL selects documented SDK defaults on this preview target,
             * including channel bitmap/dwell defaults, rather than a zeroed config. */
            esp_err_t err=esp_wifi_scan_start(NULL,false);
            lock();state.scanning=err==ESP_OK;state.scan_error=err;unlock();
            if(err!=ESP_OK)chime_scan_finish(&scan_clock);
            ESP_LOGI(TAG,"SCAN start result=%s(%d) timeout=15s",esp_err_to_name(err),(int)err);
        }
    }
}
esp_err_t chime_network_start(void)
{
    esp_err_t err=nvs_flash_init();if(err!=ESP_OK)return err; /* never erase all NVS */
    mutex=xSemaphoreCreateMutex();submissions=xQueueCreate(1,sizeof(chime_credentials_t));
    if(!mutex||!submissions)return ESP_ERR_NO_MEM;
    have_saved=load()==ESP_OK;
    if((err=esp_netif_init())!=ESP_OK)return err;
    if((err=esp_event_loop_create_default())!=ESP_OK)return err;
    sta_netif=esp_netif_create_default_wifi_sta();ap_netif=esp_netif_create_default_wifi_ap();
    if(!sta_netif||!ap_netif)return ESP_ERR_NO_MEM;
    esp_netif_set_hostname(sta_netif,CHIME_HOSTNAME);esp_netif_set_hostname(ap_netif,CHIME_HOSTNAME);
    esp_netif_ip_info_t ip={0};esp_netif_str_to_ip4(CHIME_AP_IP,&ip.ip);ip.gw=ip.ip;esp_netif_str_to_ip4("255.255.255.0",&ip.netmask);
    err=esp_netif_dhcps_stop(ap_netif);
    if(err!=ESP_OK&&err!=ESP_ERR_ESP_NETIF_DHCP_ALREADY_STOPPED)return err;
    if((err=esp_netif_set_ip_info(ap_netif,&ip))!=ESP_OK)return err;
    esp_netif_dns_info_t dns={.ip={.type=ESP_IPADDR_TYPE_V4,.u_addr={.ip4=ip.ip}}};
    if((err=esp_netif_set_dns_info(ap_netif,ESP_NETIF_DNS_MAIN,&dns))!=ESP_OK)return err;
    dhcps_offer_t offer=OFFER_DNS;
    if((err=esp_netif_dhcps_option(ap_netif,ESP_NETIF_OP_SET,ESP_NETIF_DOMAIN_NAME_SERVER,&offer,sizeof(offer)))!=ESP_OK)return err;
    if((err=esp_netif_dhcps_start(ap_netif))!=ESP_OK)return err;
    wifi_init_config_t config=WIFI_INIT_CONFIG_DEFAULT();if((err=esp_wifi_init(&config))!=ESP_OK)return err;
    if((err=esp_wifi_set_storage(WIFI_STORAGE_RAM))!=ESP_OK)return err;
    if((err=esp_event_handler_register(WIFI_EVENT,ESP_EVENT_ANY_ID,event,NULL))!=ESP_OK)return err;
    if((err=esp_event_handler_register(IP_EVENT,ESP_EVENT_ANY_ID,event,NULL))!=ESP_OK)return err;
    uint8_t mac[6];if((err=esp_read_mac(mac,ESP_MAC_WIFI_SOFTAP))!=ESP_OK)return err;
    snprintf(state.ap_ssid,sizeof(state.ap_ssid),"Chime-Setup-%02X%02X",mac[4],mac[5]);
    state.configured=have_saved;
    if((err=esp_wifi_set_mode(WIFI_MODE_STA))!=ESP_OK)return err;
    if((err=esp_wifi_start())!=ESP_OK)return err;
    /* Wi-Fi RF is now active: esp_random has its continuous hardware entropy source. */
    snprintf(state.token,sizeof(state.token),"%08lx%08lx%08lx%08lx",(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random(),(unsigned long)esp_random());
    if(have_saved) {err=connect_to(&saved,false);if(err!=ESP_OK)err=open_setup();}
    else {atomic_store(&led_setup,chime_led_setup_requested(true,false,false));err=open_setup();}
    if(err!=ESP_OK)return err;
    if(xTaskCreate(worker,"chime_wifi",6144,NULL,4,&worker_handle)!=pdPASS)return ESP_ERR_NO_MEM;
    if(xTaskCreate(dns_task,"chime_dns",3072,NULL,3,&dns_handle)!=pdPASS)return ESP_ERR_NO_MEM;
    return ESP_OK;
}

void chime_network_stop(void)
{
    atomic_store(&led_setup,false);
    atomic_store(&ready,false);atomic_fetch_add(&epoch,1);
    if(worker_handle){vTaskDelete(worker_handle);worker_handle=NULL;}
    if(dns_handle){vTaskDelete(dns_handle);dns_handle=NULL;}
    if(dns_socket>=0){close(dns_socket);dns_socket=-1;}
    esp_wifi_stop();
    /* Fatal shutdown only: app requires reset before restarting these services. */
}

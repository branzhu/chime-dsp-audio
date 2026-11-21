#include "bench_web.h"
#include "bench_status.h"
#include "bench_tone.h"
#include "chime_network.h"
#include <stdatomic.h>
#include "bench_output.h"
#include <stdio.h>
#include <string.h>
#include "cJSON.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "mdns.h"

_Static_assert(CHIME_BT_CODEC_NONE==0&&CHIME_BT_CODEC_SBC==1&&CHIME_BT_CODEC_AAC==2&&CHIME_BT_CODEC_APTX==3&&CHIME_BT_CODEC_APTX_HD==4&&CHIME_BT_CODEC_OPUS==5&&CHIME_BT_CODEC_LDAC==6&&CHIME_BT_CODEC_LHDC_V5==7,"status wire codec mapping");

static const char *TAG="chime_web";
static httpd_handle_t server;
static bool mdns_started;
static uint16_t bridge_port;
static atomic_bool client_connected,default_ready;
static atomic_bool dsp_external;
static atomic_int tuning_request,tuning_result;
static atomic_uint tuning_armed;
static bool dsp_available(void){return atomic_load(&client_connected)||atomic_load(&default_ready)||atomic_load(&dsp_external);}
void bench_web_set_default_ready(bool ready){atomic_store(&default_ready,ready);bench_web_dsp_invalidate();}
static atomic_uint_least32_t requests;
static atomic_int last_result;
static atomic_int sound_request,sound_result,sound_completed_command;
static atomic_int output_request,output_result;
static atomic_uint output_mode=BENCH_OUTPUT_DEFAULT;
static unsigned sound_tag; /* HTTP server task is the only writer. */
static atomic_bool sound_active,sound_attempted;
static atomic_uint sound_failed_reg;
/* All cache words are atomic: bounded seqlock reads have no C data races.
 * Single publisher = bridge owner. HTTP serves only this cached snapshot. */
static atomic_uint dsp_sequence,dsp_generation,dsp_captured_ms;
static atomic_int dsp_request,dsp_result; /* 0=idle,1=queued,2=running */
static atomic_bool dsp_current,dsp_complete;
static struct {atomic_uint value;atomic_int error;atomic_bool valid;} dsp_values[BENCH_DSP_STATUS_COUNT];
extern const char index_start[] asm("_binary_bench_index_html_start");

static atomic_uint source_selected,source_rendering,bt_codec,bt_rate,bt_queued,bt_overflows,bt_underruns;
static atomic_bool source_stopped,bt_available,bt_connected,bt_playing;
static atomic_int bt_error,bt_drift_ppm;
static atomic_uint bt_stream_state,bt_rebuffer_events,bt_rebuffer_resumes,bt_rebuffer_wait_ms;
void bench_web_source_publish(unsigned selected,unsigned rendering,bool stopped,const chime_bluetooth_status_t *bt)
{
    atomic_store(&source_selected,selected);atomic_store(&source_rendering,rendering);atomic_store(&source_stopped,stopped);
    atomic_store(&bt_available,bt->available);atomic_store(&bt_connected,bt->connected);atomic_store(&bt_playing,bt->playing);
    atomic_store(&bt_codec,bt->codec);atomic_store(&bt_rate,bt->rate);atomic_store(&bt_queued,bt->queued_frames);atomic_store(&bt_error,bt->error);
    atomic_store(&bt_stream_state,bt->stream_state);atomic_store(&bt_rebuffer_events,bt->rebuffer_events);
    atomic_store(&bt_rebuffer_resumes,bt->rebuffer_resumes);atomic_store(&bt_rebuffer_wait_ms,bt->rebuffer_wait_ms);
    atomic_store(&bt_drift_ppm,bt->drift_ppm);atomic_store(&bt_overflows,bt->overflows);atomic_store(&bt_underruns,bt->underruns);
}
static esp_err_t snapshot(bench_status_t *status)
{
    chime_network_status_t network;chime_network_snapshot(&network);
    *status=(bench_status_t){.tcp_port=bridge_port,
        .selected_source=atomic_load(&source_selected),.rendering_source=atomic_load(&source_rendering),
        .source_stopped=atomic_load(&source_stopped),.bt_available=atomic_load(&bt_available),
        .bt_connected=atomic_load(&bt_connected),.bt_playing=atomic_load(&bt_playing),
        .bt_codec=atomic_load(&bt_codec),.bt_rate=atomic_load(&bt_rate),.bt_queued=atomic_load(&bt_queued),.bt_error=atomic_load(&bt_error),
        .bt_stream_state=atomic_load(&bt_stream_state),.bt_rebuffer_events=atomic_load(&bt_rebuffer_events),
        .bt_rebuffer_resumes=atomic_load(&bt_rebuffer_resumes),.bt_rebuffer_wait_ms=atomic_load(&bt_rebuffer_wait_ms),
        .bt_drift_ppm=atomic_load(&bt_drift_ppm),.bt_overflows=atomic_load(&bt_overflows),.bt_underruns=atomic_load(&bt_underruns)};
    snprintf(status->ip,sizeof(status->ip),"%s",network.ip);
    char hostname[MDNS_NAME_BUF_LEN];esp_err_t err=mdns_hostname_get(hostname);
    if(err!=ESP_OK)return err;
    int length=snprintf(status->hostname,sizeof(status->hostname),"%s",hostname);
    if(length<0||(size_t)length>=sizeof(status->hostname))return ESP_FAIL;
    status->station_connected=network.online;
    status->bridge_connected=atomic_load(&client_connected);
    status->dsp_default_ready=atomic_load(&default_ready);
    status->dsp_external=atomic_load(&dsp_external);
    status->tuning_armed=atomic_load(&tuning_armed);
    int tuning=atomic_load(&tuning_request);
    status->tuning_pending=(unsigned)(tuning<0?-tuning:tuning);
    status->tuning_result=atomic_load(&tuning_result);
    status->requests=(uint32_t)atomic_load(&requests);status->last_result=atomic_load(&last_result);
    status->amplifiers_enabled=atomic_load(&sound_active);
    status->sound_attempted=atomic_load(&sound_attempted);
    status->sound_result=atomic_load(&sound_result);
    status->sound_completed_command=atomic_load(&sound_completed_command);
    status->output_mode=atomic_load(&output_mode);
    int pending=atomic_load(&output_request);
    status->output_pending=(unsigned)(pending<0?-pending:pending);
    status->output_result=atomic_load(&output_result);
    status->sound_failed_reg=(uint16_t)atomic_load(&sound_failed_reg);
    return ESP_OK;
}
static esp_err_t headers(httpd_req_t *req,const char *type)
{
    esp_err_t err=httpd_resp_set_type(req,type);
    if(err==ESP_OK)err=httpd_resp_set_hdr(req,"Cache-Control","no-store");
    if(err==ESP_OK)err=httpd_resp_set_hdr(req,"X-Content-Type-Options","nosniff");
    if(err==ESP_OK)err=httpd_resp_set_hdr(req,"Content-Security-Policy",
        "default-src 'none'; script-src 'unsafe-inline'; style-src 'unsafe-inline'; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
    return err;
}
static esp_err_t error_response(httpd_req_t *req,const char *status,const char *message)
{
    httpd_resp_set_status(req,status);headers(req,"application/json; charset=utf-8");
    return httpd_resp_send(req,message,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t send_json(httpd_req_t *req,cJSON *root)
{
    if(root==NULL)return error_response(req,"500 Internal Server Error","{\"error\":\"out_of_memory\"}");
    char *json=cJSON_PrintUnformatted(root);cJSON_Delete(root);
    if(json==NULL)return error_response(req,"500 Internal Server Error","{\"error\":\"out_of_memory\"}");
    esp_err_t err=headers(req,"application/json; charset=utf-8");
    if(err==ESP_OK)err=httpd_resp_send(req,json,HTTPD_RESP_USE_STRLEN);
    cJSON_free(json);return err;
}
static esp_err_t status_get(httpd_req_t *req)
{
    bench_status_t status;char json[1536];
    if(snapshot(&status)!=ESP_OK||!bench_status_json(&status,json,sizeof(json)))
        return error_response(req,"500 Internal Server Error","{\"error\":\"status_unavailable\"}");
    esp_err_t err=headers(req,"application/json; charset=utf-8");
    return err==ESP_OK?httpd_resp_send(req,json,HTTPD_RESP_USE_STRLEN):err;
}
void bench_web_dsp_invalidate(void){atomic_store(&dsp_current,false);}
bool bench_web_take_dsp_request(void)
{
    int expected=1;return atomic_compare_exchange_strong(&dsp_request,&expected,2);
}
void bench_web_dsp_publish(const bench_dsp_status_t *s)
{
    atomic_fetch_add(&dsp_sequence,1);
    for(unsigned i=0;i<BENCH_DSP_STATUS_COUNT;++i) {
        atomic_store(&dsp_values[i].value,s->registers[i].value);
        atomic_store(&dsp_values[i].error,s->registers[i].error);
        atomic_store(&dsp_values[i].valid,s->registers[i].valid);
    }
    atomic_store(&dsp_captured_ms,s->captured_ms);atomic_store(&dsp_result,s->result);
    atomic_store(&dsp_complete,s->complete);atomic_fetch_add(&dsp_generation,1);
    atomic_store(&dsp_current,dsp_available());atomic_fetch_add(&dsp_sequence,1);
    atomic_store(&dsp_request,0);
}
static esp_err_t dsp_get(httpd_req_t *req)
{
    bench_dsp_status_t s={0};unsigned generation=0;bool consistent=false;
    for(unsigned attempt=0;attempt<3;++attempt) {
        unsigned seq=atomic_load(&dsp_sequence);if(seq&1U)continue;
        generation=atomic_load(&dsp_generation);
        s.captured_ms=atomic_load(&dsp_captured_ms);s.result=atomic_load(&dsp_result);
        s.complete=atomic_load(&dsp_complete);
        for(unsigned i=0;i<BENCH_DSP_STATUS_COUNT;++i) {
            s.registers[i].value=(uint16_t)atomic_load(&dsp_values[i].value);
            s.registers[i].error=atomic_load(&dsp_values[i].error);
            s.registers[i].valid=atomic_load(&dsp_values[i].valid);
        }
        if(seq==atomic_load(&dsp_sequence)){consistent=true;break;}
    }
    if(!consistent)return error_response(req,"503 Service Unavailable","{\"error\":\"snapshot_busy\"}");
    cJSON *root=cJSON_CreateObject(),*values=cJSON_CreateArray();
    if(!root||!values){cJSON_Delete(root);cJSON_Delete(values);return send_json(req,NULL);}
    cJSON_AddNumberToObject(root,"generation",generation);
    cJSON_AddBoolToObject(root,"present",generation!=0);
    cJSON_AddBoolToObject(root,"current",atomic_load(&dsp_current));
    cJSON_AddBoolToObject(root,"busy",atomic_load(&dsp_request)!=0);
    cJSON_AddBoolToObject(root,"session_connected",dsp_available()); /* legacy key = DSP available */
    cJSON_AddBoolToObject(root,"complete",s.complete);
    cJSON_AddNumberToObject(root,"captured_ms",s.captured_ms);
    cJSON_AddNumberToObject(root,"result",s.result);
    cJSON_AddStringToObject(root,"result_name",esp_err_to_name(s.result));
    cJSON_AddItemToObject(root,"registers",values);
    if(generation)for(unsigned i=0;i<BENCH_DSP_STATUS_COUNT;++i) {
        cJSON *item=cJSON_CreateObject();if(!item){cJSON_Delete(root);return send_json(req,NULL);}
        cJSON_AddNumberToObject(item,"address",bench_dsp_status_registers[i].address);
        cJSON_AddStringToObject(item,"name",bench_dsp_status_registers[i].name);
        cJSON_AddBoolToObject(item,"valid",s.registers[i].valid);
        if(s.registers[i].valid)cJSON_AddNumberToObject(item,"value",s.registers[i].value);
        cJSON_AddNumberToObject(item,"error",s.registers[i].error);
        cJSON_AddItemToArray(values,item);
    }
    return send_json(req,root);
}
static esp_err_t portal_redirect(httpd_req_t *req,httpd_err_code_t code)
{
    (void)code;
    if(!chime_network_setup_request(httpd_req_to_sockfd(req)))
        return error_response(req,"404 Not Found","{\"error\":\"not_found\"}");
    httpd_resp_set_status(req,"302 Found");httpd_resp_set_hdr(req,"Location","http://" CHIME_AP_IP "/");
    headers(req,"text/plain; charset=utf-8");
    return httpd_resp_send(req,"Open http://" CHIME_AP_IP "/ to configure Chime Wi-Fi.",HTTPD_RESP_USE_STRLEN);
}
static esp_err_t index_get(httpd_req_t *req)
{
    if(chime_network_setup_request(httpd_req_to_sockfd(req))) {
        char host[96];
        if(httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host))!=ESP_OK ||
           (strcmp(host,CHIME_AP_IP)!=0&&strcmp(host,CHIME_AP_IP ":80")!=0&&strcmp(host,"chime.local")!=0&&strcmp(host,"chime.local:80")!=0))
            return portal_redirect(req,HTTPD_404_NOT_FOUND);
    }
    bench_status_t status;char json[1536];
    if(snapshot(&status)!=ESP_OK||!bench_status_json(&status,json,sizeof(json)))
        return error_response(req,"500 Internal Server Error","{\"error\":\"status_unavailable\"}");
    const char *marker=strstr(index_start,"{{DEVICE_IP}}");
    if(!marker)return ESP_FAIL;
    esp_err_t err=headers(req,"text/html; charset=utf-8");
    if(err==ESP_OK)err=httpd_resp_send_chunk(req,index_start,(ssize_t)(marker-index_start));
    if(err==ESP_OK)err=httpd_resp_send_chunk(req,status.ip,HTTPD_RESP_USE_STRLEN);
    if(err==ESP_OK)err=httpd_resp_send_chunk(req,marker+strlen("{{DEVICE_IP}}"),HTTPD_RESP_USE_STRLEN);
    if(err==ESP_OK)err=httpd_resp_send_chunk(req,NULL,0);
    return err;
}
static esp_err_t network_get(httpd_req_t *req)
{
    chime_network_status_t network;chime_network_snapshot(&network);
    cJSON *root=cJSON_CreateObject();if(!root)return send_json(req,NULL);
    cJSON_AddBoolToObject(root,"setup",network.setup);cJSON_AddBoolToObject(root,"connecting",network.connecting);
    cJSON_AddBoolToObject(root,"online",network.online);cJSON_AddBoolToObject(root,"configured",network.configured);
    cJSON_AddStringToObject(root,"phase",network.phase);cJSON_AddStringToObject(root,"ip",network.ip);
    cJSON_AddStringToObject(root,"ssid",network.ssid);cJSON_AddStringToObject(root,"ap_ssid",network.ap_ssid);
    cJSON_AddNumberToObject(root,"error",network.error);
    if(chime_network_setup_request(httpd_req_to_sockfd(req)))cJSON_AddStringToObject(root,"token",network.token);
    return send_json(req,root);
}
static esp_err_t scan_get(httpd_req_t *req)
{
    if(!chime_network_setup_request(httpd_req_to_sockfd(req)))return error_response(req,"403 Forbidden","{\"error\":\"setup_ap_only\"}");
    /* Poll cached results without continuously starting another RF scan. */
    const char *query=strchr(req->uri,'?');
    if(query&&strcmp(query,"?refresh=1")==0)chime_network_request_scan();
    chime_network_status_t network;chime_network_snapshot(&network);
    cJSON *root=cJSON_CreateObject();if(!root)return send_json(req,NULL);
    cJSON_AddBoolToObject(root,"scanning",network.scanning);cJSON_AddNumberToObject(root,"generation",network.generation);
    cJSON_AddNumberToObject(root,"error",network.scan_error);
    cJSON_AddStringToObject(root,"error_name",esp_err_to_name(network.scan_error));
    cJSON *list=cJSON_AddArrayToObject(root,"networks");if(!list){cJSON_Delete(root);return send_json(req,NULL);}
    for(size_t i=0;i<network.count;++i) {
        cJSON *ap=cJSON_CreateObject();if(!ap){cJSON_Delete(root);return send_json(req,NULL);}
        cJSON_AddNumberToObject(ap,"id",(double)i);cJSON_AddStringToObject(ap,"ssid",network.networks[i].ssid);
        cJSON_AddNumberToObject(ap,"rssi",network.networks[i].rssi);cJSON_AddBoolToObject(ap,"secure",network.networks[i].secure);
        cJSON_AddBoolToObject(ap,"supported",network.networks[i].supported);cJSON_AddItemToArray(list,ap);
    }
    return send_json(req,root);
}
/* This API accepts a flat object only. Bound parser stack use before cJSON sees
 * untrusted input, even when a 512-byte body contains hundreds of '[' tokens. */
static bool flat_json(const char *text,size_t length)
{
    unsigned depth=0;bool quoted=false,escaped=false;
    for(size_t i=0;i<length;++i) {
        char c=text[i];
        if(quoted){if(escaped)escaped=false;else if(c=='\\')escaped=true;else if(c=='"')quoted=false;continue;}
        if(c=='"')quoted=true;
        else if(c=='{'||c=='['){if(++depth>1)return false;}
        else if(c=='}'||c==']'){if(!depth)return false;--depth;}
    }
    return !quoted&&!depth;
}
static esp_err_t wifi_post(httpd_req_t *req)
{
    chime_network_status_t network;chime_network_snapshot(&network);
    char token[40],origin[96],type[64];
    if(!chime_network_setup_request(httpd_req_to_sockfd(req))||
       httpd_req_get_hdr_value_str(req,"X-Chime-Token",token,sizeof(token))!=ESP_OK||strcmp(token,network.token)!=0)
        return error_response(req,"403 Forbidden","{\"error\":\"setup_token_required\"}");
    if(httpd_req_get_hdr_value_len(req,"Origin")>0) {
        if(httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))!=ESP_OK||
           (strcmp(origin,"http://" CHIME_AP_IP)!=0&&strcmp(origin,"http://" CHIME_AP_IP ":80")!=0&&
            strcmp(origin,"http://chime.local")!=0&&strcmp(origin,"http://chime.local:80")!=0))
            return error_response(req,"403 Forbidden","{\"error\":\"invalid_origin\"}");
    }
    if(httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||
       (strcmp(type,"application/json")!=0&&strcmp(type,"application/json; charset=utf-8")!=0))
        return error_response(req,"415 Unsupported Media Type","{\"error\":\"json_required\"}");
    if(req->content_len==0||req->content_len>512)return error_response(req,"413 Content Too Large","{\"error\":\"body_size\"}");
    char body[513];size_t count=0;
    while(count<req->content_len) {
        int received=httpd_req_recv(req,body+count,req->content_len-count);
        if(received<=0){memset(body,0,sizeof(body));return error_response(req,"408 Request Timeout","{\"error\":\"body_timeout\"}");}
        count+=(size_t)received;
    }
    body[count]=0;
    cJSON *root=memchr(body,0,count)==NULL&&strstr(body,"\\u0000")==NULL&&flat_json(body,count)?cJSON_ParseWithLengthOpts(body,count+1,NULL,true):NULL;
    memset(body,0,sizeof(body));
    cJSON *gen=cJSON_GetObjectItemCaseSensitive(root,"generation"),*id=cJSON_GetObjectItemCaseSensitive(root,"id"),*password=cJSON_GetObjectItemCaseSensitive(root,"password");
    esp_err_t result=ESP_ERR_INVALID_ARG;
    if(cJSON_IsObject(root)&&cJSON_GetArraySize(root)==3&&cJSON_IsNumber(gen)&&gen->valuedouble>=0&&gen->valuedouble<=UINT32_MAX&&
       gen->valuedouble==(double)(uint32_t)gen->valuedouble&&cJSON_IsNumber(id)&&id->valuedouble>=0&&id->valuedouble<CHIME_SCAN_MAX&&
       id->valuedouble==(double)(unsigned)id->valuedouble&&cJSON_IsString(password))
        result=chime_network_submit((uint32_t)gen->valuedouble,(unsigned)id->valuedouble,password->valuestring);
    if(cJSON_IsString(password))memset(password->valuestring,0,strlen(password->valuestring));
    cJSON_Delete(root);
    if(result!=ESP_OK)return error_response(req,result==ESP_ERR_INVALID_STATE?"409 Conflict":"400 Bad Request","{\"error\":\"invalid_or_stale_selection_password_or_busy\"}");
    httpd_resp_set_status(req,"202 Accepted");headers(req,"application/json; charset=utf-8");
    return httpd_resp_send(req,"{\"accepted\":true}",HTTPD_RESP_USE_STRLEN);
}
/* Manual trusted-LAN bench control. A custom header + exact Origin/Host
 * blocks cross-site simple requests and DNS rebinding; no CORS is enabled. */
static esp_err_t tuning_post(httpd_req_t *req)
{
    chime_network_status_t n;chime_network_snapshot(&n);
    if(!n.online||chime_network_setup_request(httpd_req_to_sockfd(req)))
        return error_response(req,"403 Forbidden","{\"error\":\"sta_only\"}");
    char host[96],origin[112],expected[112],ip80[24],type[48],confirm[8];
    snprintf(ip80,sizeof(ip80),"%s:80",n.ip);
    if(httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host))!=ESP_OK||
       (strcmp(host,n.ip)&&strcmp(host,ip80)&&strcmp(host,"chime.local")&&strcmp(host,"chime.local:80"))||
       httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))!=ESP_OK)
        return error_response(req,"403 Forbidden","{\"error\":\"invalid_origin\"}");
    snprintf(expected,sizeof(expected),"http://%s",host);
    if(strcmp(origin,expected)||httpd_req_get_hdr_value_str(req,"X-Chime-Tuning",confirm,sizeof(confirm))!=ESP_OK||strcmp(confirm,"1"))
        return error_response(req,"403 Forbidden","{\"error\":\"manual_same_origin_required\"}");
    if(httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json"))
        return error_response(req,"415 Unsupported Media Type","{\"error\":\"json_required\"}");
    if(req->content_len!=1)return error_response(req,"400 Bad Request","{\"error\":\"invalid_tuning_command\"}");
    char value;
    if(httpd_req_recv(req,&value,1)!=1)return error_response(req,"408 Request Timeout","{\"error\":\"body_timeout\"}");
    if(value<'1'||value>'4')return error_response(req,"400 Bad Request","{\"error\":\"invalid_tuning_command\"}");
    int idle=0;
    if((value!='4'&&!atomic_load(&dsp_external))||atomic_load(&output_request)||
       atomic_load(&sound_request)||atomic_load(&dsp_request)||
       !atomic_compare_exchange_strong(&tuning_request,&idle,value-'0'))
        return error_response(req,"409 Conflict","{\"error\":\"dsp_not_ready_or_busy\"}");
    httpd_resp_set_status(req,"202 Accepted");headers(req,"application/json; charset=utf-8");
    return httpd_resp_send(req,"{\"queued\":true}",HTTPD_RESP_USE_STRLEN);
}
int bench_web_take_tuning_request(void)
{
    int value=atomic_load(&tuning_request);
    if(value>0&&atomic_compare_exchange_strong(&tuning_request,&value,-value))return value;
    return 0;
}
bool bench_web_tuning_queued(void){return atomic_load(&tuning_request)>0;}
bool bench_web_tuning_pending(void){return atomic_load(&tuning_request)!=0;}
void bench_web_tuning_publish(bool external,unsigned armed,int result)
{
    atomic_store(&dsp_external,external);atomic_store(&tuning_armed,armed);
    atomic_store(&tuning_result,result);atomic_store(&tuning_request,0);
}
void bench_web_tuning_revoke(bool external,int result)
{
    atomic_store(&dsp_external,external);atomic_store(&tuning_armed,0);atomic_store(&tuning_result,result);
    int request=atomic_load(&tuning_request);
    while(request>0&&request<4&&!atomic_compare_exchange_weak(&tuning_request,&request,0)){}
}
static esp_err_t output_post(httpd_req_t *req)
{
    chime_network_status_t n;chime_network_snapshot(&n);
    if(!n.online||chime_network_setup_request(httpd_req_to_sockfd(req)))
        return error_response(req,"403 Forbidden","{\"error\":\"sta_only\"}");
    char host[96],origin[112],expected[112],ip80[24],type[48],confirm[8];
    snprintf(ip80,sizeof(ip80),"%s:80",n.ip);
    if(httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host))!=ESP_OK||
       (strcmp(host,n.ip)&&strcmp(host,ip80)&&strcmp(host,"chime.local")&&strcmp(host,"chime.local:80"))||
       httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))!=ESP_OK)
        return error_response(req,"403 Forbidden","{\"error\":\"invalid_origin\"}");
    snprintf(expected,sizeof(expected),"http://%s",host);
    if(strcmp(origin,expected)||httpd_req_get_hdr_value_str(req,"X-Chime-Output",confirm,sizeof(confirm))!=ESP_OK||strcmp(confirm,"1"))
        return error_response(req,"403 Forbidden","{\"error\":\"manual_same_origin_required\"}");
    if(httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json"))
        return error_response(req,"415 Unsupported Media Type","{\"error\":\"json_required\"}");
    if(req->content_len!=1)return error_response(req,"400 Bad Request","{\"error\":\"invalid_output_mode\"}");
    char value=0;
    if(httpd_req_recv(req,&value,1)!=1)return error_response(req,"408 Request Timeout","{\"error\":\"body_timeout\"}");
    unsigned mode=(unsigned)(value-'0');
    if(!bench_output_valid(mode))return error_response(req,"400 Bad Request","{\"error\":\"invalid_output_mode\"}");
    int idle=0;
    /* Only the audited built-in map may be changed. USB playback can be
     * interrupted explicitly; SS+ owns all external DSP parameters. */
    if(bench_web_tuning_pending()||!atomic_load(&default_ready)||atomic_load(&client_connected)||
       atomic_load(&sound_request)!=0||atomic_load(&dsp_request)!=0||
       !atomic_compare_exchange_strong(&output_request,&idle,(int)mode))
        return error_response(req,"409 Conflict","{\"error\":\"dsp_not_ready_or_busy\"}");
    httpd_resp_set_status(req,"202 Accepted");headers(req,"application/json; charset=utf-8");
    return httpd_resp_send(req,"{\"queued\":true}",HTTPD_RESP_USE_STRLEN);
}
int bench_web_take_output_request(void)
{
    int request=atomic_load(&output_request);
    if(request>0&&atomic_compare_exchange_strong(&output_request,&request,-request))return request;
    return 0;
}
bool bench_web_output_change_pending(void){return atomic_load(&output_request)>0;}
void bench_web_output_publish(unsigned mode,int result)
{
    if(result==ESP_OK)atomic_store(&output_mode,mode);
    atomic_store(&output_result,result);atomic_store(&output_request,0);
}
static esp_err_t sound_post(httpd_req_t *req)
{
    chime_network_status_t n;chime_network_snapshot(&n);
    if(!n.online||chime_network_setup_request(httpd_req_to_sockfd(req)))
        return error_response(req,"403 Forbidden","{\"error\":\"sta_only\"}");
    char host[96],origin[112],expected[112],ip80[24],type[48],confirm[8];
    snprintf(ip80,sizeof(ip80),"%s:80",n.ip);
    if(httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host))!=ESP_OK||
       (strcmp(host,n.ip)&&strcmp(host,ip80)&&strcmp(host,"chime.local")&&strcmp(host,"chime.local:80"))||
       httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))!=ESP_OK)
        return error_response(req,"403 Forbidden","{\"error\":\"invalid_origin\"}");
    snprintf(expected,sizeof(expected),"http://%s",host);
    if(strcmp(origin,expected)||httpd_req_get_hdr_value_str(req,"X-Chime-Sound",confirm,sizeof(confirm))!=ESP_OK||strcmp(confirm,"1"))
        return error_response(req,"403 Forbidden","{\"error\":\"manual_same_origin_required\"}");
    if(httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json"))
        return error_response(req,"415 Unsupported Media Type","{\"error\":\"json_required\"}");
    if(req->content_len<1||req->content_len>160)
        return error_response(req,"400 Bad Request","{\"error\":\"invalid_tone_body\"}");
    char body[161];size_t received=0;
    while(received<req->content_len){
        int count=httpd_req_recv(req,body+received,req->content_len-received);
        if(count<=0)return error_response(req,"408 Request Timeout","{\"error\":\"body_timeout\"}");
        received+=(size_t)count;
    }
    body[received]=0;
    if(memchr(body,0,received))return error_response(req,"400 Bad Request","{\"error\":\"invalid_tone_body\"}");
    int command=0;
    if(received==8&&!strcmp(body,"\"resume\""))command=-2;
    else if(received==1&&body[0]>='0'&&body[0]<='3')command=body[0]-'0';
    else {
        const char *end=NULL;
        cJSON *root=cJSON_ParseWithLengthOpts(body,received+1,&end,true);
        cJSON *channel=cJSON_GetObjectItemCaseSensitive(root,"channel");
        cJSON *hz=cJSON_GetObjectItemCaseSensitive(root,"frequency_hz");
        cJSON *db=cJSON_GetObjectItemCaseSensitive(root,"level_dbfs");
        cJSON *sec=cJSON_GetObjectItemCaseSensitive(root,"seconds");
        bool valid=cJSON_IsObject(root)&&cJSON_GetArraySize(root)==4&&
            cJSON_IsNumber(channel)&&cJSON_IsNumber(hz)&&cJSON_IsNumber(db)&&cJSON_IsNumber(sec)&&
            channel->valuedouble>=1&&channel->valuedouble<=3&&channel->valuedouble==channel->valueint&&
            hz->valuedouble>=40&&hz->valuedouble<=10000&&hz->valuedouble==hz->valueint&&
            db->valuedouble>=-60&&db->valuedouble<=0&&db->valuedouble==db->valueint&&
            sec->valuedouble>=1&&sec->valuedouble<=3&&sec->valuedouble==sec->valueint;
        if(valid){
            bench_tone_config_t c={(unsigned)hz->valueint,db->valueint,(unsigned)sec->valueint};
            command=bench_tone_command((unsigned)channel->valueint,c,++sound_tag);
        }
        cJSON_Delete(root);
        if(!valid)return error_response(req,"400 Bad Request","{\"error\":\"tone_out_of_range\"}");
    }
    if(command==0)atomic_store(&sound_request,-1); /* Stop overrides pending start. */
    else {
        /* Retire old evidence before publishing a new mailbox command. */
        bench_web_sound_completed(0);
        int expected_request=0;
        if(bench_web_tuning_pending()||!atomic_load(&default_ready)||atomic_load(&client_connected)||atomic_load(&output_request)!=0||atomic_load(&sound_active)||
           !atomic_compare_exchange_strong(&sound_request,&expected_request,command))
            return error_response(req,"409 Conflict","{\"error\":\"dsp_not_ready_or_busy\"}");
    }
    char response[80];snprintf(response,sizeof(response),"{\"queued\":true,\"command\":%d}",command);
    httpd_resp_set_status(req,"202 Accepted");headers(req,"application/json; charset=utf-8");
    return httpd_resp_send(req,response,HTTPD_RESP_USE_STRLEN);
}
static esp_err_t dsp_post(httpd_req_t *req)
{
    chime_network_status_t n;chime_network_snapshot(&n);
    if(!n.online||chime_network_setup_request(httpd_req_to_sockfd(req)))
        return error_response(req,"403 Forbidden","{\"error\":\"sta_only\"}");
    char host[96],origin[112],expected[112],ip80[24],type[48],confirm[8];
    snprintf(ip80,sizeof(ip80),"%s:80",n.ip);
    if(httpd_req_get_hdr_value_str(req,"Host",host,sizeof(host))!=ESP_OK||
       (strcmp(host,n.ip)&&strcmp(host,ip80)&&strcmp(host,"chime.local")&&strcmp(host,"chime.local:80"))||
       httpd_req_get_hdr_value_str(req,"Origin",origin,sizeof(origin))!=ESP_OK)
        return error_response(req,"403 Forbidden","{\"error\":\"invalid_origin\"}");
    snprintf(expected,sizeof(expected),"http://%s",host);
    if(strcmp(origin,expected)||httpd_req_get_hdr_value_str(req,"X-Chime-DSP",confirm,sizeof(confirm))!=ESP_OK||strcmp(confirm,"1"))
        return error_response(req,"403 Forbidden","{\"error\":\"manual_same_origin_required\"}");
    if(httpd_req_get_hdr_value_str(req,"Content-Type",type,sizeof(type))!=ESP_OK||strcmp(type,"application/json"))
        return error_response(req,"415 Unsupported Media Type","{\"error\":\"json_required\"}");
    if(req->content_len!=1)return error_response(req,"400 Bad Request","{\"error\":\"read_only_command_1\"}");
    char command;
    if(httpd_req_recv(req,&command,1)!=1)return error_response(req,"408 Request Timeout","{\"error\":\"body_timeout\"}");
    if(command!='1')return error_response(req,"400 Bad Request","{\"error\":\"read_only_command_1\"}");
    int idle=0;
    if(bench_web_tuning_pending()||!dsp_available()||atomic_load(&output_request)!=0||atomic_load(&sound_active)||atomic_load(&sound_request)!=0||
       !atomic_compare_exchange_strong(&dsp_request,&idle,1))
        return error_response(req,"409 Conflict","{\"error\":\"dsp_not_ready_or_busy\"}");
    httpd_resp_set_status(req,"202 Accepted");headers(req,"application/json; charset=utf-8");
    return httpd_resp_send(req,"{\"queued\":true}",HTTPD_RESP_USE_STRLEN);
}
int bench_web_take_sound_request(void){return atomic_exchange(&sound_request,0);}
void bench_web_sound_completed(int command){atomic_store(&sound_completed_command,command);}
bool bench_web_sound_stop_pending(void){return atomic_load(&sound_request)==-1;}
void bench_web_sound_status(bool active,bool attempted,int result,uint16_t failed_reg)
{
    atomic_store(&sound_attempted,attempted);atomic_store(&sound_result,result);atomic_store(&sound_failed_reg,failed_reg);atomic_store(&sound_active,active);
}
void bench_web_set_client(bool connected)
{
    if(atomic_load(&client_connected)!=connected) {
        /* Close admission before invalidation; publish a new connection last. */
        atomic_store(&client_connected,false);
        atomic_store(&dsp_current,false);atomic_store(&dsp_request,0);
        atomic_store(&client_connected,connected);
    }
}
void bench_web_record_request(int result){atomic_store(&last_result,result);atomic_fetch_add(&requests,1);}
void bench_web_stop(void)
{
    atomic_store(&output_request,0);atomic_store(&output_result,0);
    bench_web_tuning_publish(false,0,0);
    atomic_store(&default_ready,false);bench_web_set_client(false);atomic_store(&dsp_request,0);atomic_store(&dsp_current,false);atomic_store(&sound_request,0);bench_web_sound_completed(0);bench_web_sound_status(false,false,0,0);if(server){httpd_stop(server);server=NULL;}
    if(mdns_started){mdns_free();mdns_started=false;}
}
esp_err_t bench_web_start(uint16_t tcp_port)
{
    if(server||mdns_started)return ESP_ERR_INVALID_STATE;
    if(!tcp_port)return ESP_ERR_INVALID_ARG;
    bench_web_tuning_publish(false,0,0);
    atomic_store(&default_ready,false);
    atomic_store(&output_mode,BENCH_OUTPUT_DEFAULT);atomic_store(&output_request,0);atomic_store(&output_result,0);
    atomic_store(&sound_request,0);bench_web_sound_completed(0);bench_web_sound_status(false,false,0,0);
    atomic_store(&dsp_generation,0);atomic_store(&dsp_sequence,0);
    atomic_store(&dsp_result,0);atomic_store(&dsp_complete,false);atomic_store(&dsp_captured_ms,0);
    atomic_store(&dsp_current,false);atomic_store(&dsp_request,0);
    bridge_port=tcp_port;atomic_store(&client_connected,false);atomic_store(&requests,0);atomic_store(&last_result,0);
    esp_err_t err=mdns_init();if(err!=ESP_OK)goto fail;mdns_started=true;
    if((err=mdns_hostname_set(CHIME_HOSTNAME))!=ESP_OK)goto fail;
    if((err=mdns_instance_name_set("Chime"))!=ESP_OK)goto fail;
    mdns_txt_item_t http_txt[]={{"path","/"}};
    if((err=mdns_service_add(NULL,"_http","_tcp",80,http_txt,1))!=ESP_OK)goto fail;
    mdns_txt_item_t tcp_txt[]={{"target","ADAU1451"},{"chip","70"},{"channel","0"}};
    if((err=mdns_service_add("SigmaStudio+ 3.4.0","_sigma-dsp","_tcp",tcp_port,tcp_txt,3))!=ESP_OK)goto fail;
    httpd_config_t config=HTTPD_DEFAULT_CONFIG();config.server_port=80;config.max_open_sockets=4;config.max_uri_handlers=10;
    config.stack_size=8192;config.lru_purge_enable=true;config.recv_wait_timeout=2;config.send_wait_timeout=2;
    if((err=httpd_start(&server,&config))!=ESP_OK)goto fail;
    const httpd_uri_t routes[]={
        {.uri="/",.method=HTTP_GET,.handler=index_get},
        {.uri="/api/status",.method=HTTP_GET,.handler=status_get},
        {.uri="/api/network",.method=HTTP_GET,.handler=network_get},
        {.uri="/api/scan",.method=HTTP_GET,.handler=scan_get},
        {.uri="/api/wifi",.method=HTTP_POST,.handler=wifi_post},
        {.uri="/api/sound",.method=HTTP_POST,.handler=sound_post},
        {.uri="/api/dsp/status",.method=HTTP_GET,.handler=dsp_get},
        {.uri="/api/dsp/status",.method=HTTP_POST,.handler=dsp_post},
        {.uri="/api/output",.method=HTTP_POST,.handler=output_post},
        {.uri="/api/tuning",.method=HTTP_POST,.handler=tuning_post},
    };
    for(size_t i=0;i<sizeof(routes)/sizeof(routes[0]);++i)if((err=httpd_register_uri_handler(server,routes+i))!=ESP_OK)goto fail;
    if((err=httpd_register_err_handler(server,HTTPD_404_NOT_FOUND,portal_redirect))!=ESP_OK)goto fail;
    ESP_LOGI(TAG,"WEB http://chime.local/ | setup fallback http://" CHIME_AP_IP "/ | TCP %u (STA only)",(unsigned)tcp_port);
    return ESP_OK;
fail:
    ESP_LOGE(TAG,"HTTP/mDNS startup failed: %s",esp_err_to_name(err));bench_web_stop();return err;
}

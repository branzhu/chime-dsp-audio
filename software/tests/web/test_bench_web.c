#define _CRT_SECURE_NO_WARNINGS

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "bench_web.h"
#include "bench_tone.h"
#include "chime_network.h"
#include "chime_setup_guard.h"
#include "mdns.h"
#include "esp_http_server.h"
#include "cJSON.h"
const char index_start[]="<html><code id=ip>{{DEVICE_IP}}</code></html>";
static int handle;
static unsigned calls,fail_at,mdns_stops,http_stops,route_count,service_count,scan_requests,submit_calls;
static bool mdns_active,http_active,ap_request=true,no_cache,nosniff,csp;
static httpd_uri_t routes[10];
static esp_err_t (*redirect_handler)(httpd_req_t *,httpd_err_code_t);
static char body[8192],content_type[64],location[128];
static const char *resolved_hostname="chime",*input="",*token="test-token",*origin="http://192.168.4.1",*mime="application/json",*host="192.168.4.1";
static size_t input_offset;
static const char *sound_header="1",*dsp_header="1",*output_header="1";
static int status_code=200,submit_result;
static chime_network_status_t network;
static struct sockaddr_in6 local_address;
static int next(void){return ++calls==fail_at?-77:0;}
const char *esp_err_to_name(int error){(void)error;return "test error";}
void chime_network_snapshot(chime_network_status_t *out){*out=network;}
bool chime_network_setup_request(int fd){assert(fd==7);return ap_request&&network.setup&&chime_setup_address_is_ap((const struct sockaddr *)&local_address,sizeof(local_address));}
void chime_network_request_scan(void){++scan_requests;}
esp_err_t chime_network_submit(uint32_t gen,unsigned id,const char *password){assert(gen==7&&id==0);assert(strcmp(password,"password1")==0);++submit_calls;return submit_result;}
esp_err_t mdns_init(void){int e=next();if(!e)mdns_active=true;return e;}
esp_err_t mdns_hostname_get(char *out){snprintf(out,MDNS_NAME_BUF_LEN,"%s",resolved_hostname);return next();}
void mdns_free(void){assert(mdns_active);mdns_active=false;++mdns_stops;}
esp_err_t mdns_hostname_set(const char *name){assert(strcmp(name,"chime")==0);return next();}
esp_err_t mdns_instance_name_set(const char *name){assert(strcmp(name,"Chime")==0);return next();}
esp_err_t mdns_service_add(const char *instance,const char *service,const char *protocol,uint16_t port,mdns_txt_item_t txt[],size_t count){
    assert(strcmp(protocol,"_tcp")==0);
    if(service_count++==0){assert(!instance&&strcmp(service,"_http")==0&&port==80&&count==1);assert(strcmp(txt[0].value,"/")==0);}
    else {assert(strcmp(instance,"SigmaStudio+ 3.4.0")==0&&strcmp(service,"_sigma-dsp")==0&&port==8086&&count==3);assert(strcmp(txt[1].value,"70")==0);}
    return next();
}
esp_err_t httpd_start(httpd_handle_t *out,const httpd_config_t *c){assert(c->max_open_sockets==4&&c->max_uri_handlers==10&&c->stack_size==8192&&c->server_port==80&&c->lru_purge_enable);assert(c->recv_wait_timeout==2&&c->send_wait_timeout==2);int e=next();if(!e){http_active=true;*out=&handle;}return e;}
esp_err_t httpd_stop(httpd_handle_t h){assert(h==&handle&&http_active);http_active=false;++http_stops;return 0;}
esp_err_t httpd_register_uri_handler(httpd_handle_t h,const httpd_uri_t *uri){assert(h==&handle&&route_count<10);routes[route_count++]=*uri;return next();}
esp_err_t httpd_register_err_handler(httpd_handle_t h,httpd_err_code_t code,esp_err_t (*handler)(httpd_req_t *,httpd_err_code_t)){assert(h==&handle&&code==404);redirect_handler=handler;return next();}
esp_err_t httpd_resp_set_type(httpd_req_t *req,const char *type){(void)req;snprintf(content_type,sizeof(content_type),"%s",type);return 0;}
esp_err_t httpd_resp_set_status(httpd_req_t *req,const char *status){(void)req;status_code=atoi(status);return 0;}
esp_err_t httpd_resp_set_hdr(httpd_req_t *req,const char *key,const char *value){
    (void)req;
    if(strcmp(key,"Cache-Control")==0){assert(strcmp(value,"no-store")==0);no_cache=true;}
    else if(strcmp(key,"X-Content-Type-Options")==0){assert(strcmp(value,"nosniff")==0);nosniff=true;}
    else if(strcmp(key,"Content-Security-Policy")==0){assert(strstr(value,"frame-ancestors 'none'"));csp=true;}
    else if(strcmp(key,"Location")==0)snprintf(location,sizeof(location),"%s",value);
    else assert(false);
    return 0;
}
esp_err_t httpd_resp_send_chunk(httpd_req_t *req,const char *data,ssize_t length){(void)req;if(!data){assert(length==0);return 0;}size_t n=length<0?strlen(data):(size_t)length;size_t offset=strlen(body);assert(offset+n<sizeof(body));memcpy(body+offset,data,n);body[offset+n]=0;return 0;}
esp_err_t httpd_resp_send(httpd_req_t *req,const char *data,ssize_t length){body[0]=0;return httpd_resp_send_chunk(req,data,length);}
int httpd_req_to_sockfd(httpd_req_t *req){(void)req;return 7;}
static const char *tuning_header="1";
static const char *header(const char *name){if(strcmp(name,"X-Chime-Tuning")==0)return tuning_header;if(strcmp(name,"X-Chime-Output")==0)return output_header;if(strcmp(name,"X-Chime-DSP")==0)return dsp_header;if(strcmp(name,"X-Chime-Sound")==0)return sound_header;if(strcmp(name,"X-Chime-Token")==0)return token;if(strcmp(name,"Origin")==0)return origin;if(strcmp(name,"Host")==0)return host;if(strcmp(name,"Content-Type")==0)return mime;return "";}
size_t httpd_req_get_hdr_value_len(httpd_req_t *req,const char *name){(void)req;return strlen(header(name));}
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req,const char *name,char *out,size_t n){(void)req;const char *v=header(name);if(!*v||strlen(v)>=n)return ESP_FAIL;memcpy(out,v,strlen(v)+1);return 0;}
int httpd_req_recv(httpd_req_t *req,char *out,size_t size){(void)req;size_t n=strlen(input)-input_offset;if(n>size)n=size;if(n>7)n=7;memcpy(out,input+input_offset,n);input_offset+=n;return (int)n;}
static void reset(void){assert(!mdns_active&&!http_active);calls=fail_at=mdns_stops=http_stops=route_count=service_count=0;}
static int request(unsigned route,const char *data){input=data;input_offset=0;status_code=200;body[0]=0;httpd_req_t req={.content_len=strlen(data),.uri=routes[route].uri};assert(routes[route].handler(&req)==0);return status_code;}
static void rejected(const char *data){unsigned prior=submit_calls;assert(request(4,data)==400);assert(submit_calls==prior);}
int main(void){
    const unsigned char mapped_ap[]={0,0,0,0,0,0,0,0,0,0,255,255,192,168,4,1};
    local_address.sin6_family=AF_INET6;memcpy(&local_address.sin6_addr,mapped_ap,16);
    reset();bench_web_stop();assert(bench_web_start(0)==ESP_ERR_INVALID_ARG&&calls==0);
    for(unsigned i=1;i<=17;++i){reset();fail_at=i;assert(bench_web_start(8086)==-77);assert(!mdns_active&&!http_active);assert(mdns_stops==(i>1?1U:0U));assert(http_stops==(i>6?1U:0U));bench_web_stop();}
    puts("PASS all 17 mDNS/HTTP startup fault stages roll back; invalid start and idempotent stop");
    reset();assert(bench_web_start(8086)==0&&route_count==10&&service_count==2);assert(bench_web_start(8086)==ESP_ERR_INVALID_STATE);assert(routes[4].method==HTTP_POST);
    network=(chime_network_status_t){.setup=true,.generation=7,.count=1};strcpy(network.ip,"192.168.4.1");strcpy(network.phase,"setup");strcpy(network.token,"test-token");strcpy(network.ap_ssid,"Chime-Setup-1234");strcpy(network.networks[0].ssid,"<img onerror=alert(1)>");network.networks[0].supported=true;
    assert(request(0,"")==200&&strstr(body,">192.168.4.1</code>")&&!strstr(body,"{{DEVICE_IP}}"));assert(no_cache&&nosniff&&csp);
    bench_web_set_client(true);bench_web_record_request(-105);assert(request(1,"")==200&&strstr(body,"\"requests\":1")&&strstr(body,"\"last_result\":-105"));
    resolved_hostname="chime-2";assert(request(1,"")==200&&strstr(body,"chime-2.local"));resolved_hostname="chime";
    assert(request(2,"")==200&&strstr(body,"test-token")&&!strstr(body,"password"));
    assert(request(3,"")==200&&scan_requests==0&&strstr(body,"<img onerror=alert(1)>"));cJSON *json=cJSON_Parse(body);assert(json);cJSON_Delete(json);
    assert(strstr(body,"\"error\":0")&&strstr(body,"\"error_name\":"));
    network.scan_error=263;assert(request(3,"")==200&&strstr(body,"\"error\":263")&&scan_requests==0);network.scan_error=0;
    httpd_req_t scan_req={.uri="/api/scan?refresh=1"};assert(routes[3].handler(&scan_req)==0&&scan_requests==1);
    httpd_req_t req={0};status_code=0;assert(redirect_handler(&req,404)==0&&status_code==302&&strcmp(location,"http://192.168.4.1/")==0);host="example.com";assert(request(0,"")==302);host="192.168.4.1";
    const char *good="{\"generation\":7,\"id\":0,\"password\":\"password1\"}";
    assert(request(4,good)==202&&submit_calls==1);submit_result=ESP_ERR_INVALID_STATE;assert(request(4,good)==409);submit_result=ESP_ERR_INVALID_ARG;assert(request(4,good)==400);submit_result=0;
    token="wrong";assert(request(4,good)==403);token="test-token";origin="http://evil.test";assert(request(4,good)==403);origin="http://192.168.4.1";mime="text/plain";assert(request(4,good)==415);mime="application/json";
    rejected("{}");rejected("null");rejected("[]");rejected("{\"generation\":7,\"id\":0,\"password\":\"password1\",\"extra\":1}");
    rejected("{\"generation\":-1,\"id\":0,\"password\":\"password1\"}");rejected("{\"generation\":1.5,\"id\":0,\"password\":\"password1\"}");rejected("{\"generation\":4294967296,\"id\":0,\"password\":\"password1\"}");
    rejected("{\"generation\":7,\"id\":20,\"password\":\"password1\"}");rejected("{\"generation\":7,\"id\":0,\"password\":\"pass\\u0000word1\"}");rejected("{\"generation\":7,\"id\":0,\"password\":null}");
    rejected("{\"generation\":{},\"id\":0,\"password\":\"password1\"}");
    char nested[512];memset(nested,'[',250);memset(nested+250,']',250);nested[500]=0;rejected(nested);
    char huge[514];memset(huge,'x',513);huge[513]=0;assert(request(4,huge)==413);assert(request(4,"")==413);
    input="";input_offset=0;req.content_len=100;assert(routes[4].handler(&req)==0&&status_code==408);
    /* Exact same actual guard as firmware: mapped STA destination must not
     * receive tokens, scan results, credential writes or captive redirects. */
    ((unsigned char *)&local_address.sin6_addr)[15]=50;
    assert(request(2,"")==200&&!strstr(body,"test-token"));assert(request(3,"")==403&&request(4,good)==403);
    status_code=0;assert(redirect_handler(&req,404)==0&&status_code==404);
    memcpy(&local_address.sin6_addr,mapped_ap,16);
    ap_request=false;assert(request(2,"")==200&&!strstr(body,"test-token"));assert(request(3,"")==403&&request(4,good)==403);status_code=0;assert(redirect_handler(&req,404)==0&&status_code==404);
    network.setup=false;network.online=true;strcpy(network.ip,"10.23.4.5");assert(request(0,"")==200&&strstr(body,"10.23.4.5"));
    network.setup=false;network.online=true;strcpy(network.ip,"192.168.31.135");host="192.168.31.135";origin="http://192.168.31.135";
    bench_web_set_client(false);assert(request(5,"1")==409);
    bench_web_set_client(true);assert(request(5,"1")==409);bench_web_set_client(false);bench_web_set_default_ready(true);
    assert(request(5,"\"resume\"")==202&&!bench_web_sound_stop_pending());assert(bench_web_take_sound_request()==-2);
    assert(request(5,"1")==202);assert(request(5,"2")==409);assert(bench_web_take_sound_request()==1);
    assert(request(5,"2")==202);assert(request(5,"0")==202);assert(bench_web_sound_stop_pending());assert(bench_web_take_sound_request()==-1&&bench_web_take_sound_request()==0);
    bench_web_sound_status(false,false,0,0);
    assert(request(1,"")==200&&strstr(body,"\"sound_attempted\":false")&&strstr(body,"\"sound_result\":0"));
    bench_web_sound_status(true,true,0,0);assert(request(5,"3")==409);assert(request(1,"")==200&&strstr(body,"\"amplifiers_enabled\":true"));
    bench_web_sound_status(false,true,263,0xf580);assert(request(1,"")==200&&strstr(body,"\"sound_failed_reg\":62848")&&strstr(body,"\"sound_attempted\":true"));
    assert(request(5,"4")==400&&request(5,"{}")==400&&request(5,"")==400);
    sound_header="";assert(request(5,"1")==403);sound_header="1";
    origin="http://evil.test";assert(request(5,"1")==403);origin="http://192.168.31.135";
    host="evil.test";assert(request(5,"1")==403);host="192.168.31.135";
    mime="text/plain";assert(request(5,"1")==415);mime="application/json";
    network.online=false;assert(request(5,"1")==403);network.online=true;
    assert(bench_web_take_sound_request()==0);
    assert(request(5,"{\"channel\":2,\"frequency_hz\":137,\"level_dbfs\":-48,\"seconds\":2}")==202);
    cJSON *ack=cJSON_Parse(body);int calibration_command=cJSON_GetObjectItemCaseSensitive(ack,"command")->valueint;cJSON_Delete(ack);
    assert(calibration_command>3&&bench_web_take_sound_request()==calibration_command);
    bench_tone_config_t config=bench_tone_command_config(calibration_command);
    assert(((unsigned)calibration_command&3U)==2&&config.frequency_hz==137&&config.level_dbfs==-48&&config.seconds==2);
    bench_web_sound_completed(calibration_command);
    assert(request(1,"")==200);ack=cJSON_Parse(body);
    assert(cJSON_GetObjectItemCaseSensitive(ack,"sound_completed_command")->valueint==calibration_command);cJSON_Delete(ack);
    bench_web_sound_completed(0);
    const char *bad_tones[]={
        "{\"channel\":1,\"frequency_hz\":39,\"level_dbfs\":-48,\"seconds\":3}",
        "{\"channel\":1,\"frequency_hz\":10001,\"level_dbfs\":-48,\"seconds\":3}",
        "{\"channel\":1,\"frequency_hz\":1000,\"level_dbfs\":1,\"seconds\":3}",
        "{\"channel\":1,\"frequency_hz\":1000,\"level_dbfs\":-61,\"seconds\":3}",
        "{\"channel\":1,\"frequency_hz\":1000,\"level_dbfs\":-48,\"seconds\":4}",
        "{\"channel\":1,\"frequency_hz\":1000.5,\"level_dbfs\":-48,\"seconds\":3}",
        "{\"channel\":0,\"frequency_hz\":1000,\"level_dbfs\":-48,\"seconds\":3}",
        "{\"channel\":1,\"frequency_hz\":1000,\"level_dbfs\":-48,\"seconds\":3,\"extra\":1}",
        "{\"channel\":1,\"frequency_hz\":1000,\"level_dbfs\":-48,\"seconds\":3} trailing",
        "{\"channel\":1,\"frequency_hz\":1000,\"level_dbfs\":\"-48\",\"seconds\":3}","null","[]"
    };
    for(unsigned i=0;i<sizeof(bad_tones)/sizeof(bad_tones[0]);++i)assert(request(5,bad_tones[i])==400);
    assert(bench_web_take_sound_request()==0);
    assert(request(5,"{\"channel\":1,\"frequency_hz\":40,\"level_dbfs\":-60,\"seconds\":1}")==202);
    assert(request(5,"0")==202&&bench_web_take_sound_request()==-1);
    assert(request(5,"{\"channel\":1,\"frequency_hz\":1000,\"level_dbfs\":0,\"seconds\":1}")==202);
    config=bench_tone_command_config(bench_web_take_sound_request());assert(config.level_dbfs==0);
    puts("PASS calibration JSON: chunked body, accepted parameter token, completed token, numeric/type/size/strict-object/trailing-data bounds and stop override");
    puts("PASS manual sine mailbox: STA/client gating, channel bounds, busy/stop priority, real status, exact Origin/Host/custom-header and content-type");
    bench_web_set_default_ready(false);bench_web_set_client(true);
    assert(request(6,"")==200&&strstr(body,"\"present\":false"));
    assert(request(7,"1")==202&&request(7,"1")==409);
    assert(request(6,"")==200&&strstr(body,"\"busy\":true"));
    assert(bench_web_take_dsp_request()&&!bench_web_take_dsp_request());
    assert(request(7,"1")==409); /* Running, not merely queued, is busy. */
    bench_dsp_status_t dsp={.complete=true,.captured_ms=1234};
    for(unsigned i=0;i<BENCH_DSP_STATUS_COUNT;++i)dsp.registers[i]=(bench_dsp_status_value_t){.valid=true,.value=(uint16_t)i};
    dsp.registers[14].value=0;bench_web_dsp_publish(&dsp);
    assert(request(6,"")==200&&strstr(body,"\"generation\":1")&&strstr(body,"\"current\":true"));
    assert(strstr(body,"\"busy\":false")&&strstr(body,"\"captured_ms\":1234"));
    cJSON *dsp_json=cJSON_Parse(body);assert(dsp_json);
    cJSON *values=cJSON_GetObjectItemCaseSensitive(dsp_json,"registers");assert(cJSON_GetArraySize(values)==BENCH_DSP_STATUS_COUNT);
    cJSON *asrc=cJSON_GetArrayItem(values,14);
    assert(cJSON_GetObjectItemCaseSensitive(asrc,"address")->valuedouble==0xf580);
    assert(cJSON_IsTrue(cJSON_GetObjectItemCaseSensitive(asrc,"valid"))&&cJSON_GetObjectItemCaseSensitive(asrc,"value")->valuedouble==0);
    cJSON *lr=cJSON_GetArrayItem(values,17),*sub=cJSON_GetArrayItem(values,18);
    assert(cJSON_GetObjectItemCaseSensitive(lr,"address")->valuedouble==0xf188);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(lr,"name")->valuestring,"SOUT_SOURCE8_LR")==0);
    assert(cJSON_GetObjectItemCaseSensitive(sub,"address")->valuedouble==0xf190);
    assert(strcmp(cJSON_GetObjectItemCaseSensitive(sub,"name")->valuestring,"SOUT_SOURCE16_SUB")==0);
    cJSON_Delete(dsp_json);
    bench_web_dsp_invalidate();assert(request(6,"")==200&&strstr(body,"\"current\":false"));
    dsp=(bench_dsp_status_t){.result=ESP_ERR_TIMEOUT};bench_web_dsp_publish(&dsp);
    assert(request(6,"")==200&&strstr(body,"\"generation\":2")&&!strstr(body,"\"value\":"));
    bench_web_set_client(false);assert(request(7,"1")==409);
    assert(request(6,"")==200&&strstr(body,"\"current\":false"));
    bench_web_set_client(true);assert(request(6,"")==200&&strstr(body,"\"current\":false"));
    bench_web_sound_status(true,true,0,0);assert(request(7,"1")==409);bench_web_sound_status(false,true,0,0);
    assert(request(5,"1")==409);bench_web_set_client(false);bench_web_set_default_ready(true);
    assert(request(5,"1")==202&&request(7,"1")==409);assert(bench_web_take_sound_request()==1);
    bench_web_set_default_ready(false);bench_web_set_client(true);
    assert(request(7,"0")==400&&request(7,"{}")==400);
    dsp_header="";assert(request(7,"1")==403);dsp_header="1";
    origin="http://evil.test";assert(request(7,"1")==403);origin="http://192.168.31.135";
    host="evil.test";assert(request(7,"1")==403);host="192.168.31.135";
    mime="text/plain";assert(request(7,"1")==415);mime="application/json";
    network.online=false;assert(request(7,"1")==403);network.online=true;
    network.setup=true;ap_request=true;assert(request(7,"1")==403);network.setup=false;ap_request=false;
    assert(request(7,"1")==202);bench_web_set_client(false);assert(!bench_web_take_dsp_request());
    bench_web_set_client(false);bench_web_set_default_ready(true);
    assert(request(1,"")==200&&strstr(body,"\"dsp_default_ready\":true"));
    assert(request(5,"1")==202&&bench_web_take_sound_request()==1);
    assert(request(7,"1")==202&&bench_web_take_dsp_request());
    bench_web_set_default_ready(false);
    assert(request(5,"1")==409);
    puts("PASS manual DSP mailbox/cache: no HTTP hardware access, fixed register JSON, real zero vs unread, queued/running busy, stale session cache, STA/Origin/header/type/body/sound admission");
    bench_web_set_client(false);bench_web_set_default_ready(true);bench_web_dsp_publish(&dsp);
    assert(request(1,"")==200&&strstr(body,"\"output_mode\":7"));
    const char *modes[]={"1","2","3","4","7"};
    for(unsigned i=0;i<5;++i){
        bench_web_sound_status(true,true,0,0); /* USB may be interrupted explicitly */
        assert(request(8,modes[i])==202&&request(8,"4")==409&&request(5,"1")==409&&request(7,"1")==409);
        int selected=atoi(modes[i]);assert(bench_web_output_change_pending());
        assert(bench_web_take_output_request()==selected&&!bench_web_output_change_pending()&&bench_web_take_output_request()==0);
        assert(request(8,"3")==409); /* in-flight remains busy */
        assert(request(1,"")==200&&strstr(body,"\"output_pending\":"));
        bench_web_output_publish((unsigned)selected,0);
        assert(request(1,"")==200);cJSON *state=cJSON_Parse(body);
        assert(cJSON_GetObjectItemCaseSensitive(state,"output_mode")->valueint==selected&&cJSON_GetObjectItemCaseSensitive(state,"output_pending")->valueint==0);cJSON_Delete(state);
    }
    bench_web_sound_status(false,true,0,0);
    assert(request(8,"4")==202&&bench_web_take_output_request()==4);bench_web_output_publish(4,263);
    assert(request(1,"")==200&&strstr(body,"\"output_mode\":7")&&strstr(body,"\"output_result\":263"));
    const char *bad_modes[]={"0","5","6","8","-1","4 ","{}",""};
    for(unsigned i=0;i<8;++i)assert(request(8,bad_modes[i])==400);
    output_header="";assert(request(8,"4")==403);output_header="1";
    origin="http://evil.test";assert(request(8,"4")==403);origin="http://192.168.31.135";
    host="evil.test";assert(request(8,"4")==403);host="192.168.31.135";
    mime="text/plain";assert(request(8,"4")==415);mime="application/json";
    network.online=false;assert(request(8,"4")==403);network.online=true;
    network.setup=true;ap_request=true;assert(request(8,"4")==403);network.setup=false;ap_request=false;
    bench_web_set_client(true);assert(request(8,"4")==409);bench_web_set_client(false);
    bench_web_set_default_ready(false);assert(request(8,"4")==409);bench_web_set_default_ready(true);
    assert(request(8,"4")==202&&request(5,"0")==202&&bench_web_sound_stop_pending());
    assert(bench_web_take_sound_request()==-1);assert(bench_web_take_output_request()==4);bench_web_output_publish(7,ESP_ERR_INVALID_STATE);
    puts("PASS output mailbox: five physical modes, active USB admission, applied vs pending/failure, in-flight exclusivity, STOP priority, STA/Host/Origin/header/type/bounds, SS+ exclusion, no HTTP hardware access");
    assert(request(9,"1")==409); /* built-in cannot arm external audition */
    bench_web_set_default_ready(false);bench_web_tuning_publish(true,0,0);
    bench_web_set_client(true);
    assert(request(8,"3")==409&&request(5,"1")==409);
    for(unsigned command=1;command<=4;++command){
        char value[2]={(char)('0'+command),0};
        assert(request(9,value)==202&&bench_web_tuning_pending()&&bench_web_tuning_queued());
        assert(request(9,"1")==409&&request(8,"3")==409&&request(7,"1")==409);
        assert(bench_web_take_tuning_request()==(int)command&&!bench_web_tuning_queued());
        assert(bench_web_tuning_pending()&&bench_web_take_tuning_request()==0&&request(9,"1")==409);
        bench_web_tuning_publish(true,command==4?0:3,0);
    }
    assert(request(1,"")==200&&strstr(body,"\"dsp_external\":true")&&strstr(body,"\"tuning_pending\":0"));
    const char *bad_tuning[]={"0","5","7","-1","1 ","{}",""};
    for(unsigned i=0;i<7;++i)assert(request(9,bad_tuning[i])==400);
    tuning_header="";assert(request(9,"1")==403);tuning_header="1";
    origin="http://evil.test";assert(request(9,"1")==403);origin="http://192.168.31.135";
    host="evil.test";assert(request(9,"1")==403);host="192.168.31.135";
    mime="text/plain";assert(request(9,"1")==415);mime="application/json";
    network.online=false;assert(request(9,"1")==403);network.online=true;
    network.setup=true;ap_request=true;assert(request(9,"1")==403);network.setup=false;ap_request=false;
    assert(request(9,"1")==202&&request(5,"0")==202&&bench_web_sound_stop_pending());
    assert(bench_web_take_sound_request()==-1);bench_web_tuning_publish(true,0,0);
    bench_web_set_client(false); /* retained external RAM still allows status/arming */
    assert(request(7,"1")==202&&bench_web_take_dsp_request());bench_web_dsp_publish(&dsp);
    assert(request(9,"2")==202);bench_web_tuning_publish(true,4,263);
    assert(request(1,"")==200&&strstr(body,"\"tuning_result\":263"));
    bench_web_tuning_publish(true,0,0);
    assert(request(9,"1")==202);bench_web_tuning_revoke(true,0);
    assert(!bench_web_tuning_pending()); /* write invalidates stale queued approval */
    assert(request(9,"4")==202);bench_web_tuning_revoke(true,0);
    assert(bench_web_take_tuning_request()==4); /* write cannot swallow restore */
    bench_web_tuning_publish(false,0,0);
    puts("PASS SS audition mailbox: no auto arm, exact Origin/Host/header/STA/type/body, queued/inflight exclusion, STOP priority, external RAM after disconnect, explicit restore and no HTTP hardware access");
    chime_bluetooth_status_t bt={.available=true,.connected=true,.playing=true,.codec=CHIME_BT_CODEC_AAC,.rate=44100,.channels=2};
    bench_web_source_publish(2,2,false,&bt);
    assert(request(1,"")==200&&strstr(body,"\"bt_codec\":\"AAC\"")&&strstr(body,"\"bt_rate\":44100"));
    bt.stream_state=CHIME_BT_STREAM_REBUFFERING;bt.rebuffer_events=3;bt.rebuffer_resumes=2;bt.rebuffer_wait_ms=2000;
    bench_web_source_publish(2,2,false,&bt);
    assert(request(1,"")==200&&strstr(body,"\"bt_stream_state\":4")&&strstr(body,"\"bt_rebuffer_events\":3")&&
        strstr(body,"\"bt_rebuffer_resumes\":2")&&strstr(body,"\"bt_rebuffer_wait_ms\":2000"));
    bt.codec=CHIME_BT_CODEC_SBC;bench_web_source_publish(2,2,false,&bt);
    assert(request(1,"")==200&&strstr(body,"\"bt_codec\":\"SBC\""));
    const chime_bt_codec_t codecs[]={CHIME_BT_CODEC_APTX,CHIME_BT_CODEC_APTX_HD,CHIME_BT_CODEC_OPUS,CHIME_BT_CODEC_LDAC};
    const char *names[]={"\"bt_codec\":\"aptX\"","\"bt_codec\":\"aptX HD\"","\"bt_codec\":\"Opus\"","\"bt_codec\":\"LDAC\""};
    for(unsigned i=0;i<sizeof(codecs)/sizeof(codecs[0]);++i){bt.codec=codecs[i];bench_web_source_publish(2,2,false,&bt);assert(request(1,"")==200&&strstr(body,names[i]));}
    puts("PASS negotiated codec cache to HTTP status: SBC/AAC/aptX/HD/Opus/LDAC are not hard-coded");
    bench_web_stop();bench_web_stop();assert(mdns_stops==1&&http_stops==1);
    puts("PASS live IP/mDNS, status, scan JSON, AP-only token/POST, chunked body, Origin/type/size/JSON validation and captive HTTP redirects");
    return 0;
}

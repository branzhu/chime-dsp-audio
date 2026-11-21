#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"
#ifdef _MSC_VER
typedef ptrdiff_t ssize_t;
#else
#include <sys/types.h>
#endif
typedef void *httpd_handle_t;
typedef struct {size_t content_len;const char *uri;} httpd_req_t;
typedef int httpd_err_code_t;
typedef struct {const char *uri;int method;esp_err_t (*handler)(httpd_req_t *);void *user_ctx;} httpd_uri_t;
typedef struct {unsigned stack_size;uint16_t server_port,max_open_sockets,max_uri_handlers,recv_wait_timeout,send_wait_timeout;bool lru_purge_enable;} httpd_config_t;
#define HTTPD_DEFAULT_CONFIG() ((httpd_config_t){0})
#define HTTP_GET 0
#define HTTP_POST 1
#define HTTPD_404_NOT_FOUND 404
#define HTTPD_RESP_USE_STRLEN (-1)
#define HTTPD_500_INTERNAL_SERVER_ERROR 500
esp_err_t httpd_start(httpd_handle_t *handle,const httpd_config_t *config);
esp_err_t httpd_stop(httpd_handle_t handle);
esp_err_t httpd_register_uri_handler(httpd_handle_t handle,const httpd_uri_t *uri);
esp_err_t httpd_resp_set_type(httpd_req_t *req,const char *type);
esp_err_t httpd_resp_set_hdr(httpd_req_t *req,const char *key,const char *value);
esp_err_t httpd_resp_send(httpd_req_t *req,const char *data,ssize_t length);
esp_err_t httpd_resp_send_chunk(httpd_req_t *req,const char *data,ssize_t length);
esp_err_t httpd_resp_send_err(httpd_req_t *req,int error,const char *message);

esp_err_t httpd_register_err_handler(httpd_handle_t h,httpd_err_code_t code,esp_err_t (*handler)(httpd_req_t *,httpd_err_code_t));
esp_err_t httpd_resp_set_status(httpd_req_t *req,const char *status);
int httpd_req_to_sockfd(httpd_req_t *req);
size_t httpd_req_get_hdr_value_len(httpd_req_t *req,const char *name);
esp_err_t httpd_req_get_hdr_value_str(httpd_req_t *req,const char *name,char *out,size_t capacity);
int httpd_req_recv(httpd_req_t *req,char *out,size_t size);

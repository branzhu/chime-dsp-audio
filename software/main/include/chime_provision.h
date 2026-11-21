#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define CHIME_SCAN_MAX 20
#define CHIME_AP_IP "192.168.4.1"
#define CHIME_HOSTNAME "chime"
#define CHIME_SETUP_PASSWORD "chime-setup"
typedef struct {
    uint32_t version;
    uint8_t ssid_len,password_len;
    uint8_t ssid[32],password[64];
} chime_credentials_t;
bool chime_credentials_valid(const chime_credentials_t *credentials);
bool chime_password_valid(const char *password,size_t length,bool secure);
/* DNS response for one uncompressed question, bounded to a 512-byte UDP packet.
 * A/ANY -> captive IPv4, AAAA/other IN -> no answers. Never forwards queries. */
size_t chime_dns_reply(const uint8_t *query,size_t bytes,const uint8_t ip[4],uint8_t *out,size_t capacity);

/* Worker-owned scan scheduling; API reads never trigger repeated RF scans. */
typedef struct {bool active,pending;uint64_t started_us,retry_at_us;} chime_scan_clock_t;
typedef enum {CHIME_SCAN_IDLE,CHIME_SCAN_START,CHIME_SCAN_TIMEOUT} chime_scan_action_t;
void chime_scan_request(chime_scan_clock_t *scan);
chime_scan_action_t chime_scan_poll(chime_scan_clock_t *scan,uint64_t now_us,bool allowed);
void chime_scan_finish(chime_scan_clock_t *scan);

#include "chime_provision.h"
#include <string.h>
bool chime_password_valid(const char *p,size_t length,bool secure)
{
    if(p==NULL || length>64 || memchr(p,0,length)!=NULL) return false;
    if(!secure) return length==0;
    if(length==64) {
        for(size_t i=0;i<length;++i) if(!((p[i]>='0'&&p[i]<='9')||(p[i]>='a'&&p[i]<='f')||(p[i]>='A'&&p[i]<='F'))) return false;
        return true;
    }
    return length>=8;
}
bool chime_credentials_valid(const chime_credentials_t *c)
{
    if(c==NULL || c->version!=1 || c->ssid_len==0 || c->ssid_len>32 ||
       memchr(c->ssid,0,c->ssid_len)!=NULL) return false;
    return chime_password_valid((const char *)c->password,c->password_len,c->password_len!=0);
}
size_t chime_dns_reply(const uint8_t *q,size_t n,const uint8_t ip[4],uint8_t *out,size_t capacity)
{
    if(!q||!ip||!out||n<17||n>512||(q[2]&0xfeU)||(q[3]&0xcfU)||q[4]!=0||q[5]!=1||q[6]||q[7]||q[8]||q[9])return 0;
    size_t p=12;
    while(p<n && q[p]) {
        size_t len=q[p++];
        if(len>63||len>n-p||p+len>266) return 0;
        p+=len;
    }
    if(p>=n||p+5>n) return 0;
    ++p;
    uint16_t type=(uint16_t)((uint16_t)q[p]<<8|q[p+1]);
    if(q[p+2]!=0||q[p+3]!=1)return 0;
    p+=4;
    bool answer=type==1||type==255;
    size_t total=p+(answer?16U:0U);
    if(total>capacity||total>512)return 0;
    memcpy(out,q,p);out[2]=(uint8_t)(0x84U|(q[2]&1U));out[3]=0;
    out[6]=0;out[7]=answer?1:0;out[8]=out[9]=out[10]=out[11]=0;
    if(answer) {
        static const uint8_t rr[]={0xc0,0x0c,0,1,0,1,0,0,0,0,0,4};
        memcpy(out+p,rr,sizeof(rr));memcpy(out+p+sizeof(rr),ip,4);
    }
    return total;
}

void chime_scan_request(chime_scan_clock_t *scan)
{
    if(!scan->active)scan->pending=true; /* coalesce, never queue behind active scan */
}
chime_scan_action_t chime_scan_poll(chime_scan_clock_t *scan,uint64_t now_us,bool allowed)
{
    if(scan->active) {
        if(now_us-scan->started_us>=15000000ULL) {
            scan->active=false;scan->pending=false;scan->retry_at_us=now_us+1000000ULL;
            return CHIME_SCAN_TIMEOUT;
        }
        return CHIME_SCAN_IDLE;
    }
    if(allowed&&scan->pending&&now_us>=scan->retry_at_us) {
        scan->pending=false;scan->active=true;scan->started_us=now_us;scan->retry_at_us=now_us+10000000ULL;
        return CHIME_SCAN_START;
    }
    return CHIME_SCAN_IDLE;
}
void chime_scan_finish(chime_scan_clock_t *scan) {scan->active=false;}

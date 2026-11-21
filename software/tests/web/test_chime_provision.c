
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "chime_provision.h"
int main(void){
    assert(!chime_credentials_valid(NULL));chime_credentials_t c={.version=1,.ssid_len=4};memcpy(c.ssid,"Home",4);assert(chime_credentials_valid(&c));
    c.version=2;assert(!chime_credentials_valid(&c));c.version=1;c.ssid_len=33;assert(!chime_credentials_valid(&c));c.ssid_len=0;assert(!chime_credentials_valid(&c));c.ssid_len=4;c.ssid[1]=0;assert(!chime_credentials_valid(&c));c.ssid[1]='o';
    assert(chime_password_valid("",0,false));assert(!chime_password_valid("x",1,false));assert(!chime_password_valid("1234567",7,true));assert(chime_password_valid("12345678",8,true));assert(!chime_password_valid("pass\0word",9,true));
    char pass[65];memset(pass,'a',64);pass[64]=0;assert(chime_password_valid(pass,63,true));assert(chime_password_valid(pass,64,true));pass[3]='g';assert(!chime_password_valid(pass,64,true));assert(!chime_password_valid(pass,65,true));
    c.password_len=8;memcpy(c.password,"password",8);assert(chime_credentials_valid(&c));c.password_len=65;assert(!chime_credentials_valid(&c));
    puts("PASS Wi-Fi credential version/SSID/password boundaries, open and WPA2/hex PSK");
    chime_scan_clock_t scan={0};
    assert(chime_scan_poll(&scan,100,true)==CHIME_SCAN_IDLE);
    chime_scan_request(&scan);assert(chime_scan_poll(&scan,100,false)==CHIME_SCAN_IDLE);
    assert(chime_scan_poll(&scan,100,true)==CHIME_SCAN_START&&scan.active);
    chime_scan_request(&scan);assert(!scan.pending); /* reads/clicks cannot queue endless scans */
    assert(chime_scan_poll(&scan,15000099,true)==CHIME_SCAN_IDLE);
    assert(chime_scan_poll(&scan,15000100,true)==CHIME_SCAN_TIMEOUT&&!scan.active);
    assert(chime_scan_poll(&scan,16000100,true)==CHIME_SCAN_IDLE); /* no retry loop */
    chime_scan_request(&scan);assert(chime_scan_poll(&scan,16000099,true)==CHIME_SCAN_IDLE);
    assert(chime_scan_poll(&scan,16000100,true)==CHIME_SCAN_START);
    chime_scan_finish(&scan);assert(!scan.active);chime_scan_request(&scan);
    assert(chime_scan_poll(&scan,26000099,true)==CHIME_SCAN_IDLE);
    assert(chime_scan_poll(&scan,26000100,true)==CHIME_SCAN_START);
    chime_scan_finish(&scan);assert(chime_scan_poll(&scan,90000000,true)==CHIME_SCAN_IDLE);
    puts("PASS scan scheduling: initial request, pause while connecting, coalesce active requests, 15s timeout, bounded manual retry, no rescan-on-read loop");
    const uint8_t ip[]={192,168,4,1};uint8_t q[512]={0x12,0x34,1,0,0,1,0,0,0,0,0,0,3,'w','w','w',4,'t','e','s','t',0,0,1,0,1};const size_t size=26;
    uint8_t out[514];memset(out,0xa5,sizeof(out));size_t n=chime_dns_reply(q,size,ip,out+1,512);assert(n==42&&out[0]==0xa5&&out[513]==0xa5);uint8_t *r=out+1;assert(r[0]==0x12&&r[1]==0x34&&r[2]==0x85&&r[3]==0&&r[7]==1);assert(!memcmp(r+n-4,ip,4));assert(r[32]==0&&r[33]==0&&r[34]==0&&r[35]==0);assert(!chime_dns_reply(q,size,ip,r,n-1));
    for(size_t i=0;i<size;++i)assert(!chime_dns_reply(q,i,ip,r,512));
    q[23]=28;assert(chime_dns_reply(q,size,ip,r,512)==size&&r[7]==0);q[23]=255;assert(chime_dns_reply(q,size,ip,r,512)==42);q[23]=1;
    q[3]=0x30;assert(chime_dns_reply(q,size,ip,r,512)==42&&r[3]==0);q[3]=0;
    for(unsigned bit=1;bit<8;++bit){q[2]=(uint8_t)(1U<<bit);assert(!chime_dns_reply(q,size,ip,r,512));}q[2]=1;
    q[12]=0xc0;assert(!chime_dns_reply(q,size,ip,r,512));q[12]=64;assert(!chime_dns_reply(q,size,ip,r,512));q[12]=3;q[5]=2;assert(!chime_dns_reply(q,size,ip,r,512));q[5]=1;
    q[25]=3;assert(!chime_dns_reply(q,size,ip,r,512));q[25]=1;q[11]=1;/* EDNS additional data is safely stripped; not reflected */assert(chime_dns_reply(q,size,ip,r,512)==42&&r[11]==0);q[11]=0;
    uint32_t seed=1;
    for(unsigned i=0;i<30000;++i){seed=seed*1664525U+1013904223U;size_t len=seed%513;for(size_t j=0;j<len;++j){seed=seed*1664525U+1013904223U;q[j]=(uint8_t)(seed>>24);}memset(out,0xa5,sizeof(out));size_t cap=(seed>>8)%513;size_t reply=chime_dns_reply(q,len,ip,out+1,cap);assert(reply<=cap&&out[0]==0xa5&&out[cap+1]==0xa5);}
    puts("PASS bounded captive DNS: A/ANY/AAAA, zero TTL, AD/CD, malformed flags/labels/classes, truncation, EDNS and 30000 fuzz packets");return 0;
}

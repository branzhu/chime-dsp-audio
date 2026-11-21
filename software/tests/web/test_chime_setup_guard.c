#include "chime_setup_guard.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
static bool allowed(const void *address,size_t size) {return chime_setup_address_is_ap((const struct sockaddr *)address,size);}
int main(void)
{
    const uint8_t ap4[]={192,168,4,1};
    const uint8_t ap6[]={0,0,0,0,0,0,0,0,0,0,255,255,192,168,4,1};
    struct sockaddr_in v4={0};v4.sin_family=AF_INET;memcpy(&v4.sin_addr,ap4,4);
    assert(allowed(&v4,sizeof(v4)));
    for(size_t i=0;i<sizeof(v4);++i)assert(!allowed(&v4,i));
    uint8_t *address=(uint8_t *)&v4.sin_addr;
    for(unsigned i=0;i<4;++i){address[i]^=1;assert(!allowed(&v4,sizeof(v4)));address[i]^=1;}
    const uint8_t denied4[][4]={{192,168,4,2},{192,168,1,50},{127,0,0,1},{0,0,0,0},{255,255,255,255}};
    for(size_t i=0;i<sizeof(denied4)/sizeof(denied4[0]);++i){memcpy(&v4.sin_addr,denied4[i],4);assert(!allowed(&v4,sizeof(v4)));}
    struct sockaddr_in6 v6={0};v6.sin6_family=AF_INET6;memcpy(&v6.sin6_addr,ap6,16);
    assert(allowed(&v6,sizeof(v6)));
    /* Reproduce old 16-byte buffer: getsockname truncates the mapped address,
     * leaving flowinfo at the offset the old code read as its IPv4 address. */
    assert(!allowed(&v6,sizeof(struct sockaddr_in)));
    struct sockaddr_in legacy={0};memcpy(&legacy,&v6,sizeof(legacy));
    assert(memcmp(&legacy.sin_addr,ap4,4)!=0); /* exact original false rejection */
    for(size_t i=0;i<sizeof(v6);++i)assert(!allowed(&v6,i));
    uint8_t *mapped=(uint8_t *)&v6.sin6_addr;
    for(unsigned i=0;i<16;++i){mapped[i]^=1;assert(!allowed(&v6,sizeof(v6)));mapped[i]^=1;}
    v6.sin6_flowinfo=123;v6.sin6_scope_id=4;assert(allowed(&v6,sizeof(v6)));
    const uint8_t denied6[][16]={
        {0,0,0,0,0,0,0,0,0,0,255,255,192,168,4,2},
        {0,0,0,0,0,0,0,0,0,0,255,255,192,168,1,50},
        {0,0,0,0,0,0,0,0,0,0,0,0,192,168,4,1}, /* IPv4-compatible != mapped */
        {0xfe,0x80,0,0,0,0,0,0,0,0,0,0,192,168,4,1},
        {0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1},{0}};
    memcpy(&v6.sin6_flowinfo,ap4,4); /* never interpret flowinfo as IPv4 */
    for(size_t i=0;i<sizeof(denied6)/sizeof(denied6[0]);++i){memcpy(&v6.sin6_addr,denied6[i],16);assert(!allowed(&v6,sizeof(v6)));}
    struct sockaddr_storage unknown={0};unknown.ss_family=AF_UNSPEC;
    assert(!allowed(&unknown,sizeof(unknown)));assert(!allowed(NULL,0));assert(!allowed(NULL,sizeof(v4)));
    assert(!allowed(&unknown,sizeof(unknown)+1));
    puts("PASS actual setup guard: IPv4 AP and exact IPv4-mapped IPv6 AP allowed; STA/client/loopback/unspecified/native IPv6/compatible/truncated/unknown/null denied");
    return 0;
}

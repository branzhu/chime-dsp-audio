#include "chime_setup_guard.h"
#include <stdint.h>
#include <string.h>

bool chime_setup_address_is_ap(const struct sockaddr *local,size_t length)
{
    static const uint8_t ap4[4]={192,168,4,1};
    static const uint8_t ap6[16]={0,0,0,0,0,0,0,0,0,0,255,255,192,168,4,1};
    if(!local||length<sizeof(struct sockaddr)||length>sizeof(struct sockaddr_storage))return false;
    if(local->sa_family==AF_INET) {
        if(length<sizeof(struct sockaddr_in))return false;
        const struct sockaddr_in *v4=(const struct sockaddr_in *)local;
        return memcmp(&v4->sin_addr,ap4,sizeof(ap4))==0;
    }
#if !defined(ESP_PLATFORM) || CONFIG_LWIP_IPV6
    if(local->sa_family==AF_INET6) {
        if(length<sizeof(struct sockaddr_in6))return false;
        const struct sockaddr_in6 *v6=(const struct sockaddr_in6 *)local;
        return memcmp(&v6->sin6_addr,ap6,sizeof(ap6))==0;
    }
#endif
    return false;
}

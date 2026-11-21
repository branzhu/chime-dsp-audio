#pragma once
#include <stdbool.h>
#include <stddef.h>
#ifdef ESP_PLATFORM
#include "lwip/sockets.h"
#elif defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#endif
/* Check the socket LOCAL destination, never Host/Origin or peer-supplied IP.
 * Accept only the AP's IPv4 or its exact IPv4-mapped IPv6 equivalent. */
bool chime_setup_address_is_ap(const struct sockaddr *local,size_t length);

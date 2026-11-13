#ifndef NETWORK_H
#define NETWORK_H

#include <net/if.h>
#include <stdbool.h>

typedef enum {
    NET_OFFLINE,
    NET_WIFI,
    NET_ETHERNET,
    NET_OTHER,
} NetworkKind;

typedef struct {
    NetworkKind kind;
    char iface[IF_NAMESIZE];
    char ssid[33];
    int signal_bars; /* 0..5, or -1 if unknown */
    bool vpn;        /* the default route goes through a tunnel over the link above */
} NetworkStatus;

void network_get_status(NetworkStatus *status);

#endif

#include "network.h"
#include "sysfs.h"
#include "utf8.h"
#include <linux/genetlink.h>
#include <linux/netlink.h>
#include <linux/nl80211.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

/* Both route tables list one route per line. A parser fills in the interface and metric of a
 * default route and returns true; the lowest metric wins. */
typedef bool (*RouteParser)(const char *line, char *iface, unsigned *metric);

static bool parse_route_v4(const char *line, char *iface, unsigned *metric) {
    char name[IF_NAMESIZE];
    unsigned dest, gateway, flags, mask;
    int refcnt, use, metric_value;
    if (sscanf(line, "%15s %x %x %x %d %d %d %x", name, &dest, &gateway, &flags, &refcnt, &use,
               &metric_value, &mask) != 8)
        return false;
    if (dest != 0 || mask != 0 || !(flags & 1)) return false;
    snprintf(iface, IF_NAMESIZE, "%s", name);
    *metric = metric_value;
    return true;
}

static bool parse_route_v6(const char *line, char *iface, unsigned *metric) {
    char dest[33], name[IF_NAMESIZE];
    unsigned plen, refcnt, use, flags;
    if (sscanf(line, "%32s %x %*s %*s %*s %x %x %x %x %15s", dest, &plen, metric, &refcnt, &use,
               &flags, name) != 7)
        return false;
    if (plen != 0 || strspn(dest, "0") != 32 || !(flags & 1) || strcmp(name, "lo") == 0)
        return false;
    snprintf(iface, IF_NAMESIZE, "%s", name);
    return true;
}

static bool is_wireless(const char *iface) {
    char path[64];
    snprintf(path, sizeof(path), "/sys/class/net/%s/wireless", iface);
    return access(path, F_OK) == 0;
}

static bool is_ethernet(const char *iface) {
    char path[64];
    long type;
    snprintf(path, sizeof(path), "/sys/class/net/%s/type", iface);
    return sysfs_read_int(path, &type) && type == 1;
}

typedef struct {
    char iface[IF_NAMESIZE];
    unsigned metric;
    bool found;
} Route;

/* Keeps the route with the lowest metric. */
static void consider(Route *best, const char *iface, unsigned metric) {
    if (best->found && metric >= best->metric) return;
    snprintf(best->iface, sizeof(best->iface), "%s", iface);
    best->metric = metric;
    best->found = true;
}

/* Finds the preferred default route overall and the preferred one over a real link: with a
 * VPN the first is the tunnel, and the second is the Wi-Fi or Ethernet it runs over. */
static void default_routes(const char *table, RouteParser parse, Route *any, Route *physical) {
    FILE *f = fopen(table, "r");
    if (!f) return;

    char line[256], name[IF_NAMESIZE];
    unsigned metric;
    while (fgets(line, sizeof(line), f)) {
        if (!parse(line, name, &metric)) continue;
        consider(any, name, metric);
        if (is_wireless(name) || is_ethernet(name)) consider(physical, name, metric);
    }
    fclose(f);
}

static int signal_bars(const char *iface) {
    FILE *f = fopen("/proc/net/wireless", "r");
    if (!f) return -1;

    char line[256], name[IF_NAMESIZE + 1];
    double link, level;
    int bars = -1;

    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, " %16[^:]: %*x %lf %lf", name, &link, &level) != 3) continue;
        if (strcmp(name, iface) != 0) continue;

        double percent = level < 0 ? 2 * (level + 100) : link * 100 / 70;
        if (percent < 0) percent = 0;
        if (percent > 100) percent = 100;
        bars = percent <= 0 ? 0 : (int)percent / 20 + 1;
        if (bars > 5) bars = 5;
        break;
    }
    fclose(f);
    return bars;
}

typedef struct {
    struct nlmsghdr n;
    struct genlmsghdr g;
    char attrs[64];
} GenlRequest;

static void add_attr(GenlRequest *req, int type, const void *data, int len) {
    if (NLMSG_ALIGN(req->n.nlmsg_len) + NLA_ALIGN(NLA_HDRLEN + len) > sizeof(*req)) return;
    struct nlattr *attr = (struct nlattr *)((char *)req + NLMSG_ALIGN(req->n.nlmsg_len));
    attr->nla_type = type;
    attr->nla_len = NLA_HDRLEN + len;
    memcpy((char *)attr + NLA_HDRLEN, data, len);
    req->n.nlmsg_len = NLMSG_ALIGN(req->n.nlmsg_len) + NLA_ALIGN(attr->nla_len);
}

/* Sends one generic netlink request and returns the attribute of the given type. */
static struct nlattr *genl_query(int fd, int family, int cmd, int attr_type,
                                 const void *attr, int attr_len, int want,
                                 char *resp, int resp_size) {
    GenlRequest req = {
        .n = { .nlmsg_len = NLMSG_LENGTH(GENL_HDRLEN), .nlmsg_type = family,
               .nlmsg_flags = NLM_F_REQUEST, .nlmsg_seq = 1 },
        .g = { .cmd = cmd, .version = 1 },
    };
    add_attr(&req, attr_type, attr, attr_len);
    if (send(fd, &req, req.n.nlmsg_len, 0) < 0) return NULL;

    /* Accept only the kernel's answer to this request: any local process could otherwise
     * address a forged message to our socket. */
    struct sockaddr_nl from;
    socklen_t from_len = sizeof(from);
    int len = recvfrom(fd, resp, resp_size, 0, (struct sockaddr *)&from, &from_len);
    struct nlmsghdr *n = (struct nlmsghdr *)resp;
    if (len < (int)NLMSG_HDRLEN || from_len < sizeof(from) || from.nl_pid != 0 ||
        !NLMSG_OK(n, (unsigned)len) || n->nlmsg_seq != req.n.nlmsg_seq ||
        n->nlmsg_type == NLMSG_ERROR)
        return NULL;

    int remaining = n->nlmsg_len - NLMSG_LENGTH(GENL_HDRLEN);
    struct nlattr *a = (struct nlattr *)((char *)NLMSG_DATA(n) + GENL_HDRLEN);
    while (remaining >= NLA_HDRLEN && a->nla_len >= NLA_HDRLEN && a->nla_len <= remaining) {
        if ((a->nla_type & NLA_TYPE_MASK) == want) return a;
        remaining -= NLA_ALIGN(a->nla_len);
        a = (struct nlattr *)((char *)a + NLA_ALIGN(a->nla_len));
    }
    return NULL;
}

/* Code points that must never reach the screen from an untrusted name: C0/C1 controls,
 * DEL, zero-width and bidirectional formatting characters (they could reorder or hide
 * the rest of the line). */
static bool unsafe_code_point(uint32_t cp) {
    return cp < 0x20 || (cp >= 0x7F && cp <= 0x9F) || cp == 0x061C ||
           (cp >= 0x200B && cp <= 0x200F) || (cp >= 0x202A && cp <= 0x202E) ||
           (cp >= 0x2066 && cp <= 0x2069) || cp == 0xFEFF;
}

/* SSIDs are arbitrary bytes; Cairo needs valid UTF-8, so replace anything invalid or
 * unsafe with '?'. */
static void sanitize_utf8(char *s) {
    const char *read = s;
    char *write = s;
    while (*read) {
        uint32_t cp;
        int len = utf8_decode(read, &cp);
        if (len == 0 || unsafe_code_point(cp)) {
            *write++ = '?';
            read += len ? len : 1;
        } else {
            memmove(write, read, len);
            write += len;
            read += len;
        }
    }
    *write = '\0';
}

static void wifi_ssid(const char *iface, char *ssid, size_t size) {
    ssid[0] = '\0';
    unsigned ifindex = if_nametoindex(iface);
    if (!ifindex) return;

    int fd = socket(AF_NETLINK, SOCK_RAW | SOCK_CLOEXEC, NETLINK_GENERIC);
    if (fd < 0) return;
    /* The kernel answers in microseconds; this only bounds a stuck socket. */
    struct timeval timeout = { .tv_usec = 50000 };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));

    char resp[8192];
    /* Both queries share the same resp buffer; the nlattr pointer from the first call
     * is only used to extract the family ID before the second call overwrites the buffer. */
    struct nlattr *a = genl_query(fd, GENL_ID_CTRL, CTRL_CMD_GETFAMILY,
                                  CTRL_ATTR_FAMILY_NAME, "nl80211", sizeof("nl80211"),
                                  CTRL_ATTR_FAMILY_ID, resp, sizeof(resp));
    if (a && a->nla_len >= NLA_HDRLEN + sizeof(unsigned short)) {
        int family = *(unsigned short *)((char *)a + NLA_HDRLEN);
        a = genl_query(fd, family, NL80211_CMD_GET_INTERFACE, NL80211_ATTR_IFINDEX,
                       &ifindex, sizeof(ifindex), NL80211_ATTR_SSID, resp, sizeof(resp));
    } else {
        a = NULL;
    }
    if (a) {
        size_t len = a->nla_len - NLA_HDRLEN;
        if (len >= size) len = size - 1;
        memcpy(ssid, (char *)a + NLA_HDRLEN, len);
        ssid[len] = '\0';
        sanitize_utf8(ssid);
    }
    close(fd);
}

void network_get_status(NetworkStatus *status) {
    memset(status, 0, sizeof(*status));
    status->signal_bars = -1;

    Route any = {0}, physical = {0};
    default_routes("/proc/net/route", parse_route_v4, &any, &physical);
    default_routes("/proc/net/ipv6_route", parse_route_v6, &any, &physical);
    if (!any.found) {
        status->kind = NET_OFFLINE;
        return;
    }
    const Route *link = physical.found ? &physical : &any;
    snprintf(status->iface, sizeof(status->iface), "%s", link->iface);
    status->vpn = physical.found && strcmp(any.iface, physical.iface) != 0;

    if (is_wireless(status->iface)) {
        status->kind = NET_WIFI;
        status->signal_bars = signal_bars(status->iface);
        wifi_ssid(status->iface, status->ssid, sizeof(status->ssid));
    } else if (is_ethernet(status->iface)) {
        status->kind = NET_ETHERNET;
    } else {
        status->kind = NET_OTHER;
    }
}

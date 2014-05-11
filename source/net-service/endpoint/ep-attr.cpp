/*
 * Copyright 2014 by Formation Data Systems, Inc.
 */
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <ifaddrs.h>
#include <arpa/inet.h>
#include <net/net-service.h>

namespace fds {

/*
 * -----------------------------------------------------------------------------------
 * EndPoint Attributes
 * -----------------------------------------------------------------------------------
 */
EpAttr::EpAttr(fds_uint32_t ip, int port) : ep_refcnt(0)
{
    ep_addr.sa_family = AF_INET;
    ((struct sockaddr_in *)&ep_addr)->sin_port = port;
}

EpAttr &
EpAttr::operator = (const EpAttr &rhs)
{
    ep_addr = rhs.ep_addr;
    return *this;
}

int
EpAttr::attr_get_port()
{
    if (ep_addr.sa_family == AF_INET) {
        return (((struct sockaddr_in *)&ep_addr)->sin_port);
    }
    return (((struct sockaddr_in6 *)&ep_addr)->sin6_port);
}

int
EpAttr::netaddr_get_port(const struct sockaddr *adr)
{
    if (adr->sa_family == AF_INET) {
        return (((struct sockaddr_in *)adr)->sin_port);
    }
    return (((struct sockaddr_in6 *)adr)->sin6_port);
}

void
EpAttr::netaddr_to_str(const struct sockaddr *adr, char *ip, int ip_len)
{
    int   len;
    void *buf;

    if (adr->sa_family == AF_INET) {
        struct sockaddr_in *ip4 = (struct sockaddr_in *)adr;
        len = INET6_ADDRSTRLEN;
        buf = &ip4->sin_addr;
    } else {
        struct sockaddr_in6 *ip6 = (struct sockaddr_in6 *)adr;
        len = INET_ADDRSTRLEN;
        buf = &ip6->sin6_addr;
    }
    fds_verify(len <= ip_len);
    inet_ntop(AF_INET, buf, ip, len);
}

void
EpAttr::netaddr_my_ip(struct sockaddr *adr,
                      struct sockaddr *mask,
                      unsigned int    *flags,
                      const char      *iface)
{
    struct ifaddrs *ifa, *cur;

    ifa = NULL;
    *flags = 0;
    memset(adr, 0, sizeof(*adr));

    getifaddrs(&ifa);
    for (cur = ifa; cur != NULL; cur = cur->ifa_next) {
        if (iface == NULL) {
            struct sockaddr_in *ip;
            char ipv4[INET_ADDRSTRLEN];

            /* Hacking, should be based on IP used with the GW */
            if (cur->ifa_addr->sa_family != AF_INET) {
                continue;
            }
            if (cur->ifa_name[0] != 'l' && cur->ifa_name[1] != 'o') {
                ip = reinterpret_cast<struct sockaddr_in *>(cur->ifa_addr);
                inet_ntop(AF_INET, &ip->sin_addr, ipv4, INET_ADDRSTRLEN);
                if (ipv4[0] == '1' && ipv4[1] == '0' &&
                    ipv4[2] == '.' && ipv4[3] == '1') {
                    goto found;
                }
            }
            continue;
        }
        if (strcmp(cur->ifa_name, iface) == 0) {
 found:
            *adr   = *cur->ifa_addr;
            *mask  = *cur->ifa_netmask;
            *flags = cur->ifa_flags;
            break;
        }
    }
    if (ifa != NULL) {
        freeifaddrs(ifa);
    }
}

}  // namespace fds

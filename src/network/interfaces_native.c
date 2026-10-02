#define _DEFAULT_SOURCE
#define _POSIX_C_SOURCE 200809L
#if defined(_WIN32) && !defined(_WIN32_WINNT)
#define _WIN32_WINNT 0x0600
#endif
#include "interfaces_internal.h"

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#else
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

static bool copy_address(const struct sockaddr *source, qa_net_address *out)
{
    if (!source) return false;
    if (source->sa_family == AF_INET) {
        const struct sockaddr_in *ip = (const struct sockaddr_in *)source;
        out->kind = QA_NET_IPV4;
        memcpy(out->host.ipv4, &ip->sin_addr, 4);
    } else if (source->sa_family == AF_INET6) {
        const struct sockaddr_in6 *ip = (const struct sockaddr_in6 *)source;
        out->kind = QA_NET_IPV6;
        memcpy(out->host.ipv6.bytes, &ip->sin6_addr, 16);
        out->host.ipv6.scope = ip->sin6_scope_id;
    } else return false;
    return true;
}

#ifdef _WIN32
static bool enumerate(qa_net_interfaces *o, qa_error *e)
{
    ULONG extent = 15360;
    IP_ADAPTER_ADDRESSES *adapters = NULL;
    ULONG status;
    for (;;) {
        IP_ADAPTER_ADDRESSES *next = realloc(adapters, extent);
        if (!next) { free(adapters); qa_error_set(e, QA_ERROR_MEMORY, 0, "Enumerating native Windows interfaces"); return false; }
        adapters = next;
        status = GetAdaptersAddresses(AF_UNSPEC,
            GAA_FLAG_INCLUDE_PREFIX | GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER,
            NULL, adapters, &extent);
        if (status != ERROR_BUFFER_OVERFLOW) break;
    }
    if (status == ERROR_NO_DATA) { free(adapters); return true; }
    if (status != NO_ERROR) {
        free(adapters); qa_error_set(e, QA_ERROR_IO, 0, "Native Windows interface enumeration failed: %lu", (unsigned long)status); return false;
    }
    bool ok = true;
    for (const IP_ADAPTER_ADDRESSES *a = adapters; ok && a; a = a->Next) {
        for (const IP_ADAPTER_UNICAST_ADDRESS *u = a->FirstUnicastAddress; ok && u; u = u->Next) {
            qa_net_interface record = {.name = a->AdapterName,
                .up = a->OperStatus == IfOperStatusUp, .running = a->OperStatus == IfOperStatusUp,
                .loopback = a->IfType == IF_TYPE_SOFTWARE_LOOPBACK};
            if (!copy_address(u->Address.lpSockaddr, &record.address)) continue;
            record.index = record.address.kind == QA_NET_IPV4 ? a->IfIndex : a->Ipv6IfIndex;
            unsigned bits = u->OnLinkPrefixLength;
            unsigned maximum = record.address.kind == QA_NET_IPV4 ? 32 : 128;
            if (bits <= maximum) {
                record.has_netmask = true;
                record.netmask.kind = record.address.kind;
                uint8_t *mask = record.address.kind == QA_NET_IPV4 ? record.netmask.host.ipv4 : record.netmask.host.ipv6.bytes;
                for (unsigned i = 0; i < maximum / 8; ++i) {
                    mask[i] = bits >= 8 ? 255 : bits ? (uint8_t)(255u << (8 - bits)) : 0;
                    bits = bits >= 8 ? bits - 8 : 0;
                }
            }
            ok = qa_net_interfaces_append(o, &record, e);
        }
    }
    free(adapters);
    return ok;
}
#else
static bool enumerate(qa_net_interfaces *o, qa_error *e)
{
    struct ifaddrs *interfaces = NULL;
    if (getifaddrs(&interfaces) < 0) {
        qa_error_set(e, QA_ERROR_IO, 0, "Native interface enumeration failed: %s", strerror(errno)); return false;
    }
    bool ok = true;
    for (const struct ifaddrs *a = interfaces; ok && a; a = a->ifa_next) {
        qa_net_interface record = {.name = a->ifa_name, .index = if_nametoindex(a->ifa_name),
            .up = (a->ifa_flags & IFF_UP) != 0, .running = (a->ifa_flags & IFF_RUNNING) != 0,
            .loopback = (a->ifa_flags & IFF_LOOPBACK) != 0};
        if (!copy_address(a->ifa_addr, &record.address)) continue;
        record.has_netmask = copy_address(a->ifa_netmask, &record.netmask);
        ok = qa_net_interfaces_append(o, &record, e);
    }
    freeifaddrs(interfaces);
    return ok;
}
#endif

bool qa_net_interfaces_capture(qa_net_interfaces **out, qa_error *e)
{
    if (!out) { qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing native interface snapshot output"); return false; }
    qa_net_interfaces *o = calloc(1, sizeof(*o));
    if (!o) { qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating native interface snapshot"); return false; }
    if (!enumerate(o, e)) { qa_net_interfaces_destroy(o); return false; }
    if (!qa_net_interfaces_valid(o)) {
        qa_net_interfaces_destroy(o);
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Invalid native interface address or mask");
        return false;
    }
    o->native_enumerated = true;
    *out = o;
    return true;
}

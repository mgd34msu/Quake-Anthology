/* Port of src/network/common/endpoint.ts. Addresses are value structs;
 * ipv6 host / loopback id strings are owned copies (destroy frees).
 * resolveAddress: TS async DNS -> synchronous getaddrinfo (PORTING async
 * rule). ipAddress IPv6 canonicalization uses inet_ntop in place of the
 * TS URL canonicalizer. RangeError -> qa_range_error. GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_ENDPOINT_H
#define QA_NETWORK_COMMON_ENDPOINT_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef enum qa_net_address_kind {
  QA_NET_IPV4, QA_NET_IPV6, QA_NET_LOOPBACK, QA_NET_IPX
} qa_net_address_kind;
typedef struct qa_net_address {
  qa_net_address_kind kind;
  uint8_t ipv4[4];
  char *host; /* ipv6 host or loopback id, owned */
  uint32_t network; uint8_t node[6];
  int64_t port;
} qa_net_address;
typedef qa_net_address qa_ipv4_address_net;
typedef qa_net_address qa_ipx_address_net;

int64_t qa_net_port_number(int64_t port, bool allow_zero);
/* All makers return 0 only on OOM; invalid input aborts. */
int qa_net_make_ipv4(qa_net_address *out, const int64_t host[4], int64_t port, bool allow_zero);
int qa_net_make_ip(qa_net_address *out, const char *host, int64_t port, bool allow_zero);
int qa_net_make_ipx(qa_net_address *out, int64_t network, const int64_t node[6], int64_t port);
int qa_net_make_loopback(qa_net_address *out, const char *id);
int qa_net_address_clone(const qa_net_address *in, qa_net_address *out);
void qa_net_address_destroy(qa_net_address *a);
/* Malloc'd strings, caller frees; NULL on OOM. */
char *qa_net_address_host(const qa_net_address *a);
char *qa_net_address_key(const qa_net_address *a, bool include_port);
bool qa_net_same_address(const qa_net_address *a, const qa_net_address *b, bool include_port);
/* parseNetworkAddress boundary: kind "ipv4"|"ipv6"|"loopback"|"ipx";
 * host_text for ipv6/loopback, numbers[] octets/node otherwise. */
int qa_net_parse_address(qa_net_address *out, const char *kind, const char *host_text,
                         const double *numbers, size_t number_count,
                         int64_t network, bool has_network, double port, bool has_port);
int qa_net_resolve_address(qa_net_address *out, const char *text, int64_t default_port, int family);
#endif

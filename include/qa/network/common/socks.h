/* Port of src/network/common/socks.ts. SocksAssociation open: TS async
 * TCP negotiation -> synchronous blocking connect (PORTING async rule).
 * Errors -> qa_range_error with the TS message. GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_SOCKS_H
#define QA_NETWORK_COMMON_SOCKS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/network/common/endpoint.h"
typedef struct qa_socks_options {
  const char *server; int64_t port; const char *username; const char *password;
} qa_socks_options;
/* Malloc'd framed datagram, caller frees; *out_len set. NULL on OOM. */
uint8_t *qa_socks_datagram(const qa_net_address *to, const uint8_t *payload, size_t length, size_t *out_len);
/* Returns 1 + from/payload (payload malloc'd), 0 if not a SOCKS datagram. */
int qa_socks_read_datagram(const uint8_t *bytes, size_t length, qa_net_address *from,
                           uint8_t **payload, size_t *payload_len);
/* Defined here (not opaque) so transports can embed it by value. */
typedef struct qa_socks_association {
  int fd; bool started, failed, has_relay; qa_net_address relay;
} qa_socks_association;
int qa_socks_association_init(qa_socks_association *a);
void qa_socks_association_destroy(qa_socks_association *a);
void qa_socks_association_open(qa_socks_association *a, const qa_socks_options *opts, int64_t local_port);
int qa_socks_association_get_relay(const qa_socks_association *a, qa_net_address *out);
void qa_socks_association_close(qa_socks_association *a);
#endif

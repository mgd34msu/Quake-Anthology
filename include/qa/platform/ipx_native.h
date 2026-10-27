/*
 * Port of src/platform/ipx-native.ts: raw AF_IPX datagram socket
 * (glibc netipx/ipx.h layout). C accommodations: direct syscalls
 * instead of Bun FFI; only the Linux branch is ported (the Winsock
 * branch has no Linux build); the byte-level sockaddr layout is
 * built explicitly as in the source. Throws become false plus an
 * optional error record; UnsupportedTransportError is its flag.
 * GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_IPX_NATIVE_H
#define QA_PLATFORM_IPX_NATIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/*
 * Mirror of IpxAddress from src/network/common/endpoint.ts (TS
 * owner; no C home yet). Platform-prefixed to stay collision-proof.
 */
typedef struct qa_platform_ipx_address {
  uint32_t network;
  uint8_t node[6];
  uint16_t port;
} qa_platform_ipx_address;

/* Mirror of the NativeIpxCapability bind options from
 * src/network/common/ipx-host.ts (TS owner; no C home yet). */
typedef struct qa_platform_ipx_bind_options {
  double port;
  double packet_type;
  bool broadcast; /* must be true */
} qa_platform_ipx_bind_options;

typedef struct qa_platform_ipx_error {
  bool unsupported; /* UnsupportedTransportError("ipx-native", ...) */
  char message[320];
} qa_platform_ipx_error;

/* Mirror of ReceiveEvent<IpxAddress> from
 * src/network/common/transport.ts (TS owner; no C home yet). */
typedef enum qa_platform_ipx_receive_kind {
  QA_PLATFORM_IPX_RECEIVE_NONE,    /* TS null */
  QA_PLATFORM_IPX_RECEIVE_PACKET,
  QA_PLATFORM_IPX_RECEIVE_DROPPED, /* reason is always oversize here */
  QA_PLATFORM_IPX_RECEIVE_ERROR,
} qa_platform_ipx_receive_kind;

typedef struct qa_platform_ipx_receive_event {
  qa_platform_ipx_receive_kind kind;
  qa_platform_ipx_address from;
  uint8_t *payload; /* owned on packet; dispose the event */
  size_t payload_length;
  double received_at; /* performance.now() milliseconds */
  qa_platform_ipx_error error; /* on error */
} qa_platform_ipx_receive_event;

typedef struct qa_native_ipx_socket {
  int descriptor;
  qa_platform_ipx_address address;
  int64_t max_datagram_bytes;
  int packet_type;
  bool opened;
} qa_native_ipx_socket;

/* Validates like TS ipxAddress/portNumber (port must be 1..65535). */
qa_platform_ipx_address qa_platform_ipx_address_make(uint32_t network,
                                                     const uint8_t node[6],
                                                     double port);
void qa_platform_ipx_receive_event_dispose(qa_platform_ipx_receive_event *event);

bool qa_bind_native_ipx_socket(const qa_platform_ipx_bind_options *options,
                               qa_native_ipx_socket *out,
                               qa_platform_ipx_error *error);
/* False with *out_sent false on would-block; false+error on failure. */
bool qa_native_ipx_socket_send(qa_native_ipx_socket *socket,
                               const qa_platform_ipx_address *to,
                               const uint8_t *payload, size_t length,
                               bool *out_sent, qa_platform_ipx_error *error);
bool qa_native_ipx_socket_receive(qa_native_ipx_socket *socket,
                                  qa_platform_ipx_receive_event *out,
                                  qa_platform_ipx_error *error);
bool qa_native_ipx_socket_readable(qa_native_ipx_socket *socket, bool *out,
                                   qa_platform_ipx_error *error);
bool qa_native_ipx_socket_close(qa_native_ipx_socket *socket,
                                qa_platform_ipx_error *error);

#endif

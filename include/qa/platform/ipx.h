/*
 * Port of src/platform/ipx.ts: NativeIpxTransport over the native
 * IPX socket plus the bunNativeIpxCapability platform gate. C
 * accommodations: the async dynamic-import bind is a synchronous
 * direct call; the 4 ms readability timer and queueMicrotask become
 * an explicit pump the owner calls (PORTING.md pumped state);
 * listener unsubscribe closures become subscription ids; the
 * latched Error becomes the platform error record. Loading this
 * header acquires no sockets. GPL-2.0-or-later.
 */
#ifndef QA_PLATFORM_IPX_H
#define QA_PLATFORM_IPX_H

#include "qa/platform/ipx_native.h"

typedef void (*qa_ipx_readable_fn)(void *ctx);

typedef enum qa_native_ipx_capability_kind {
  QA_NATIVE_IPX_UNAVAILABLE,
  QA_NATIVE_IPX_AVAILABLE,
} qa_native_ipx_capability_kind;

/* Mirror of NativeIpxCapability from src/network/common/ipx-host.ts
 * (TS owner; no C home yet), platform-prefixed. */
typedef struct qa_platform_native_ipx_capability {
  qa_native_ipx_capability_kind kind;
  char reason[256]; /* when unavailable */
} qa_platform_native_ipx_capability;

typedef struct qa_ipx_listener {
  qa_ipx_readable_fn fn;
  void *ctx;
  size_t id;
} qa_ipx_listener;

typedef struct qa_native_ipx_transport {
  qa_native_ipx_socket socket;
  bool ended;
  bool has_fault;
  qa_platform_ipx_error fault;
  bool fault_reported;
  qa_ipx_listener *listeners;
  size_t listener_count;
  size_t listener_capacity;
  size_t next_listener_id;
  bool pumping;       /* TS timer armed */
  bool pending_notify; /* TS queued microtask */
} qa_native_ipx_transport;

qa_platform_native_ipx_capability qa_bun_native_ipx_capability(void);
/* available-kind bind; wraps failures like the TypeScript catch. */
bool qa_native_ipx_capability_bind(
    const qa_platform_native_ipx_capability *capability,
    const qa_platform_ipx_bind_options *options, qa_native_ipx_transport *out,
    qa_platform_ipx_error *error);

const qa_platform_ipx_address *qa_native_ipx_transport_address(
    const qa_native_ipx_transport *transport);
int64_t qa_native_ipx_transport_max_datagram_bytes(
    const qa_native_ipx_transport *transport);
bool qa_native_ipx_transport_closed(const qa_native_ipx_transport *transport);
bool qa_native_ipx_transport_send(qa_native_ipx_transport *transport,
                                  const qa_platform_ipx_address *to,
                                  const uint8_t *payload, size_t length);
/* kind NONE is TS null; a latched fault is reported once as ERROR. */
void qa_native_ipx_transport_poll(qa_native_ipx_transport *transport,
                                  qa_platform_ipx_receive_event *out);
/* Returns the subscription id for qa_native_ipx_transport_unsubscribe. */
size_t qa_native_ipx_transport_subscribe_readable(
    qa_native_ipx_transport *transport, qa_ipx_readable_fn fn, void *ctx);
void qa_native_ipx_transport_unsubscribe(qa_native_ipx_transport *transport,
                                         size_t id);
/* One TS timer tick (readability probe + notify) plus queued notify. */
void qa_native_ipx_transport_pump(qa_native_ipx_transport *transport);
void qa_native_ipx_transport_close(qa_native_ipx_transport *transport);

#endif

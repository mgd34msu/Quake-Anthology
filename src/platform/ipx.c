/* Port of src/platform/ipx.ts. GPL-2.0-or-later. */
#include "qa/platform/ipx.h"

#include <stdarg.h>
#include <stdnoreturn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "qa/foundation/support.h"

/* TS `throw new Error(msg)`, as in src/text/atlas.c. */
static noreturn void qa_error(const char *format, ...) {
  char text[1536];
  va_list args;
  va_start(args, format);
  vsnprintf(text, sizeof text, format, args);
  va_end(args);
  qa_raise("Error", 0, 0, "%s", text);
}

static const char *host_platform(void) {
#ifdef __linux__
  return "linux";
#elif defined(_WIN32)
  return "win32";
#else
  return "unknown";
#endif
}

static const char *host_arch(void) {
#if defined(__x86_64__)
  return "x64";
#elif defined(__aarch64__)
  return "arm64";
#else
  return "unknown";
#endif
}

qa_platform_native_ipx_capability qa_bun_native_ipx_capability(void) {
  qa_platform_native_ipx_capability capability;
  memset(&capability, 0, sizeof capability);
  bool linux = strcmp(host_platform(), "linux") == 0;
  bool windows = strcmp(host_platform(), "win32") == 0;
  if (!linux && !windows) {
    capability.kind = QA_NATIVE_IPX_UNAVAILABLE;
    snprintf(capability.reason, sizeof capability.reason,
             "No AF_IPX socket ABI is implemented for %s", host_platform());
    return capability;
  }
  bool arch_ok = windows ? strcmp(host_arch(), "x64") == 0
                         : strcmp(host_arch(), "x64") == 0 ||
                               strcmp(host_arch(), "arm64") == 0;
  /* The C port implements only the Linux socket; a Windows build
   * would report available here in the TypeScript, so gate it the
   * same way and let bind fail until that branch is ported. */
  if (!arch_ok || !linux) {
    capability.kind = QA_NATIVE_IPX_UNAVAILABLE;
    snprintf(capability.reason, sizeof capability.reason,
             "Native IPX requires Linux x64/arm64 glibc or Windows x64 "
             "Winsock; this host is %s/%s",
             host_platform(), host_arch());
    return capability;
  }
  capability.kind = QA_NATIVE_IPX_AVAILABLE;
  return capability;
}

bool qa_native_ipx_capability_bind(
    const qa_platform_native_ipx_capability *capability,
    const qa_platform_ipx_bind_options *options, qa_native_ipx_transport *out,
    qa_platform_ipx_error *error) {
  memset(out, 0, sizeof *out);
  if (capability->kind != QA_NATIVE_IPX_AVAILABLE) {
    if (error != NULL) {
      error->unsupported = true;
      snprintf(error->message, sizeof error->message, "ipx-native: %s",
               capability->reason);
    }
    return false;
  }
  qa_platform_ipx_error inner;
  memset(&inner, 0, sizeof inner);
  if (!qa_bind_native_ipx_socket(options, &out->socket, &inner)) {
    if (error != NULL) {
      if (inner.unsupported) {
        *error = inner;
      } else {
        error->unsupported = false;
        snprintf(error->message, sizeof error->message,
                 "Native IPX %s/%s bind failed: %.240s", host_platform(),
                 host_arch(), inner.message);
      }
    }
    return false;
  }
  return true;
}

static void open_check(const qa_native_ipx_transport *transport) {
  if (transport->ended) qa_error("Native IPX socket is closed");
}

static void latch_fault(qa_native_ipx_transport *transport,
                        const qa_platform_ipx_error *error) {
  if (!transport->has_fault) {
    transport->fault = *error;
    transport->has_fault = true;
  }
  transport->pumping = false; /* TS stopTimer */
}

static void notify(qa_native_ipx_transport *transport) {
  /* Snapshot like [...this.listeners]; skip ones removed meanwhile. */
  size_t count = transport->listener_count;
  qa_ipx_listener *snapshot = NULL;
  if (count != 0) {
    snapshot = (qa_ipx_listener *)malloc(count * sizeof *snapshot);
    if (snapshot == NULL) qa_range_error("out of memory");
    memcpy(snapshot, transport->listeners, count * sizeof *snapshot);
  }
  for (size_t i = 0; i < count; i++) {
    bool present = false;
    for (size_t j = 0; j < transport->listener_count; j++) {
      if (transport->listeners[j].id == snapshot[i].id) present = true;
    }
    if (present) snapshot[i].fn(snapshot[i].ctx);
  }
  free(snapshot);
}

const qa_platform_ipx_address *qa_native_ipx_transport_address(
    const qa_native_ipx_transport *transport) {
  return &transport->socket.address;
}

int64_t qa_native_ipx_transport_max_datagram_bytes(
    const qa_native_ipx_transport *transport) {
  return transport->socket.max_datagram_bytes;
}

bool qa_native_ipx_transport_closed(const qa_native_ipx_transport *transport) {
  return transport->ended;
}

bool qa_native_ipx_transport_send(qa_native_ipx_transport *transport,
                                  const qa_platform_ipx_address *to,
                                  const uint8_t *payload, size_t length) {
  open_check(transport);
  (void)qa_platform_ipx_address_make(to->network, to->node, (double)to->port);
  if ((int64_t)length > transport->socket.max_datagram_bytes)
    qa_range_error("Datagram exceeds native IPX payload limit");
  if (transport->has_fault) return false;
  bool sent = false;
  qa_platform_ipx_error error;
  memset(&error, 0, sizeof error);
  if (!qa_native_ipx_socket_send(&transport->socket, to, payload, length, &sent,
                                 &error)) {
    latch_fault(transport, &error);
    notify(transport);
    return false;
  }
  return sent;
}

void qa_native_ipx_transport_poll(qa_native_ipx_transport *transport,
                                  qa_platform_ipx_receive_event *out) {
  open_check(transport);
  memset(out, 0, sizeof *out);
  out->kind = QA_PLATFORM_IPX_RECEIVE_NONE;
  if (!transport->has_fault) {
    qa_platform_ipx_error error;
    memset(&error, 0, sizeof error);
    if (!qa_native_ipx_socket_receive(&transport->socket, out, &error))
      latch_fault(transport, &error);
    else if (out->kind != QA_PLATFORM_IPX_RECEIVE_NONE)
      return;
  }
  if (transport->has_fault && !transport->fault_reported) {
    transport->fault_reported = true;
    memset(out, 0, sizeof *out);
    out->kind = QA_PLATFORM_IPX_RECEIVE_ERROR;
    out->error = transport->fault;
    return;
  }
  memset(out, 0, sizeof *out);
  out->kind = QA_PLATFORM_IPX_RECEIVE_NONE;
}

size_t qa_native_ipx_transport_subscribe_readable(
    qa_native_ipx_transport *transport, qa_ipx_readable_fn fn, void *ctx) {
  open_check(transport);
  if (transport->listener_count == transport->listener_capacity) {
    size_t capacity = transport->listener_capacity == 0
                          ? 8
                          : transport->listener_capacity * 2;
    qa_ipx_listener *next = (qa_ipx_listener *)realloc(transport->listeners,
                                                       capacity * sizeof *next);
    if (next == NULL) qa_range_error("out of memory");
    transport->listeners = next;
    transport->listener_capacity = capacity;
  }
  size_t id = ++transport->next_listener_id;
  transport->listeners[transport->listener_count].fn = fn;
  transport->listeners[transport->listener_count].ctx = ctx;
  transport->listeners[transport->listener_count].id = id;
  transport->listener_count++;
  if (!transport->pumping && !transport->has_fault) transport->pumping = true;
  if (transport->has_fault && !transport->fault_reported)
    transport->pending_notify = true;
  return id;
}

void qa_native_ipx_transport_unsubscribe(qa_native_ipx_transport *transport,
                                         size_t id) {
  for (size_t i = 0; i < transport->listener_count; i++) {
    if (transport->listeners[i].id == id) {
      memmove(&transport->listeners[i], &transport->listeners[i + 1],
              (transport->listener_count - i - 1) * sizeof(qa_ipx_listener));
      transport->listener_count--;
      break;
    }
  }
  if (transport->listener_count == 0) transport->pumping = false;
}

void qa_native_ipx_transport_pump(qa_native_ipx_transport *transport) {
  if (transport->ended) return;
  if (transport->pending_notify) {
    transport->pending_notify = false;
    notify(transport);
    return;
  }
  if (!transport->pumping) return;
  bool ready = false;
  qa_platform_ipx_error error;
  memset(&error, 0, sizeof error);
  if (!qa_native_ipx_socket_readable(&transport->socket, &ready, &error)) {
    latch_fault(transport, &error);
    ready = true;
  }
  if (ready) notify(transport);
}

void qa_native_ipx_transport_close(qa_native_ipx_transport *transport) {
  if (transport->ended) return;
  transport->ended = true;
  transport->pumping = false;
  qa_platform_ipx_error error;
  memset(&error, 0, sizeof error);
  bool ok = qa_native_ipx_socket_close(&transport->socket, &error);
  notify(transport);
  free(transport->listeners);
  transport->listeners = NULL;
  transport->listener_count = 0;
  transport->listener_capacity = 0;
  if (!ok) qa_error("%s", error.message);
}

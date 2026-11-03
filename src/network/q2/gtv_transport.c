/* Port of src/network/q2/gtv-transport.ts. GPL-2.0-or-later. */
#include "qa/network/q2/gtv_transport.h"

#include <arpa/inet.h>
#include <errno.h>
#include <math.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "qa/foundation/support.h"
#include "qa/network/q2/constants.h"

static double now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (double)ts.tv_sec * 1000 + (double)ts.tv_nsec / 1000000;
}
static bool send_all(int fd, const uint8_t *bytes, size_t length) {
  size_t sent = 0;
  while (sent < length) {
    ssize_t n = send(fd, bytes + sent, length - sent, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) continue;
      return false;
    }
    if (n == 0) return false;
    sent += (size_t)n;
  }
  return true;
}
/* TS GtvClient send closure. */
static void connection_send(void *vctx, const uint8_t *bytes, size_t length) {
  qa_q2_gtv_connection *c = (qa_q2_gtv_connection *)vctx;
  if (c->closed) qa_q2_throw_error("GTV connection closed");
  int queued = 0;
  if (ioctl(c->socket_fd, TIOCOUTQ, &queued) == 0 &&
      (double)queued + (double)length > 65536) {
    qa_q2_throw_error("GTV send queue overflow");
  }
  if (!send_all(c->socket_fd, bytes, length)) {
    qa_q2_throw_error("GTV transport failed");
  }
}
static void dispatch(qa_q2_gtv_connection *c, const qa_q2_gtv_event *event) {
  if (c->options.event != NULL) c->options.event(c->options.event_ctx, event);
}
void qa_q2_gtv_connection_close(qa_q2_gtv_connection *c) {
  if (c->closed) return;
  c->closed = true;
  c->next_ping_ms = 0;
  if (c->client_ready) qa_q2_gtv_client_close(&c->client);
  if (c->socket_fd >= 0) {
    (void)shutdown(c->socket_fd, SHUT_RDWR);
    (void)close(c->socket_fd);
    c->socket_fd = -1;
  }
}
typedef struct dispatch_scope {
  qa_q2_gtv_connection *c;
  const qa_q2_gtv_event *event;
} dispatch_scope;
static void dispatch_body(void *arg) {
  dispatch_scope *scope = (dispatch_scope *)arg;
  dispatch(scope->c, scope->event);
}
/* TS fail(): before the hello the ready promise rejects (a raise out of connect); after it, one
 * closed event is delivered and its handler's failure is ignored (.catch(() => {})). */
static void connection_fail(qa_q2_gtv_connection *c, const char *reason) {
  if (c->closed) return;
  bool opened = c->opened;
  qa_q2_gtv_connection_close(c);
  if (!opened) qa_q2_throw_error("%s", reason);
  qa_q2_gtv_event event;
  memset(&event, 0, sizeof event);
  event.kind = QA_Q2_GTV_EVENT_CLOSED;
  size_t n = strlen(reason);
  event.reason = (char *)malloc(n + 1);
  if (event.reason != NULL) memcpy(event.reason, reason, n + 1);
  dispatch_scope scope = {c, &event};
  qa_caught_error caught;
  (void)qa_try(dispatch_body, &scope, &caught);
  free(event.reason);
}
typedef struct chunk_events {
  qa_q2_gtv_event *events;
  size_t count;
} chunk_events;
static void chunk_events_release(void *arg) {
  chunk_events *held = (chunk_events *)arg;
  qa_q2_gtv_events_free(held->events, held->count);
}
/* Process one received chunk through the client; returns dispatched
 * count. Mirrors the TS data handler's per-event loop. */
static size_t process_chunk(qa_q2_gtv_connection *c, const uint8_t *bytes,
                            size_t length) {
  chunk_events held = {NULL, 0};
  if (!qa_q2_gtv_client_receive(&c->client, bytes, length, &held.events, &held.count)) {
    qa_range_error("out of memory");
  }
  qa_cleanup node;
  qa_cleanup_push(&node, chunk_events_release, &held);
  size_t dispatched = 0;
  for (size_t i = 0; i < held.count; i++) {
    if (held.events[i].kind == QA_Q2_GTV_EVENT_HELLO) {
      c->opened = true;
      c->next_ping_ms = now_ms() + 60000;
    }
    dispatch(c, &held.events[i]);
    dispatched++;
    if (held.events[i].kind == QA_Q2_GTV_EVENT_CLOSED) {
      qa_q2_gtv_connection_close(c);
      break;
    }
  }
  qa_cleanup_pop(&node);
  qa_q2_gtv_events_free(held.events, held.count);
  return dispatched;
}
typedef struct chunk_scope {
  qa_q2_gtv_connection *c;
  const uint8_t *bytes;
  size_t length, dispatched;
} chunk_scope;
static void chunk_body(void *arg) {
  chunk_scope *scope = (chunk_scope *)arg;
  scope->dispatched = process_chunk(scope->c, scope->bytes, scope->length);
}
/* One recv step. Returns false when the connection has failed/closed. */
static bool receive_step(qa_q2_gtv_connection *c, size_t *dispatched) {
  *dispatched = 0;
  uint8_t buffer[65536];
  ssize_t n = recv(c->socket_fd, buffer, sizeof buffer, 0);
  if (n == 0) {
    connection_fail(c, "GTV peer ended stream");
    return false;
  }
  if (n < 0) {
    if (errno == EINTR) return true;
    if (errno == EAGAIN || errno == EWOULDBLOCK) {
      connection_fail(c, "GTV transport timed out");
    } else {
      connection_fail(c, "GTV transport failed");
    }
    return false;
  }
  if (c->closed) return false;
  /* TS processing.catch(error => fail(error)): a malformed stream or a failing event handler
   * fails this connection with that error's message. */
  chunk_scope scope = {c, buffer, (size_t)n, 0};
  qa_caught_error caught;
  if (!qa_try(chunk_body, &scope, &caught)) {
    *dispatched = scope.dispatched;
    connection_fail(c, caught.message);
    return false;
  }
  *dispatched = scope.dispatched;
  return !c->closed;
}

bool qa_q2_gtv_connection_connect(qa_q2_gtv_connection *c,
                                  qa_q2_gtv_connection_options options) {
  double timeout = options.timeout_milliseconds < 0 ? 90000
                                                     : options.timeout_milliseconds;
  if (options.port != trunc(options.port) || options.port < 1 ||
      options.port > 65535 || options.host == NULL || options.host[0] == '\0' ||
      timeout != trunc(timeout) || timeout < 1 || timeout > 9007199254740991.0) {
    qa_range_error("Invalid GTV endpoint or timeout");
  }
  memset(c, 0, sizeof *c);
  c->socket_fd = -1;
  c->options = options;
  if (!qa_q2_gtv_client_init(&c->client, connection_send, c, options.identity,
                             -1)) {
    return false;
  }
  c->client_ready = true;
  if (options.aborted != NULL && *options.aborted) {
    connection_fail(c, "GTV connection cancelled");
  }
  struct addrinfo hints;
  memset(&hints, 0, sizeof hints);
  hints.ai_socktype = SOCK_STREAM;
  hints.ai_family = AF_UNSPEC;
  char port_text[16];
  (void)snprintf(port_text, sizeof port_text, "%lld", (long long)options.port);
  struct addrinfo *addresses = NULL;
  if (getaddrinfo(options.host, port_text, &hints, &addresses) != 0) {
    connection_fail(c, "GTV connection failed");
    return false;
  }
  int fd = -1;
  for (struct addrinfo *at = addresses; at != NULL; at = at->ai_next) {
    fd = socket(at->ai_family, at->ai_socktype, at->ai_protocol);
    if (fd < 0) continue;
    struct timeval tv;
    tv.tv_sec = (time_t)(timeout / 1000);
    tv.tv_usec = (suseconds_t)((timeout - (double)tv.tv_sec * 1000) * 1000);
    (void)setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
    (void)setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    int one = 1;
    (void)setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
    if (connect(fd, at->ai_addr, at->ai_addrlen) == 0) break;
    (void)close(fd);
    fd = -1;
  }
  freeaddrinfo(addresses);
  if (fd < 0) {
    connection_fail(c, "GTV connection failed");
    return false;
  }
  c->socket_fd = fd;
  qa_q2_gtv_client_start(&c->client);
  while (!c->opened) {
    if (options.aborted != NULL && *options.aborted) {
      connection_fail(c, "GTV connection cancelled");
    }
    if (c->client.phase == QA_Q2_GTV_CLOSED) {
      /* A server close before the hello rejects the ready promise. */
      connection_fail(c, "GTV connection closed");
    }
    size_t dispatched = 0;
    if (!receive_step(c, &dispatched)) {
      if (!c->opened) connection_fail(c, "GTV connection closed");
      break;
    }
  }
  return true;
}

void qa_q2_gtv_connection_destroy(qa_q2_gtv_connection *c) {
  qa_q2_gtv_connection_close(c);
  if (c->client_ready) qa_q2_gtv_client_destroy(&c->client);
  memset(c, 0, sizeof *c);
  c->socket_fd = -1;
}

static void ping_body(void *arg) {
  qa_q2_gtv_client_ping(&((qa_q2_gtv_connection *)arg)->client);
}

size_t qa_q2_gtv_connection_poll(qa_q2_gtv_connection *c,
                                 double wait_milliseconds) {
  if (c->closed || c->socket_fd < 0) return 0;
  if (c->options.aborted != NULL && *c->options.aborted) {
    connection_fail(c, "GTV connection cancelled");
    return 0;
  }
  if (c->next_ping_ms > 0 && now_ms() >= c->next_ping_ms) {
    c->next_ping_ms = now_ms() + 60000;
    /* TS: try { ping() } catch { fail(new Error('GTV keepalive failed')) } */
    if (!c->closed) {
      qa_caught_error caught;
      if (!qa_try(ping_body, c, &caught)) {
        connection_fail(c, "GTV keepalive failed");
        return 0;
      }
    }
  }
  struct pollfd item;
  item.fd = c->socket_fd;
  item.events = POLLIN;
  item.revents = 0;
  /* poll() needs an int: NaN and negatives wait 0, huge waits saturate. */
  int wait = !(wait_milliseconds > 0) ? 0 : wait_milliseconds >= 2147483647.0 ? 2147483647 : (int)wait_milliseconds;
  int ready = poll(&item, 1, wait);
  if (ready <= 0) return 0;
  if ((item.revents & (POLLERR | POLLNVAL)) != 0) {
    connection_fail(c, "GTV transport failed");
    return 0;
  }
  size_t dispatched = 0;
  (void)receive_step(c, &dispatched);
  return dispatched;
}

bool qa_q2_gtv_connection_get_closed(const qa_q2_gtv_connection *c) {
  return c->closed;
}
void qa_q2_gtv_connection_start(qa_q2_gtv_connection *c,
                                double max_buffered_packets) {
  qa_q2_gtv_client_request_start(&c->client, max_buffered_packets);
}
void qa_q2_gtv_connection_stop(qa_q2_gtv_connection *c) {
  qa_q2_gtv_client_request_stop(&c->client);
}
void qa_q2_gtv_connection_command(qa_q2_gtv_connection *c, const char *text) {
  qa_q2_gtv_client_command(&c->client, text);
}
void qa_q2_gtv_connection_ping(qa_q2_gtv_connection *c) {
  qa_q2_gtv_client_ping(&c->client);
}


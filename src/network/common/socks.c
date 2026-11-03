/* Port of src/network/common/socks.ts. GPL-2.0-or-later. */
#define _DEFAULT_SOURCE
#include "qa/network/common/socks.h"
#include "qa/foundation/support.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

uint8_t *qa_socks_datagram(const qa_net_address *to, const uint8_t *payload, size_t length, size_t *out_len) {
  uint8_t *b = malloc(length + 10); if (!b) return NULL;
  memset(b, 0, 10); b[3] = 1; memcpy(b + 4, to->ipv4, 4);
  b[8] = (uint8_t)((to->port >> 8) & 255); b[9] = (uint8_t)(to->port & 255);
  if (length) memcpy(b + 10, payload, length);
  *out_len = length + 10; return b;
}
int qa_socks_read_datagram(const uint8_t *bytes, size_t length, qa_net_address *from,
                           uint8_t **payload, size_t *payload_len) {
  if (length < 10 || bytes[0] || bytes[1] || bytes[2] || bytes[3] != 1) return 0;
  /* TS constructs the record directly, so port 0 is preserved here. */
  memset(from, 0, sizeof *from); from->kind = QA_NET_IPV4;
  memcpy(from->ipv4, bytes + 4, 4);
  from->port = ((int64_t)bytes[8] << 8) | bytes[9];
  *payload_len = length - 10;
  *payload = malloc(*payload_len ? *payload_len : 1);
  if (!*payload) return 0;
  if (*payload_len) memcpy(*payload, bytes + 10, *payload_len);
  return 1;
}
int qa_socks_association_init(qa_socks_association *a) { memset(a, 0, sizeof *a); a->fd = -1; return 1; }
void qa_socks_association_destroy(qa_socks_association *a) { qa_socks_association_close(a); }

/* TS fail(): the first failure wins; the relay disappears and the socket is destroyed. */
static void socks_fail(qa_socks_association *a, const char *message) {
  if (a->failed) return;
  a->failed = true; a->failure = message; a->has_relay = false;
  if (a->fd >= 0) { close(a->fd); a->fd = -1; }
}
/* The socket's data/error/end events: drains what the control stream delivered. */
static void socks_service(qa_socks_association *a) {
  while (!a->failed && a->fd >= 0) {
    uint8_t chunk[1024];
    ssize_t n = recv(a->fd, chunk, sizeof chunk, MSG_DONTWAIT);
    if (n > 0) {
      /* Only bounded handshake replies are expected on this control stream. */
      if (a->control_length + (size_t)n > sizeof a->control) { socks_fail(a, "SOCKS control response exceeds limit"); return; }
      memcpy(a->control + a->control_length, chunk, (size_t)n); a->control_length += (size_t)n;
      continue;
    }
    if (n == 0) { socks_fail(a, "SOCKS control connection ended"); return; }
    if (errno == EINTR) continue;
    if (errno != EAGAIN && errno != EWOULDBLOCK) socks_fail(a, "SOCKS control connection failed");
    return;
  }
}
int qa_socks_association_get_relay(qa_socks_association *a, qa_net_address *out) {
  socks_service(a);
  if (a->failed || !a->has_relay) return 0;
  return qa_net_address_clone(&a->relay, out);
}
void qa_socks_association_close(qa_socks_association *a) { socks_fail(a, "SOCKS association closed"); }

static double monotonic_ms(void) {
  struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
  return (double)now.tv_sec * 1000.0 + (double)now.tv_nsec / 1e6;
}
/* setTimeout(5000) over the whole negotiation. */
static void socks_wait(qa_socks_association *a, double deadline, short events) {
  double remaining = deadline - monotonic_ms();
  if (remaining <= 0) { socks_fail(a, "SOCKS negotiation timed out"); return; }
  struct pollfd pfd = { a->fd, events, 0 };
  int ready = poll(&pfd, 1, (int)remaining + 1);
  if (ready == 0 && monotonic_ms() >= deadline) socks_fail(a, "SOCKS negotiation timed out");
}
static void socks_read(qa_socks_association *a, uint8_t *out, size_t length, double deadline) {
  for (;;) {
    if (a->failed) qa_raise("Error", 0, 0, "%s", a->failure);
    if (a->control_length >= length) {
      memcpy(out, a->control, length);
      memmove(a->control, a->control + length, a->control_length - length);
      a->control_length -= length;
      return;
    }
    socks_wait(a, deadline, POLLIN);
    socks_service(a);
  }
}
/* socket.write: a failed write is the socket's error event; the next read reports it. */
static void socks_write(qa_socks_association *a, const uint8_t *bytes, size_t length, double deadline) {
  size_t sent = 0;
  while (!a->failed && sent < length) {
    ssize_t n = send(a->fd, bytes + sent, length - sent, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (n > 0) { sent += (size_t)n; continue; }
    if (n < 0 && errno == EINTR) continue;
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) { socks_wait(a, deadline, POLLOUT); continue; }
    socks_fail(a, "SOCKS control connection failed");
  }
}
/* credentialBytes: one byte per UTF-16 code unit below 256 (C text is UTF-8). */
static size_t credential_bytes(const char *text, uint8_t out[255]) {
  size_t units = 0;
  const unsigned char *p = (const unsigned char *)text;
  for (const unsigned char *q = p; *q; q++) if ((*q & 0xc0) != 0x80) units += (*q >= 0xf0) ? 2 : 1;
  if (units > 255) qa_raise("Error", 0, 0, "SOCKS credential exceeds 255 bytes");
  size_t n = 0;
  while (*p) {
    uint32_t cp; size_t need;
    if (*p < 0x80) { cp = *p; need = 1; }
    else if ((*p & 0xe0) == 0xc0 && (p[1] & 0xc0) == 0x80) { cp = (uint32_t)(*p & 0x1f) << 6 | (p[1] & 0x3f); need = 2; }
    else { cp = 0x100; need = 1; while (p[need] && (p[need] & 0xc0) == 0x80) need++; }
    if (cp == 0 || cp > 255) qa_raise("Error", 0, 0, "SOCKS credentials require non-NUL source bytes");
    out[n++] = (uint8_t)cp; p += need;
  }
  return n;
}
struct open_scope { qa_socks_association *a; const qa_socks_options *opts; int64_t local_port; };
static void socks_open_body(void *ctx) {
  struct open_scope *scope = ctx;
  qa_socks_association *a = scope->a; const qa_socks_options *opts = scope->opts;
  double deadline = monotonic_ms() + 5000;
  if (a->failed) qa_raise("Error", 0, 0, "%s", a->failure);
  if (!opts->server || !opts->server[0] || opts->port < 1 || opts->port > 65535)
    qa_raise("Error", 0, 0, "SOCKS server and port are invalid");
  uint8_t username[255], password[255];
  size_t ul = credential_bytes(opts->username, username), pl = credential_bytes(opts->password, password);
  bool authenticated = ul != 0 || pl != 0;
  /* socket.connect({ family: 4 }): resolution and connection failures are the socket's
   * error event, observed by the first read. */
  struct addrinfo hints; memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = NULL; char portbuf[16]; snprintf(portbuf, sizeof portbuf, "%lld", (long long)opts->port);
  if (getaddrinfo(opts->server, portbuf, &hints, &res) != 0 || !res) socks_fail(a, "SOCKS control connection failed");
  else {
    a->fd = socket(res->ai_family, res->ai_socktype | SOCK_NONBLOCK, res->ai_protocol);
    if (a->fd < 0) socks_fail(a, "SOCKS control connection failed");
    else if (connect(a->fd, res->ai_addr, res->ai_addrlen) != 0 && errno != EINPROGRESS) socks_fail(a, "SOCKS control connection failed");
    freeaddrinfo(res);
    if (!a->failed) {
      socks_wait(a, deadline, POLLOUT);
      int error = 0; socklen_t length = sizeof error;
      if (!a->failed && (getsockopt(a->fd, SOL_SOCKET, SO_ERROR, &error, &length) != 0 || error != 0)) socks_fail(a, "SOCKS control connection failed");
    }
  }
  /* Repair the source's overwritten buf[2]/uninitialized buf[3] greeting.
   * RFC 1928 lists both intended methods explicitly, including no-auth. */
  static const uint8_t with_auth[4] = {5, 2, 0, 2}, without_auth[3] = {5, 1, 0};
  if (!a->failed) socks_write(a, authenticated ? with_auth : without_auth, authenticated ? 4 : 3, deadline);
  uint8_t method[2]; socks_read(a, method, 2, deadline);
  if (method[0] != 5 || (method[1] != 0 && (method[1] != 2 || !authenticated))) qa_raise("Error", 0, 0, "SOCKS authentication method rejected");
  if (method[1] == 2) {
    /* Preserve source empty fields if the proxy selects authentication. */
    uint8_t request[3 + 255 + 255]; request[0] = 1; request[1] = (uint8_t)ul;
    memcpy(request + 2, username, ul); request[2 + ul] = (uint8_t)pl; memcpy(request + 3 + ul, password, pl);
    socks_write(a, request, 3 + ul + pl, deadline);
    uint8_t reply[2]; socks_read(a, reply, 2, deadline);
    if (reply[0] != 1 || reply[1] != 0) qa_raise("Error", 0, 0, "SOCKS authentication failed");
  }
  uint32_t local = (uint32_t)scope->local_port;
  uint8_t request[10] = {5, 3, 0, 1, 0, 0, 0, 0, (uint8_t)((local >> 8) & 255), (uint8_t)(local & 255)};
  socks_write(a, request, 10, deadline);
  uint8_t header[4]; socks_read(a, header, 4, deadline);
  if (header[0] != 5 || header[1] != 0 || header[2] != 0) qa_raise("Error", 0, 0, "SOCKS UDP association rejected");
  if (header[3] != 1) qa_raise("Error", 0, 0, "SOCKS relay address is not IPv4");
  uint8_t reply[6]; socks_read(a, reply, 6, deadline);
  int64_t port = ((int64_t)reply[4] << 8) | reply[5];
  if (port == 0) qa_raise("Error", 0, 0, "SOCKS relay port is zero");
  memset(&a->relay, 0, sizeof a->relay); a->relay.kind = QA_NET_IPV4;
  memcpy(a->relay.ipv4, reply, 4); a->relay.port = port; a->has_relay = true;
}
void qa_socks_association_open(qa_socks_association *a, const qa_socks_options *opts, int64_t local_port) {
  if (a->started) qa_raise("Error", 0, 0, "SOCKS association has already started");
  a->started = true;
  struct open_scope scope = { a, opts, local_port };
  qa_caught_error caught;
  if (!qa_try(socks_open_body, &scope, &caught)) { qa_socks_association_close(a); qa_rethrow(&caught); }
}

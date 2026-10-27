/* Port of src/network/common/socks.ts. GPL-2.0-or-later. */
#include "qa/network/common/socks.h"
#include "qa/foundation/support.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

uint8_t *qa_socks_datagram(const qa_net_address *to, const uint8_t *payload, size_t length, size_t *out_len) {
  uint8_t *b = malloc(length + 10); if (!b) return NULL;
  memset(b, 0, 10); b[3] = 1; memcpy(b + 4, to->ipv4, 4);
  b[8] = (uint8_t)((to->port >> 8) & 255); b[9] = (uint8_t)(to->port & 255);
  memcpy(b + 10, payload, length); *out_len = length + 10; return b;
}
int qa_socks_read_datagram(const uint8_t *bytes, size_t length, qa_net_address *from,
                           uint8_t **payload, size_t *payload_len) {
  if (length < 10 || bytes[0] || bytes[1] || bytes[2] || bytes[3] != 1) return 0;
  int64_t h[4] = { bytes[4], bytes[5], bytes[6], bytes[7] };
  /* TS constructs the record directly, so port 0 is preserved here. */
  memset(from, 0, sizeof *from); from->kind = QA_NET_IPV4;
  for (int i = 0; i < 4; i++) from->ipv4[i] = (uint8_t)h[i];
  from->port = ((int64_t)bytes[8] << 8) | bytes[9];
  *payload_len = length - 10;
  *payload = malloc(*payload_len ? *payload_len : 1);
  if (*payload_len) memcpy(*payload, bytes + 10, *payload_len);
  return 1;
}
int qa_socks_association_init(qa_socks_association *a) { memset(a, 0, sizeof *a); a->fd = -1; return 1; }
void qa_socks_association_destroy(qa_socks_association *a) { qa_socks_association_close(a); }
int qa_socks_association_get_relay(const qa_socks_association *a, qa_net_address *out) {
  if (a->failed || !a->has_relay) return 0;
  return qa_net_address_clone(&a->relay, out);
}
void qa_socks_association_close(qa_socks_association *a) {
  a->failed = true; a->has_relay = false;
  if (a->fd >= 0) { close(a->fd); a->fd = -1; }
}
static void cred_check(const char *text) {
  size_t n = strlen(text);
  if (n > 255) qa_range_error("SOCKS credential exceeds 255 bytes");
  for (size_t i = 0; i < n; i++) if ((uint8_t)text[i] == 0) qa_range_error("SOCKS credentials require non-NUL source bytes");
}
static void sock_read_exact(int fd, uint8_t *buf, size_t n) {
  size_t got = 0;
  while (got < n) { ssize_t r = recv(fd, buf + got, n - got, 0); if (r <= 0) qa_range_error("SOCKS control connection failed"); got += (size_t)r; }
}
static void sock_write_all(int fd, const uint8_t *buf, size_t n) {
  size_t sent = 0;
  while (sent < n) { ssize_t w = send(fd, buf + sent, n - sent, 0); if (w <= 0) qa_range_error("SOCKS control connection failed"); sent += (size_t)w; }
}
void qa_socks_association_open(qa_socks_association *a, const qa_socks_options *opts, int64_t local_port) {
  if (a->started) qa_range_error("SOCKS association has already started");
  a->started = true;
  if (!opts->server || !opts->server[0] || opts->port < 1 || opts->port > 65535)
    qa_range_error("SOCKS server and port are invalid");
  cred_check(opts->username); cred_check(opts->password);
  bool authed = opts->username[0] || opts->password[0];
  struct addrinfo hints; memset(&hints, 0, sizeof hints);
  hints.ai_family = AF_INET; hints.ai_socktype = SOCK_STREAM;
  struct addrinfo *res = NULL; char portbuf[16]; snprintf(portbuf, sizeof portbuf, "%lld", (long long)opts->port);
  if (getaddrinfo(opts->server, portbuf, &hints, &res) != 0 || !res) qa_range_error("SOCKS control connection failed");
  int fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
  if (fd < 0 || connect(fd, res->ai_addr, res->ai_addrlen) != 0) { freeaddrinfo(res); qa_range_error("SOCKS control connection failed"); }
  freeaddrinfo(res);
  struct timeval tv = { 5, 0 }; setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
  a->fd = fd;
  if (authed) { uint8_t g[4] = {5, 2, 0, 2}; sock_write_all(fd, g, 4); }
  else { uint8_t g[3] = {5, 1, 0}; sock_write_all(fd, g, 3); }
  uint8_t method[2]; sock_read_exact(fd, method, 2);
  if (method[0] != 5 || (method[1] != 0 && (method[1] != 2 || !authed))) qa_range_error("SOCKS authentication method rejected");
  if (method[1] == 2) {
    size_t ul = strlen(opts->username), pl = strlen(opts->password);
    uint8_t req[3 + 255 + 255]; req[0] = 1; req[1] = (uint8_t)ul;
    memcpy(req + 2, opts->username, ul); req[2 + ul] = (uint8_t)pl; memcpy(req + 3 + ul, opts->password, pl);
    sock_write_all(fd, req, 3 + ul + pl);
    uint8_t reply[2]; sock_read_exact(fd, reply, 2);
    if (reply[0] != 1 || reply[1] != 0) qa_range_error("SOCKS authentication failed");
  }
  uint8_t req[10] = {5, 3, 0, 1, 0, 0, 0, 0, (uint8_t)((local_port >> 8) & 255), (uint8_t)(local_port & 255)};
  sock_write_all(fd, req, 10);
  uint8_t header[4]; sock_read_exact(fd, header, 4);
  if (header[0] != 5 || header[1] != 0 || header[2] != 0) qa_range_error("SOCKS UDP association rejected");
  if (header[3] != 1) qa_range_error("SOCKS relay address is not IPv4");
  uint8_t reply[6]; sock_read_exact(fd, reply, 6);
  int64_t port = ((int64_t)reply[4] << 8) | reply[5];
  if (port == 0) qa_range_error("SOCKS relay port is zero");
  int64_t h[4] = { reply[0], reply[1], reply[2], reply[3] };
  (void)qa_net_make_ipv4(&a->relay, h, port, false); a->has_relay = true;
}

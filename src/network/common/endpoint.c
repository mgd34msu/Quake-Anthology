/* Port of src/network/common/endpoint.ts. GPL-2.0-or-later. */
#include "qa/network/common/endpoint.h"
#include "qa/foundation/support.h"
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

int64_t qa_net_port_number(int64_t port, bool allow_zero) {
  if (port < (allow_zero ? 0 : 1) || port > 65535) qa_range_error("Invalid network port");
  return port;
}
static char *dupstr(const char *s) { size_t n = strlen(s) + 1; char *p = malloc(n); if (p) memcpy(p, s, n); return p; }
int qa_net_make_ipv4(qa_net_address *out, const int64_t host[4], int64_t port, bool allow_zero) {
  for (int i = 0; i < 4; i++) if (host[i] < 0 || host[i] > 255) qa_range_error("Invalid IPv4 octet");
  memset(out, 0, sizeof *out); out->kind = QA_NET_IPV4;
  for (int i = 0; i < 4; i++) out->ipv4[i] = (uint8_t)host[i];
  out->port = qa_net_port_number(port, allow_zero); return 1;
}
int qa_net_make_ip(qa_net_address *out, const char *host, int64_t port, bool allow_zero) {
  struct in6_addr a6; struct in_addr a4;
  const char *pct = strchr(host, '%');
  char lit[256]; size_t ln = pct ? (size_t)(pct - host) : strlen(host);
  if (ln >= sizeof lit) ln = sizeof lit - 1;
  memcpy(lit, host, ln); lit[ln] = '\0';
  if (inet_pton(AF_INET6, lit, &a6) == 1) {
    char norm[INET6_ADDRSTRLEN]; inet_ntop(AF_INET6, &a6, norm, sizeof norm);
    memset(out, 0, sizeof *out); out->kind = QA_NET_IPV6;
    if (pct) { size_t need = strlen(norm) + strlen(pct) + 1; out->host = malloc(need); if (!out->host) return 0; snprintf(out->host, need, "%s%s", norm, pct); }
    else { out->host = dupstr(norm); if (!out->host) return 0; }
    out->port = qa_net_port_number(port, allow_zero); return 1;
  }
  if (inet_pton(AF_INET, host, &a4) != 1) qa_range_error("Expected an IP literal: %s", host);
  const uint8_t *b = (const uint8_t *)&a4.s_addr;
  int64_t h[4] = { b[0], b[1], b[2], b[3] };
  return qa_net_make_ipv4(out, h, port, allow_zero);
}
int qa_net_make_ipx(qa_net_address *out, int64_t network, const int64_t node[6], int64_t port) {
  bool bad = network < 0 || network > 0xffffffffLL;
  for (int i = 0; i < 6; i++) if (node[i] < 0 || node[i] > 255) bad = true;
  if (bad) qa_range_error("Invalid IPX address");
  memset(out, 0, sizeof *out); out->kind = QA_NET_IPX; out->network = (uint32_t)network;
  for (int i = 0; i < 6; i++) out->node[i] = (uint8_t)node[i];
  out->port = qa_net_port_number(port, false); return 1;
}
int qa_net_make_loopback(qa_net_address *out, const char *id) {
  memset(out, 0, sizeof *out); out->kind = QA_NET_LOOPBACK;
  out->host = dupstr(id); return out->host != NULL;
}
int qa_net_address_clone(const qa_net_address *in, qa_net_address *out) {
  *out = *in; out->host = NULL;
  if (in->host) { out->host = dupstr(in->host); if (!out->host) return 0; }
  return 1;
}
void qa_net_address_destroy(qa_net_address *a) { free(a->host); a->host = NULL; }
char *qa_net_address_host(const qa_net_address *a) {
  if (a->kind == QA_NET_IPV4) {
    char buf[32]; snprintf(buf, sizeof buf, "%u.%u.%u.%u", a->ipv4[0], a->ipv4[1], a->ipv4[2], a->ipv4[3]);
    return dupstr(buf);
  }
  return a->host ? dupstr(a->host) : dupstr("");
}
char *qa_net_address_key(const qa_net_address *a, bool include_port) {
  char buf[320];
  switch (a->kind) {
    case QA_NET_LOOPBACK: snprintf(buf, sizeof buf, "loopback:%s", a->host ? a->host : ""); break;
    case QA_NET_IPV4:
      if (include_port) snprintf(buf, sizeof buf, "%u.%u.%u.%u:%lld", a->ipv4[0], a->ipv4[1], a->ipv4[2], a->ipv4[3], (long long)a->port);
      else snprintf(buf, sizeof buf, "%u.%u.%u.%u", a->ipv4[0], a->ipv4[1], a->ipv4[2], a->ipv4[3]);
      break;
    case QA_NET_IPV6:
      if (include_port) snprintf(buf, sizeof buf, "[%s]:%lld", a->host ? a->host : "", (long long)a->port);
      else snprintf(buf, sizeof buf, "[%s]", a->host ? a->host : "");
      break;
    case QA_NET_IPX:
      if (include_port) snprintf(buf, sizeof buf, "ipx:%08x:%02x%02x%02x%02x%02x%02x:%lld", a->network, a->node[0], a->node[1], a->node[2], a->node[3], a->node[4], a->node[5], (long long)a->port);
      else snprintf(buf, sizeof buf, "ipx:%08x:%02x%02x%02x%02x%02x%02x", a->network, a->node[0], a->node[1], a->node[2], a->node[3], a->node[4], a->node[5]);
      break;
  }
  return dupstr(buf);
}
bool qa_net_same_address(const qa_net_address *a, const qa_net_address *b, bool include_port) {
  if (a->kind != b->kind) return false;
  char *ka = qa_net_address_key(a, include_port), *kb = qa_net_address_key(b, include_port);
  bool eq = ka && kb && strcmp(ka, kb) == 0; free(ka); free(kb); return eq;
}
int qa_net_parse_address(qa_net_address *out, const char *kind, const char *host_text,
                         const double *numbers, size_t count, int64_t network, bool has_network,
                         double port, bool has_port) {
  if (!kind) qa_range_error("Invalid network address record");
  if (strcmp(kind, "loopback") == 0 && host_text && host_text[0])
    return qa_net_make_loopback(out, host_text);
  if (!has_port) qa_range_error("Network address has no port");
  int64_t p = (int64_t)port;
  if (strcmp(kind, "ipv6") == 0 && host_text) {
    int ok = qa_net_make_ip(out, host_text, p, false);
    if (ok && out->kind != QA_NET_IPV6) qa_range_error("IPv6 record contains another address family");
    return ok;
  }
  if (strcmp(kind, "ipv4") == 0 && numbers && count == 4) {
    int64_t h[4]; for (int i = 0; i < 4; i++) h[i] = (int64_t)numbers[i];
    return qa_net_make_ipv4(out, h, p, false);
  }
  if (strcmp(kind, "ipx") == 0 && numbers && count == 6 && has_network) {
    int64_t n[6]; for (int i = 0; i < 6; i++) n[i] = (int64_t)numbers[i];
    return qa_net_make_ipx(out, network, n, p);
  }
  qa_range_error("Invalid network address fields");
}
static int all_digits(const char *s) { if (!*s) return 0; for (; *s; s++) if (*s < '0' || *s > '9') return 0; return 1; }
int qa_net_resolve_address(qa_net_address *out, const char *text, int64_t default_port, int family) {
  char hostbuf[512]; const char *host = text; int64_t port = default_port;
  if (text[0] == '[') {
    const char *end = strchr(text, ']');
    if (!end) qa_range_error("Unclosed IPv6 address");
    size_t n = (size_t)(end - text - 1); if (n >= sizeof hostbuf) n = sizeof hostbuf - 1;
    memcpy(hostbuf, text + 1, n); hostbuf[n] = '\0'; host = hostbuf;
    const char *suffix = end + 1;
    if (suffix[0]) {
      if (suffix[0] != ':' || !all_digits(suffix + 1)) qa_range_error("Invalid address port");
      port = strtoll(suffix + 1, NULL, 10);
    }
  } else {
    const char *first = strchr(text, ':'), *last = strrchr(text, ':');
    if (first && first == last) {
      size_t n = (size_t)(last - text); if (n >= sizeof hostbuf) n = sizeof hostbuf - 1;
      memcpy(hostbuf, text, n); hostbuf[n] = '\0'; host = hostbuf;
      if (!all_digits(last + 1)) qa_range_error("Invalid address port");
      port = strtoll(last + 1, NULL, 10);
    }
  }
  (void)qa_net_port_number(port, false);
  struct in_addr a4; struct in6_addr a6;
  char lit[512]; const char *pct = strchr(host, '%');
  size_t ln = pct ? (size_t)(pct - host) : strlen(host); if (ln >= sizeof lit) ln = sizeof lit - 1;
  memcpy(lit, host, ln); lit[ln] = '\0';
  if (inet_pton(AF_INET, lit, &a4) == 1 || inet_pton(AF_INET6, lit, &a6) == 1) {
    int ok = qa_net_make_ip(out, host, port, false);
    if (!ok) return 0;
    if (family != 0 && (out->kind == QA_NET_IPV4 ? 4 : 6) != family)
      qa_range_error("Address family does not match selection");
    return 1;
  }
  struct addrinfo hints; memset(&hints, 0, sizeof hints);
  hints.ai_family = family == 4 ? AF_INET : family == 6 ? AF_INET6 : AF_UNSPEC;
  hints.ai_socktype = SOCK_DGRAM;
  struct addrinfo *res = NULL;
  if (getaddrinfo(host, NULL, &hints, &res) != 0 || !res) qa_range_error("Expected an IP literal: %s", host);
  char num[INET6_ADDRSTRLEN] = {0};
  if (res->ai_family == AF_INET) inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, num, sizeof num);
  else inet_ntop(AF_INET6, &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr, num, sizeof num);
  freeaddrinfo(res);
  return qa_net_make_ip(out, num, port, false);
}

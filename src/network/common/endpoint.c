/* Port of src/network/common/endpoint.ts. GPL-2.0-or-later. */
#define _POSIX_C_SOURCE 200809L
#include "qa/network/common/endpoint.h"
#include "qa/foundation/support.h"
#include "qa/persistence/value.h"
#include <arpa/inet.h>
#include <math.h>
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
/* Maps a JS number onto the int64 range checks: non-integers become -1, which every
 * caller's range check rejects with its own TS message. */
static int64_t js_integer(double value) {
  if (!isfinite(value) || value != trunc(value)) return -1;
  if (value > 4294967296.0 * 2) return (int64_t)4294967296LL * 2;
  if (value < -1) return -1;
  return (int64_t)value;
}
int qa_net_make_ipv4(qa_net_address *out, const int64_t host[4], int64_t port, bool allow_zero) {
  for (int i = 0; i < 4; i++) if (host[i] < 0 || host[i] > 255) qa_range_error("Invalid IPv4 octet");
  memset(out, 0, sizeof *out); out->kind = QA_NET_IPV4;
  for (int i = 0; i < 4; i++) out->ipv4[i] = (uint8_t)host[i];
  out->port = qa_net_port_number(port, allow_zero); return 1;
}
/* Node isIP: dotted quads without leading zeros (inet_pton's AF_INET grammar), or an
 * IPv6 literal with an optional %scope of [0-9a-zA-Z-.:]+. Fills the parsed bytes. */
static int ip_family(const char *host, uint8_t v4[4], uint8_t v6[16]) {
  if (inet_pton(AF_INET, host, v4) == 1) return 4;
  const char *pct = strchr(host, '%');
  size_t ln = pct ? (size_t)(pct - host) : strlen(host);
  char lit[INET6_ADDRSTRLEN + 1];
  if (ln >= sizeof lit) return 0;
  memcpy(lit, host, ln); lit[ln] = '\0';
  if (inet_pton(AF_INET6, lit, v6) != 1) return 0;
  if (pct) {
    const char *scope = pct + 1;
    if (!*scope) return 0;
    for (; *scope; scope++) {
      unsigned char c = (unsigned char)*scope;
      if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-' || c == '.' || c == ':')) return 0;
    }
  }
  return 6;
}
/* WHATWG URL host serializer: lowercase hex pieces, the first longest run of two or more
 * zero pieces compressed, embedded IPv4 written as hex (new URL canonicalization). */
static void ipv6_serialize(const uint8_t b[16], char out[INET6_ADDRSTRLEN]) {
  unsigned piece[8];
  for (int i = 0; i < 8; i++) piece[i] = (unsigned)b[i * 2] << 8 | b[i * 2 + 1];
  int best = -1, best_len = 1;
  for (int i = 0; i < 8;) {
    if (piece[i] != 0) { i++; continue; }
    int j = i; while (j < 8 && piece[j] == 0) j++;
    if (j - i > best_len) { best = i; best_len = j - i; }
    i = j;
  }
  size_t n = 0; bool ignore0 = false;
  for (int i = 0; i < 8; i++) {
    if (ignore0 && piece[i] == 0) continue;
    ignore0 = false;
    if (i == best) { n += (size_t)snprintf(out + n, INET6_ADDRSTRLEN - n, i == 0 ? "::" : ":"); ignore0 = true; continue; }
    n += (size_t)snprintf(out + n, INET6_ADDRSTRLEN - n, "%x%s", piece[i], i != 7 ? ":" : "");
  }
  out[n] = '\0';
}
int qa_net_make_ip(qa_net_address *out, const char *host, int64_t port, bool allow_zero) {
  uint8_t v4[4], v6[16];
  int family = ip_family(host, v4, v6);
  if (family == 6) {
    const char *pct = strchr(host, '%');
    char norm[INET6_ADDRSTRLEN]; ipv6_serialize(v6, norm);
    memset(out, 0, sizeof *out); out->kind = QA_NET_IPV6;
    int64_t checked = qa_net_port_number(port, allow_zero);
    if (pct) { size_t need = strlen(norm) + strlen(pct) + 1; out->host = malloc(need); if (!out->host) return 0; snprintf(out->host, need, "%s%s", norm, pct); }
    else { out->host = dupstr(norm); if (!out->host) return 0; }
    out->port = checked; return 1;
  }
  if (family != 4) qa_range_error("Expected an IP literal: %s", host);
  int64_t h[4] = { v4[0], v4[1], v4[2], v4[3] };
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
  /* Number.isInteger: a fractional or non-finite port or octet is invalid, never truncated. */
  int64_t p = js_integer(port);
  if (strcmp(kind, "ipv6") == 0 && host_text) {
    int ok = qa_net_make_ip(out, host_text, p, false);
    if (ok && out->kind != QA_NET_IPV6) qa_range_error("IPv6 record contains another address family");
    return ok;
  }
  if (strcmp(kind, "ipv4") == 0 && numbers && count == 4) {
    int64_t h[4]; for (int i = 0; i < 4; i++) h[i] = js_integer(numbers[i]);
    return qa_net_make_ipv4(out, h, p, false);
  }
  if (strcmp(kind, "ipx") == 0 && numbers && count == 6 && has_network) {
    int64_t n[6]; for (int i = 0; i < 6; i++) n[i] = js_integer(numbers[i]);
    return qa_net_make_ipx(out, network, n, p);
  }
  qa_range_error("Invalid network address fields");
}
void qa_net_parse_address_value(const qa_save_value *value, qa_net_address *out) {
  memset(out, 0, sizeof *out);
  if (value == NULL || value->kind != QA_SAVE_OBJECT || qa_save_value_get(value, "kind") == NULL)
    qa_range_error("Invalid network address record");
  const qa_save_value *kind = qa_save_value_get(value, "kind");
  const char *kind_text = kind->kind == QA_SAVE_STRING ? kind->text : "";
  const qa_save_value *id = qa_save_value_get(value, "id");
  if (strcmp(kind_text, "loopback") == 0 && id && id->kind == QA_SAVE_STRING && id->text[0] != '\0') {
    if (!qa_net_make_loopback(out, id->text)) qa_range_error("Out of memory");
    return;
  }
  const qa_save_value *port = qa_save_value_get(value, "port");
  if (port == NULL || port->kind != QA_SAVE_NUMBER) qa_range_error("Network address has no port");
  const qa_save_value *host = qa_save_value_get(value, "host");
  if (strcmp(kind_text, "ipv6") == 0 && host && host->kind == QA_SAVE_STRING) {
    if (!qa_net_parse_address(out, "ipv6", host->text, NULL, 0, 0, false, port->number, true)) qa_range_error("Out of memory");
    return;
  }
  if (strcmp(kind_text, "ipv4") == 0 && host && host->kind == QA_SAVE_ARRAY && host->item_count == 4) {
    double numbers[4]; bool numeric = true;
    for (size_t i = 0; i < 4; i++) { if (host->items[i]->kind != QA_SAVE_NUMBER) numeric = false; else numbers[i] = host->items[i]->number; }
    if (numeric) {
      int64_t h[4]; for (int i = 0; i < 4; i++) h[i] = js_integer(numbers[i]);
      (void)qa_net_make_ipv4(out, h, js_integer(port->number), false);
      return;
    }
  }
  const qa_save_value *node = qa_save_value_get(value, "node"), *network = qa_save_value_get(value, "network");
  if (strcmp(kind_text, "ipx") == 0 && node && node->kind == QA_SAVE_ARRAY && network && network->kind == QA_SAVE_NUMBER
      && node->item_count == 6) {
    double numbers[6]; bool numeric = true;
    for (size_t i = 0; i < 6; i++) { if (node->items[i]->kind != QA_SAVE_NUMBER) numeric = false; else numbers[i] = node->items[i]->number; }
    if (numeric) {
      int64_t n[6]; for (int i = 0; i < 6; i++) n[i] = js_integer(numbers[i]);
      (void)qa_net_make_ipx(out, js_integer(network->number), n, js_integer(port->number));
      return;
    }
  }
  qa_range_error("Invalid network address fields");
}
static int all_digits(const char *s) { if (!*s) return 0; for (; *s; s++) if (*s < '0' || *s > '9') return 0; return 1; }
/* Number(digits): any value past 65535 only has to fail portNumber. */
static int64_t digits_port(const char *s) {
  int64_t v = 0;
  for (; *s; s++) { v = v * 10 + (*s - '0'); if (v > 65536) return 65536; }
  return v;
}
int qa_net_resolve_address(qa_net_address *out, const char *text, int64_t default_port, int family) {
  size_t text_length = strlen(text);
  char *host = malloc(text_length + 1);
  if (!host) return 0;
  memcpy(host, text, text_length + 1);
  int64_t port = default_port;
  qa_cleanup guard; qa_cleanup_push(&guard, free, host);
  if (text[0] == '[') {
    const char *end = strchr(text, ']');
    if (!end) qa_range_error("Unclosed IPv6 address");
    size_t n = (size_t)(end - text - 1);
    memcpy(host, text + 1, n); host[n] = '\0';
    const char *suffix = end + 1;
    if (suffix[0]) {
      if (suffix[0] != ':' || !all_digits(suffix + 1)) qa_range_error("Invalid address port");
      port = digits_port(suffix + 1);
    }
  } else {
    const char *first = strchr(text, ':'), *last = strrchr(text, ':');
    if (first && first == last) {
      size_t n = (size_t)(last - text);
      host[n] = '\0';
      if (!all_digits(last + 1)) qa_range_error("Invalid address port");
      port = digits_port(last + 1);
    }
  }
  (void)qa_net_port_number(port, false);
  uint8_t v4[4], v6[16];
  int ok;
  if (ip_family(host, v4, v6) != 0) {
    ok = qa_net_make_ip(out, host, port, false);
    if (ok && family != 0 && (out->kind == QA_NET_IPV4 ? 4 : 6) != family) {
      qa_net_address_destroy(out);
      qa_range_error("Address family does not match selection");
    }
  } else {
    /* dns.lookup: synchronous getaddrinfo (PORTING async rule); a failed lookup is the
     * Node DNS Error, not a RangeError. */
    struct addrinfo hints; memset(&hints, 0, sizeof hints);
    hints.ai_family = family == 4 ? AF_INET : family == 6 ? AF_INET6 : AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo *res = NULL;
    int status = getaddrinfo(host, NULL, &hints, &res);
    if (status != 0 || !res) {
      if (res) freeaddrinfo(res);
      qa_raise("Error", 0, 0, "getaddrinfo %s %s", status == EAI_AGAIN ? "EAI_AGAIN" : "ENOTFOUND", host);
    }
    char num[INET6_ADDRSTRLEN] = {0};
    if (res->ai_family == AF_INET) inet_ntop(AF_INET, &((struct sockaddr_in *)res->ai_addr)->sin_addr, num, sizeof num);
    else inet_ntop(AF_INET6, &((struct sockaddr_in6 *)res->ai_addr)->sin6_addr, num, sizeof num);
    freeaddrinfo(res);
    ok = qa_net_make_ip(out, num, port, false);
  }
  qa_cleanup_pop(&guard); free(host);
  return ok;
}

/* TS isPrivateHost. */
static bool ends_with(const char *text, const char *suffix) {
  size_t length = strlen(text), suffix_length = strlen(suffix);
  return length >= suffix_length && strcmp(text + length - suffix_length, suffix) == 0;
}
/* /^(\d+)\.(\d+)\.\d+\.\d+$/ with Number() of the first two groups. */
static bool dotted_quad(const char *host, double *a, double *b) {
  double parts[2] = {0, 0};
  const char *p = host;
  for (int group = 0; group < 4; group++) {
    if (*p < '0' || *p > '9') return false;
    double value = 0;
    while (*p >= '0' && *p <= '9') { value = value * 10 + (*p - '0'); p++; }
    if (group < 2) parts[group] = value;
    if (group < 3) { if (*p != '.') return false; p++; }
  }
  if (*p != '\0') return false;
  *a = parts[0]; *b = parts[1];
  return true;
}
bool qa_net_is_private_host(const char *hostname) {
  /* hostname.replace(/^\[|\]$/g, "").toLowerCase() (ASCII; hostnames here are ASCII) */
  size_t length = strlen(hostname);
  const char *start = hostname;
  if (length > 0 && start[0] == '[') { start++; length--; }
  if (length > 0 && start[length - 1] == ']') length--;
  char *host = malloc(length + 1);
  if (host == NULL) qa_range_error("Out of memory");
  for (size_t i = 0; i < length; i++) host[i] = (char)(start[i] >= 'A' && start[i] <= 'Z' ? start[i] - 'A' + 'a' : start[i]);
  host[length] = '\0';
  bool result = false;
  double a = 0, b = 0;
  if (strcmp(host, "localhost") == 0 || ends_with(host, ".localhost") || ends_with(host, ".local") || ends_with(host, ".internal")) result = true;
  else if (dotted_quad(host, &a, &b))
    result = a == 0 || a == 10 || a == 127 || (a == 169 && b == 254) || (a == 172 && b >= 16 && b <= 31) || (a == 192 && b == 168) ||
        (a == 100 && b >= 64 && b <= 127);
  else if (strchr(host, ':') != NULL)
    result = strcmp(host, "::") == 0 || strcmp(host, "::1") == 0 || (host[0] == 'f' && (host[1] == 'c' || host[1] == 'd')) ||
        (host[0] == 'f' && host[1] == 'e' && (host[2] == '8' || host[2] == '9' || host[2] == 'a' || host[2] == 'b')) ||
        strncmp(host, "::ffff:", 7) == 0;
  free(host);
  return result;
}
bool qa_net_is_private_network_address(const qa_net_address *address) {
  if (address->kind == QA_NET_LOOPBACK || address->kind == QA_NET_IPX) return true;
  if (address->kind == QA_NET_IPV4) {
    char host[32];
    snprintf(host, sizeof host, "%u.%u.%u.%u", address->ipv4[0], address->ipv4[1], address->ipv4[2], address->ipv4[3]);
    return qa_net_is_private_host(host);
  }
  return address->kind == QA_NET_IPV6 && address->host != NULL && qa_net_is_private_host(address->host);
}

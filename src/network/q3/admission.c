/* Port of src/network/q3/admission.ts. GPL-2.0-or-later. */
#include "qa/network/q3/admission.h"
#include "qa/core/commands/text.h"
#include "qa/core/common_error.h"
#include "qa/core/cvars/info.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js_case.h"
static char *xstrdup(const char *s) { size_t n = strlen(s); char *r = malloc(n + 1); if (r) memcpy(r, s, n + 1); return r; }
bool qa_q3_mirror_address_copy(qa_q3_mirror_address *dst, const qa_q3_mirror_address *src) {
  *dst = *src;
  dst->loopback_id = NULL; dst->ipv6_host = NULL;
  if (src->kind == QA_Q3_ADDR_LOOPBACK && src->loopback_id) {
    dst->loopback_id = xstrdup(src->loopback_id);
    if (!dst->loopback_id) return false;
  }
  if (src->kind == QA_Q3_ADDR_IPV6 && src->ipv6_host) {
    dst->ipv6_host = xstrdup(src->ipv6_host);
    if (!dst->ipv6_host) return false;
  }
  return true;
}
void qa_q3_mirror_address_destroy(qa_q3_mirror_address *a) { free(a->loopback_id); free(a->ipv6_host); a->loopback_id = NULL; a->ipv6_host = NULL; }
/* Borrowed-string view of a mirror address as the landed owner
 * type; read-only use (key/same) only, never destroyed. */
static qa_net_address net_view(const qa_q3_mirror_address *a) {
  qa_net_address out;
  memset(&out, 0, sizeof out);
  switch (a->kind) {
    case QA_Q3_ADDR_IPV4: out.kind = QA_NET_IPV4; memcpy(out.ipv4, a->host, 4); break;
    case QA_Q3_ADDR_IPV6: out.kind = QA_NET_IPV6; out.host = a->ipv6_host; break;
    case QA_Q3_ADDR_IPX:
      out.kind = QA_NET_IPX; out.network = (uint32_t)a->network; memcpy(out.node, a->node, 6); break;
    case QA_Q3_ADDR_LOOPBACK: out.kind = QA_NET_LOOPBACK; out.host = a->loopback_id; break;
  }
  out.port = a->port;
  return out;
}
bool qa_q3_address_to_net(const qa_q3_address *in, qa_net_address *out) {
  *out = net_view(in);
  const char *text = in->kind == QA_Q3_ADDR_IPV6 ? in->ipv6_host
                     : in->kind == QA_Q3_ADDR_LOOPBACK ? in->loopback_id : NULL;
  out->host = NULL;
  if (text != NULL) {
    out->host = xstrdup(text);
    if (out->host == NULL) return false;
  }
  return true;
}
bool qa_q3_address_from_net(const qa_net_address *in, qa_q3_address *out) {
  memset(out, 0, sizeof *out);
  switch (in->kind) {
    case QA_NET_IPV4: out->kind = QA_Q3_ADDR_IPV4; memcpy(out->host, in->ipv4, 4); break;
    case QA_NET_IPV6:
      out->kind = QA_Q3_ADDR_IPV6;
      if (in->host != NULL) { out->ipv6_host = xstrdup(in->host); if (out->ipv6_host == NULL) return false; }
      break;
    case QA_NET_IPX:
      out->kind = QA_Q3_ADDR_IPX; out->network = (int64_t)in->network; memcpy(out->node, in->node, 6); break;
    case QA_NET_LOOPBACK:
      out->kind = QA_Q3_ADDR_LOOPBACK;
      if (in->host != NULL) { out->loopback_id = xstrdup(in->host); if (out->loopback_id == NULL) return false; }
      break;
  }
  out->port = in->port;
  return true;
}
char *qa_q3_mirror_address_key(const qa_q3_mirror_address *a, bool include_port) {
  qa_net_address view = net_view(a);
  return qa_net_address_key(&view, include_port);
}
bool qa_q3_mirror_same_address(const qa_q3_mirror_address *a, const qa_q3_mirror_address *b, bool include_port) {
  qa_net_address va = net_view(a), vb = net_view(b);
  return qa_net_same_address(&va, &vb, include_port);
}
static void address_text(const qa_q3_address *a, char *buf, size_t n) {
  switch (a->kind) {
    case QA_Q3_ADDR_LOOPBACK: snprintf(buf, n, "loopback"); break;
    case QA_Q3_ADDR_IPV6: snprintf(buf, n, "[%s]:%lld", a->ipv6_host ? a->ipv6_host : "", (long long)a->port); break;
    case QA_Q3_ADDR_IPV4: snprintf(buf, n, "%u.%u.%u.%u:%lld", a->host[0], a->host[1], a->host[2], a->host[3], (long long)a->port); break;
    case QA_Q3_ADDR_IPX: {
      char node[13]; for (int i = 0; i < 6; i++) snprintf(node + i * 2, 3, "%02x", a->node[i]);
      snprintf(buf, n, "%08llx.%s:%lld", (unsigned long long)(uint32_t)a->network, node, (long long)a->port);
      break;
    }
  }
}
bool qa_q3_is_lan_address(const qa_q3_address *a) {
  if (a->kind != QA_Q3_ADDR_IPV4) return true;
  return a->host[0] == 127 || a->host[0] == 10
    || (a->host[0] == 192 && a->host[1] == 168)
    || (a->host[0] == 172 && a->host[1] >= 16 && a->host[1] <= 31);
}
static bool ci_eq_n(const char *a, size_t an, const char *b) {
  size_t bn = strlen(b);
  if (an != bn) return false;
  /* TS toLowerCase() === over Latin-1 connectionless text. */
  for (size_t i = 0; i < an; i++) if (qa_js_latin1_lower((unsigned char)a[i]) != qa_js_latin1_lower((unsigned char)b[i])) return false;
  return true;
}
char *qa_q3_info_value(const char *info, const char *key) {
  if (strlen(info) >= 8192) qa_common_error(QA_COMMON_ERROR_DROP, "Info_ValueForKey: oversize infostring");
  /* fields = split('\\') of sourceCommandText(info); C text already NUL-cut bytes. */
  size_t n = strlen(info);
  /* iterate fields by index parity like TS */
  size_t start_index = (n > 0 && info[0] == '\\') ? 1 : 0;
  size_t field = 0; size_t pos = 0;
  const char *key_start = NULL; size_t key_len = 0;
  for (;;) {
    size_t end = pos;
    while (end < n && info[end] != '\\') end++;
    if (field >= start_index && ((field - start_index) % 2) == 0) { key_start = info + pos; key_len = end - pos; }
    else if (field >= start_index && key_start) {
      if (ci_eq_n(key_start, key_len, key)) {
        char *r = malloc((end - pos) + 1);
        if (!r) return NULL;
        memcpy(r, info + pos, end - pos); r[end - pos] = 0; return r;
      }
      key_start = NULL;
    }
    if (end >= n) break;
    pos = end + 1; field++;
  }
  return xstrdup("");
}
typedef struct { qa_q3_message_print_fn print; void *ctx; } print_adapter_t;
static void print_adapter(void *ctx, const char *text) { print_adapter_t *p = ctx; if (p->print) p->print(p->ctx, text); }
static char *info_set(const char *info, const char *key, const char *value, qa_q3_message_print_fn print, void *print_ctx) {
  print_adapter_t pa = { print, print_ctx };
  qa_info_options o = { QA_COMMAND_DIALECT_Q3, 1024, QA_CVAR_INFO_CLIENT_USERINFO, false, print_adapter, &pa };
  return qa_set_info_value(info, key, value, &o);
}
/* TS exported addressText and infoSet. */
void qa_q3_address_text(const qa_q3_address *address, char *buf, size_t n) { address_text(address, buf, n); }
char *qa_q3_info_set(const char *info, const char *key, const char *value, qa_q3_message_print_fn print, void *print_ctx) {
  return info_set(info, key, value, print, print_ctx);
}
/* TS q3PeerUserinfo: SV_UserinfoChanged keeps the transport address in the userinfo, because the
 * banning code and rate rules read it. */
char *qa_q3_peer_userinfo(const char *info, const qa_q3_address *address, qa_q3_message_print_fn print, void *print_ctx) {
  char text[128];
  if (address->kind == QA_Q3_ADDR_LOOPBACK) snprintf(text, sizeof text, "localhost");
  else address_text(address, text, sizeof text);
  return info_set(info, "ip", text, print, print_ctx);
}
void qa_q3_outgoing_datagram_destroy(qa_q3_outgoing_datagram *d) {
  qa_q3_mirror_address_destroy(&d->to); free(d->payload); d->payload = NULL;
}
void qa_q3_client_admission_result_destroy(qa_q3_client_admission_result *r) {
  qa_q3_mirror_address_destroy(&r->address);
  qa_q3_connectionless_packet_destroy(&r->packet);
}
struct qa_q3_client_admission {
  qa_q3_client_admission_phase phase;
  bool has_address; qa_q3_address address;
  int64_t challenge; double connect_time; int32_t connect_packet_count; double last_packet_time;
  int64_t qport; qa_q3_message_print_fn print; void *print_ctx;
};
bool qa_q3_client_admission_init(qa_q3_client_admission **out, int64_t qport, qa_q3_message_print_fn print, void *print_ctx) {
  if (qport < 0 || qport > 65535) qa_range_error("Q3 qport must be uint16");
  qa_q3_client_admission *a = calloc(1, sizeof *a);
  if (!a) return false;
  a->phase = QA_Q3_PHASE_DISCONNECTED; a->connect_time = -99999; a->qport = qport;
  a->print = print; a->print_ctx = print_ctx;
  *out = a; return true;
}
void qa_q3_client_admission_destroy(qa_q3_client_admission *a) { if (!a) return; qa_q3_mirror_address_destroy(&a->address); free(a); }
qa_q3_client_admission_phase qa_q3_client_admission_get_phase(const qa_q3_client_admission *a) { return a->phase; }
void qa_q3_client_admission_set_phase(qa_q3_client_admission *a, qa_q3_client_admission_phase p) { a->phase = p; }
bool qa_q3_client_admission_get_address(const qa_q3_client_admission *a, qa_q3_address *out) {
  if (!a->has_address) return false;
  return qa_q3_mirror_address_copy(out, &a->address);
}
int64_t qa_q3_client_admission_challenge(const qa_q3_client_admission *a) { return a->challenge; }
void qa_q3_client_admission_set_challenge(qa_q3_client_admission *a, int64_t c) { a->challenge = c; }
int64_t qa_q3_client_admission_connect_time(const qa_q3_client_admission *a) { return (int64_t)a->connect_time; }
void qa_q3_client_admission_set_connect_time(qa_q3_client_admission *a, int64_t t) { a->connect_time = (double)t; }
int64_t qa_q3_client_admission_connect_packet_count(const qa_q3_client_admission *a) { return a->connect_packet_count; }
void qa_q3_client_admission_set_connect_packet_count(qa_q3_client_admission *a, int64_t n) { a->connect_packet_count = (int32_t)n; }
int64_t qa_q3_client_admission_last_packet_time(const qa_q3_client_admission *a) { return (int64_t)a->last_packet_time; }
void qa_q3_client_admission_set_last_packet_time(qa_q3_client_admission *a, int64_t t) { a->last_packet_time = (double)t; }
int64_t qa_q3_client_admission_get_qport(const qa_q3_client_admission *a) { return a->qport; }
bool qa_q3_client_admission_begin(qa_q3_client_admission *a, const qa_q3_address *address) {
  qa_q3_mirror_address_destroy(&a->address);
  if (!qa_q3_mirror_address_copy(&a->address, address)) return false;
  a->has_address = true;
  a->phase = address->kind == QA_Q3_ADDR_LOOPBACK ? QA_Q3_PHASE_CHALLENGING : QA_Q3_PHASE_CONNECTING;
  a->connect_time = -99999; a->connect_packet_count = 0;
  return true;
}
bool qa_q3_client_admission_resend(qa_q3_client_admission *a, double now, const char *userinfo, qa_q3_outgoing_datagram *out) {
  memset(out, 0, sizeof *out);
  if ((a->phase != QA_Q3_PHASE_CONNECTING && a->phase != QA_Q3_PHASE_CHALLENGING) || now - a->connect_time < 3000) return false;
  if (!a->has_address) qa_range_error("Connection resend has no resolved address");
  a->connect_time = now;
  a->connect_packet_count = (int32_t)((uint32_t)a->connect_packet_count + 1u);
  if (!qa_q3_mirror_address_copy(&out->to, &a->address)) return false;
  if (a->phase == QA_Q3_PHASE_CONNECTING)
    return qa_q3_encode_connectionless_text("getchallenge", &out->payload, &out->payload_len);
  char *info = qa_source_command_text(userinfo);
  if (!info) return false;
  if (strlen(info) > 1023) info[1023] = 0;
  char num[32];
  char *next = info_set(info, "protocol", "68", a->print, a->print_ctx); free(info); info = next;
  if (!info) return false;
  snprintf(num, sizeof num, "%lld", (long long)a->qport);
  next = info_set(info, "qport", num, a->print, a->print_ctx); free(info); info = next;
  if (!info) return false;
  snprintf(num, sizeof num, "%lld", (long long)a->challenge);
  next = info_set(info, "challenge", num, a->print, a->print_ctx); free(info); info = next;
  if (!info) return false;
  bool ok = qa_q3_encode_connect(info, &out->payload, &out->payload_len);
  free(info);
  return ok;
}
static bool ci_eq(const char *a, const char *b) {
  return qa_js_latin1_equals_ignore_case(a, b); /* TS toLowerCase() === over Latin-1 text */
}
bool qa_q3_client_admission_receive(qa_q3_client_admission *a, const qa_q3_address *from, const uint8_t *bytes, size_t len, double now, qa_q3_client_admission_result *out) {
  memset(out, 0, sizeof *out);
  a->last_packet_time = now;
  if (len >= 4 && bytes[0] == 255 && bytes[1] == 255 && bytes[2] == 255 && bytes[3] == 255) {
    if (!qa_q3_decode_connectionless(bytes, len, QA_Q3_CONNLESS_CLIENT, NULL, &out->packet)) return false;
    if (ci_eq(out->packet.command, "challengeResponse")) {
      /* The original lets a proxy on the same host answer from another port, but never another host. */
      if (a->phase == QA_Q3_PHASE_CONNECTING && a->has_address && qa_q3_mirror_same_address(from, &a->address, false)) {
        a->challenge = qa_native_atoi(out->packet.argument_count ? out->packet.arguments[0] : "");
        a->phase = QA_Q3_PHASE_CHALLENGING; a->connect_packet_count = 0; a->connect_time = -99999;
        qa_q3_mirror_address_destroy(&a->address);
        if (!qa_q3_mirror_address_copy(&a->address, from)) return false;
        a->has_address = true;
      }
      qa_q3_connectionless_packet_destroy(&out->packet);
      out->kind = QA_Q3_ADMIT_HANDLED; return true;
    }
    if (ci_eq(out->packet.command, "connectResponse")) {
      qa_q3_connectionless_packet_destroy(&out->packet);
      if (a->phase != QA_Q3_PHASE_CHALLENGING || !a->has_address || !qa_q3_mirror_same_address(from, &a->address, false)) {
        out->kind = QA_Q3_ADMIT_IGNORED; return true;
      }
      a->phase = QA_Q3_PHASE_CONNECTED;
      qa_q3_mirror_address_destroy(&a->address);
      if (!qa_q3_mirror_address_copy(&a->address, from)) return false;
      out->kind = QA_Q3_ADMIT_ADMITTED;
      if (!qa_q3_mirror_address_copy(&out->address, from)) return false;
      out->challenge = a->challenge; out->qport = a->qport;
      return true;
    }
    out->kind = QA_Q3_ADMIT_CONNECTIONLESS; return true;
  }
  if (a->phase != QA_Q3_PHASE_CONNECTED || !a->has_address || !qa_q3_mirror_same_address(from, &a->address, true)) {
    out->kind = QA_Q3_ADMIT_IGNORED; return true;
  }
  out->kind = QA_Q3_ADMIT_SEQUENCED; out->bytes = bytes; out->byte_len = len;
  return true;
}
void qa_q3_client_admission_disconnect(qa_q3_client_admission *a) {
  a->phase = QA_Q3_PHASE_DISCONNECTED;
  qa_q3_mirror_address_destroy(&a->address); a->has_address = false;
}
struct qa_q3_server_admission {
  qa_q3_server_admission_bindings bindings;
  qa_q3_challenge challenges[1024];
};
bool qa_q3_server_admission_init(qa_q3_server_admission **out, const qa_q3_server_admission_bindings *bindings) {
  qa_q3_server_admission *a = calloc(1, sizeof *a);
  if (!a) return false;
  a->bindings = *bindings;
  *out = a; return true;
}
void qa_q3_server_admission_destroy(qa_q3_server_admission *a) {
  if (!a) return;
  for (int i = 0; i < 1024; i++) qa_q3_mirror_address_destroy(&a->challenges[i].address);
  free(a);
}
qa_q3_challenge *qa_q3_server_admission_challenges(qa_q3_server_admission *a, size_t *count) { *count = 1024; return a->challenges; }
const qa_q3_server_admission_bindings *qa_q3_server_admission_get_bindings(const qa_q3_server_admission *a) { return &a->bindings; }
static void sadm_reply(qa_q3_server_admission *a, const qa_q3_address *to, const char *text) {
  uint8_t *packet = NULL; size_t len = 0;
  if (!qa_q3_encode_connectionless_text(text, &packet, &len)) return;
  qa_q3_address copy; memset(&copy, 0, sizeof copy);
  if (qa_q3_mirror_address_copy(&copy, to)) a->bindings.send(a->bindings.ctx, &copy, packet, len);
  qa_q3_mirror_address_destroy(&copy);
  free(packet);
}
static void sadm_challenge(qa_q3_server_admission *a, const qa_q3_address *from, double now) {
  qa_q3_server_admission_bindings *b = &a->bindings;
  if (!b->enabled(b->ctx)) return;
  double oldest_time = 0x7fffffff;
  qa_q3_challenge *oldest = &a->challenges[0], *found = NULL;
  for (int i = 0; i < 1024; i++) {
    qa_q3_challenge *cand = &a->challenges[i];
    if (!cand->connected && cand->has_address && qa_q3_mirror_same_address(from, &cand->address, true)) { found = cand; break; }
    if (cand->time < oldest_time) { oldest_time = cand->time; oldest = cand; }
  }
  if (!found) {
    found = oldest;
    double r1 = b->random(b->ctx), r2 = b->random(b->ctx);
    /* TS (random()<<16)^random()^now with JS ToInt32 of doubles. */
    found->challenge = (int32_t)(((uint32_t)qa_int32(r1) << 16) ^ (uint32_t)qa_int32(r2) ^ (uint32_t)qa_int32(now));
    qa_q3_mirror_address_destroy(&found->address);
    qa_q3_mirror_address_copy(&found->address, from); found->has_address = true;
    found->first_time = now; found->time = now; found->connected = false;
  }
  if (b->is_lan(b->ctx, from) || qa_int32(now - found->first_time) > 5000) {
    found->ping_time = now;
    char buf[64]; snprintf(buf, sizeof buf, "challengeResponse %lld", (long long)found->challenge);
    sadm_reply(a, from, buf);
    return;
  }
  b->authorize(b->ctx, found);
}
static void sadm_authorize(qa_q3_server_admission *a, const qa_q3_address *from, qa_q3_connectionless_packet *packet, double now) {
  qa_q3_server_admission_bindings *b = &a->bindings;
  qa_q3_address authority; memset(&authority, 0, sizeof authority);
  bool has_auth = b->authorize_address(b->ctx, &authority);
  bool ok = from->kind == QA_Q3_ADDR_IPV4 && has_auth && authority.kind == QA_Q3_ADDR_IPV4
    && qa_q3_mirror_same_address(from, &authority, false);
  qa_q3_mirror_address_destroy(&authority);
  if (!ok) return;
  int64_t number = qa_native_atoi(packet->argument_count > 0 ? packet->arguments[0] : "");
  qa_q3_challenge *challenge = NULL;
  for (int i = 0; i < 1024; i++) if (a->challenges[i].challenge == number) { challenge = &a->challenges[i]; break; }
  if (!challenge || !challenge->has_address) return;
  challenge->ping_time = now;
  const char *result = packet->argument_count > 1 ? packet->arguments[1] : "";
  const char *reason = packet->argument_count > 2 ? packet->arguments[2] : "";
  if (ci_eq(result, "accept") || (ci_eq(result, "demo") && b->demo_restricted(b->ctx))) {
    char buf[64]; snprintf(buf, sizeof buf, "challengeResponse %lld", (long long)challenge->challenge);
    sadm_reply(a, &challenge->address, buf);
  } else {
    if (ci_eq(result, "demo")) sadm_reply(a, &challenge->address, "print\nServer is not a demo server\n");
    else {
      size_t n = strlen(reason) + 16; char *buf = malloc(n);
      if (buf) { snprintf(buf, n, "print\n%s\n", reason); sadm_reply(a, &challenge->address, buf); free(buf); }
    }
    qa_q3_mirror_address_destroy(&challenge->address); challenge->has_address = false;
    challenge->challenge = 0; challenge->time = 0; challenge->first_time = 0; challenge->ping_time = 0; challenge->connected = false;
  }
}
static bool slot_matches(const qa_q3_address *from, const qa_q3_admission_slot *slot, int64_t qport) {
  if (!slot->has_address) return false;
  if (!qa_q3_mirror_same_address(from, &slot->address, false)) return false;
  if (slot->qport == qport) return true;
  return from->kind != QA_Q3_ADDR_LOOPBACK && slot->address.kind != QA_Q3_ADDR_LOOPBACK && from->port == slot->address.port;
}
static bool sadm_connect(qa_q3_server_admission *a, const qa_q3_address *from, const char *input, double now) {
  qa_q3_server_admission_bindings *host = &a->bindings;
  char *userinfo = qa_source_command_text(input);
  if (!userinfo) return false;
  if (strlen(userinfo) > 1023) userinfo[1023] = 0;
  char *proto = qa_q3_info_value(userinfo, "protocol");
  int is68 = proto && qa_native_atoi(proto) == 68; free(proto);
  if (!is68) { sadm_reply(a, from, "print\nServer uses protocol version 68.\n"); free(userinfo); return true; }
  char *qs = qa_q3_info_value(userinfo, "qport"); int64_t qport = qa_native_atoi(qs ? qs : ""); free(qs);
  char *cs = qa_q3_info_value(userinfo, "challenge"); int64_t challenge_number = qa_native_atoi(cs ? cs : ""); free(cs);
  if (qport < 0 || qport > 65535) { sadm_reply(a, from, "print\nInvalid qport.\n"); free(userinfo); return true; }
  size_t slot_count = 0;
  const qa_q3_admission_slot *slots = host->slots(host->ctx, &slot_count);
  const qa_q3_admission_slot *existing = NULL;
  for (size_t i = 0; i < slot_count; i++)
    if (slots[i].phase != QA_Q3_SLOT_FREE && slot_matches(from, &slots[i], qport)) { existing = &slots[i]; break; }
  /* ((now - lastConnectTime) | 0) < Math.imul(reconnectLimitSeconds, 1000) */
  if (existing && qa_int32(now - existing->last_connect_time)
        < (int32_t)((uint32_t)qa_int32(host->reconnect_limit_seconds(host->ctx)) * 1000u)) { free(userinfo); return true; }
  if (from->kind != QA_Q3_ADDR_LOOPBACK) {
    qa_q3_challenge *challenge = NULL;
    for (int i = 0; i < 1024; i++) {
      qa_q3_challenge *c = &a->challenges[i];
      if (c->has_address && qa_q3_mirror_same_address(from, &c->address, true) && c->challenge == challenge_number) { challenge = c; break; }
    }
    if (!challenge) { sadm_reply(a, from, "print\nNo or bad challenge for address.\n"); free(userinfo); return true; }
    char text[128]; address_text(from, text, sizeof text);
    char *next = info_set(userinfo, "ip", text, host->print, host->print_ctx); free(userinfo); userinfo = next;
    if (!userinfo) return false;
    int32_t ping = qa_int32(now - challenge->ping_time);
    challenge->connected = true;
    if (!host->is_lan(host->ctx, from)) {
      float minp = (float)host->minimum_ping(host->ctx);
      if (minp != 0 && (float)ping < minp) {
        sadm_reply(a, from, "print\nServer is for high pings only\n");
        challenge->address.port = 0; /* TS {...from, port:0}; kinds here are ipv4/ipx */
        free(userinfo); return true;
      }
      float maxp = (float)host->maximum_ping(host->ctx);
      if (maxp != 0 && (float)ping > maxp) { sadm_reply(a, from, "print\nServer is for low pings only\n"); free(userinfo); return true; }
    }
  } else {
    char *next = info_set(userinfo, "ip", "localhost", host->print, host->print_ctx); free(userinfo); userinfo = next;
    if (!userinfo) return false;
  }
  char *pw = qa_q3_info_value(userinfo, "password");
  int64_t start = (pw && strcmp(pw, host->private_password(host->ctx)) == 0) ? 0 : host->private_clients(host->ctx);
  free(pw);
  const qa_q3_admission_slot *selected = existing;
  if (!selected) for (size_t i = 0; i < slot_count; i++)
    if (slots[i].slot >= start && slots[i].phase == QA_Q3_SLOT_FREE) { selected = &slots[i]; break; }
  if (!selected) {
    if (from->kind != QA_Q3_ADDR_LOOPBACK) { sadm_reply(a, from, "print\nServer is full.\n"); free(userinfo); return true; }
    bool any_non_bot = false; const qa_q3_admission_slot *last = NULL;
    for (size_t i = 0; i < slot_count; i++) if (slots[i].slot >= start) { if (!slots[i].bot) any_non_bot = true; last = &slots[i]; }
    if (any_non_bot || !last) { free(userinfo); qa_common_error(QA_COMMON_ERROR_FATAL, "server is full on local connect\n"); }
    selected = last;
    host->drop_bot(host->ctx, selected->slot);
    if (!host->enabled(host->ctx)) { free(userinfo); return true; }
  }
  qa_q3_accepted_connect accepted; memset(&accepted, 0, sizeof accepted);
  accepted.slot = selected->slot; accepted.qport = qport; accepted.challenge = challenge_number;
  if (!qa_q3_mirror_address_copy(&accepted.address, from)) { free(userinfo); return false; }
  accepted.userinfo = userinfo;
  char *rejected = host->admit(host->ctx, &accepted);
  qa_q3_mirror_address_destroy(&accepted.address);
  free(userinfo);
  if (!host->enabled(host->ctx)) { free(rejected); return true; }
  if (rejected) {
    size_t n = strlen(rejected) + 16; char *buf = malloc(n);
    if (buf) { snprintf(buf, n, "print\n%s\n", rejected); sadm_reply(a, from, buf); free(buf); }
    free(rejected);
    return true;
  }
  sadm_reply(a, from, "connectResponse");
  return true;
}
bool qa_q3_server_admission_receive(qa_q3_server_admission *a, const qa_q3_address *from, const uint8_t *bytes, size_t len, double now) {
  qa_q3_connectionless_packet packet;
  if (!qa_q3_decode_connectionless(bytes, len, QA_Q3_CONNLESS_SERVER, NULL, &packet)) return false;
  bool ok = true;
  if (ci_eq(packet.command, "getchallenge")) sadm_challenge(a, from, now);
  else if (ci_eq(packet.command, "ipAuthorize")) sadm_authorize(a, from, &packet, now);
  else if (ci_eq(packet.command, "connect")) ok = sadm_connect(a, from, packet.argument_count ? packet.arguments[0] : "", now);
  else a->bindings.query(a->bindings.ctx, from, &packet);
  qa_q3_connectionless_packet_destroy(&packet);
  return ok;
}
void qa_q3_server_admission_disconnect(qa_q3_server_admission *a, const qa_q3_address *address) {
  for (int i = 0; i < 1024; i++) {
    qa_q3_challenge *c = &a->challenges[i];
    if (c->has_address && qa_q3_mirror_same_address(address, &c->address, true)) { c->connected = false; break; }
  }
}
int64_t qa_q3_route_sequenced_packet(const qa_q3_address *from, const uint8_t *bytes, size_t len,
                                     const qa_q3_admission_slot *slots, size_t slot_count) {
  if (len < 6) return -1;
  int64_t qport = bytes[4] | (bytes[5] << 8);
  for (size_t i = 0; i < slot_count; i++) {
    const qa_q3_admission_slot *slot = &slots[i];
    if (slot->phase != QA_Q3_SLOT_FREE && slot->has_address
        && qa_q3_mirror_same_address(from, &slot->address, false) && slot->qport == qport)
      return slot->slot;
  }
  return -1;
}

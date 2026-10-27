/* Port of src/network/services/discovery.ts. GPL-2.0-or-later. */
#include "qa/network/services/discovery.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static char *dupstr(const char *s) { if (!s) return NULL; size_t n = strlen(s) + 1; char *p = malloc(n); memcpy(p, s, n); return p; }
void qa_server_status_destroy(qa_server_status *s) {
  free(s->name); free(s->map);
  qa_qw_info_destroy(&s->rules);
  for (size_t i = 0; i < s->player_count; i++) free(s->player_details[i].name);
  free(s->player_details);
  memset(s, 0, sizeof *s);
}
void qa_server_status_copy(const qa_server_status *src, qa_server_status *dst) {
  memset(dst, 0, sizeof *dst);
  dst->name = dupstr(src->name); dst->map = dupstr(src->map);
  dst->players = src->players; dst->max_players = src->max_players;
  qa_qw_info_init(&dst->rules);
  for (size_t i = 0; i < src->rules.count; i++)
    qa_qw_info_set(&dst->rules, src->rules.entries[i].key, src->rules.entries[i].value);
  dst->player_count = src->player_count;
  if (src->player_count) {
    dst->player_details = malloc(src->player_count * sizeof *dst->player_details);
    for (size_t i = 0; i < src->player_count; i++) {
      dst->player_details[i].name = dupstr(src->player_details[i].name);
      dst->player_details[i].score = src->player_details[i].score;
      dst->player_details[i].ping = src->player_details[i].ping;
    }
  }
  dst->wire = src->wire;
}
static void entry_destroy(qa_browser_entry *e) {
  qa_net_address_destroy(&e->address);
  free(e->sources);
  if (e->has_status) qa_server_status_destroy(&e->status);
}
struct pending_query {
  long long handle; char *challenge; qa_net_address address;
  const char *request_kind; double sent_at; bool retain_result;
  int timeout_mode; /* 0 undefined, 1 null, 2 value */ double timeout_ms;
};
struct stored_result {
  long long handle; const char *kind; qa_net_address address; const char *request_kind;
  double sent_at; bool has_status; qa_server_status status; double ping_ms, completed_at;
};
struct broadcast_rec { char *challenge; double sent_at; };
struct qa_server_browser {
  qa_discovery_wire wire; qa_packet_sender transport;
  qa_browser_entry *entries; char **keys; size_t count, capacity;
  struct pending_query *queries; size_t qcount, qcap;
  struct stored_result *results; size_t rcount, rcap;
  struct broadcast_rec *broadcasts; size_t bcount, bcap;
  long long query_sequence, next_handle;
};
void qa_server_browser_init(qa_server_browser *b, qa_discovery_wire wire, qa_packet_sender transport) {
  memset(b, 0, sizeof *b); b->wire = wire; b->transport = transport; b->next_handle = 1;
}
void qa_server_browser_destroy(qa_server_browser *b) {
  for (size_t i = 0; i < b->count; i++) { entry_destroy(&b->entries[i]); free(b->keys[i]); }
  for (size_t i = 0; i < b->qcount; i++) { free(b->queries[i].challenge); qa_net_address_destroy(&b->queries[i].address); }
  for (size_t i = 0; i < b->rcount; i++) {
    qa_net_address_destroy(&b->results[i].address);
    if (b->results[i].has_status) qa_server_status_destroy(&b->results[i].status);
  }
  for (size_t i = 0; i < b->bcount; i++) free(b->broadcasts[i].challenge);
  free(b->entries); free(b->keys); free(b->queries); free(b->results); free(b->broadcasts);
  memset(b, 0, sizeof *b);
}
static long long entry_index(qa_server_browser *b, const char *key) {
  for (size_t i = 0; i < b->count; i++) if (strcmp(b->keys[i], key) == 0) return (long long)i;
  return -1;
}
static bool has_source(const qa_browser_entry *e, const char *source) {
  for (size_t i = 0; i < e->source_count; i++) if (strcmp(e->sources[i], source) == 0) return true;
  return false;
}
const qa_browser_entry *qa_server_browser_add(qa_server_browser *b, const qa_net_address *address, const char *source, double now) {
  char *key = qa_net_address_key(address, true);
  long long at = entry_index(b, key);
  if (at >= 0) {
    free(key);
    qa_browser_entry *e = &b->entries[at];
    if (!has_source(e, source)) {
      e->sources = realloc(e->sources, (e->source_count + 1) * sizeof *e->sources);
      e->sources[e->source_count++] = source;
    }
    return e;
  }
  if (b->count == b->capacity) {
    size_t nc = b->capacity ? b->capacity * 2 : 8;
    b->entries = realloc(b->entries, nc * sizeof *b->entries);
    b->keys = realloc(b->keys, nc * sizeof *b->keys);
    b->capacity = nc;
  }
  qa_browser_entry *e = &b->entries[b->count];
  memset(e, 0, sizeof *e);
  (void)qa_net_address_clone(address, &e->address);
  e->sources = malloc(sizeof *e->sources); e->sources[0] = source; e->source_count = 1;
  e->updated_at = now;
  b->keys[b->count] = key;
  b->count++;
  return e;
}
void qa_server_browser_remove_source(qa_server_browser *b, const qa_net_address *address, const char *source) {
  char *key = qa_net_address_key(address, true);
  long long at = entry_index(b, key);
  free(key);
  if (at < 0) return;
  qa_browser_entry *e = &b->entries[at];
  size_t w = 0;
  for (size_t i = 0; i < e->source_count; i++) if (strcmp(e->sources[i], source) != 0) e->sources[w++] = e->sources[i];
  e->source_count = w;
  if (w == 0) {
    entry_destroy(e); free(b->keys[at]);
    memmove(&b->entries[at], &b->entries[at+1], (b->count - (size_t)at - 1) * sizeof *b->entries);
    memmove(&b->keys[at], &b->keys[at+1], (b->count - (size_t)at - 1) * sizeof *b->keys);
    b->count--;
  }
}
void qa_server_browser_remove_favorite(qa_server_browser *b, const qa_net_address *address) {
  qa_server_browser_remove_source(b, address, "favorite");
}
void qa_server_browser_restore_entry(qa_server_browser *b, const qa_browser_entry *entry) {
  char *key = qa_net_address_key(&entry->address, true);
  long long at = entry_index(b, key);
  if (at < 0) {
    if (b->count == b->capacity) {
      size_t nc = b->capacity ? b->capacity * 2 : 8;
      b->entries = realloc(b->entries, nc * sizeof *b->entries);
      b->keys = realloc(b->keys, nc * sizeof *b->keys);
      b->capacity = nc;
    }
    qa_browser_entry *e = &b->entries[b->count];
    memset(e, 0, sizeof *e);
    (void)qa_net_address_clone(&entry->address, &e->address);
    e->source_count = entry->source_count;
    e->sources = malloc((e->source_count ? e->source_count : 1) * sizeof *e->sources);
    memcpy(e->sources, entry->sources, e->source_count * sizeof *e->sources);
    e->has_status = entry->has_status;
    if (entry->has_status) qa_server_status_copy(&entry->status, &e->status);
    e->has_ping = entry->has_ping; e->ping_ms = entry->ping_ms; e->updated_at = entry->updated_at;
    b->keys[b->count++] = key;
    return;
  }
  free(key);
  qa_browser_entry *e = &b->entries[at];
  if (!e->has_status && entry->has_status) {
    qa_server_status_copy(&entry->status, &e->status); e->has_status = true;
    e->has_ping = entry->has_ping; e->ping_ms = entry->ping_ms; e->updated_at = entry->updated_at;
  }
  for (size_t i = 0; i < entry->source_count; i++) if (!has_source(e, entry->sources[i])) {
    e->sources = realloc(e->sources, (e->source_count + 1) * sizeof *e->sources);
    e->sources[e->source_count++] = entry->sources[i];
  }
}
const qa_browser_entry *qa_server_browser_get_entry(qa_server_browser *b, const qa_net_address *address) {
  char *key = qa_net_address_key(address, true);
  long long at = entry_index(b, key);
  free(key);
  return at < 0 ? NULL : &b->entries[at];
}
size_t qa_server_browser_size(qa_server_browser *b) { return b->count; }
const qa_browser_entry *qa_server_browser_entry_at(qa_server_browser *b, size_t index) { return &b->entries[index]; }
size_t qa_server_browser_pending_requests(qa_server_browser *b) { return b->qcount; }
static void store_result(qa_server_browser *b, long long handle, const char *kind,
                         const qa_net_address *address, const char *request_kind, double sent_at,
                         const qa_server_status *status, double ping_ms, double completed_at) {
  for (size_t i = 0; i < b->rcount; i++) if (b->results[i].handle == handle) {
    qa_net_address_destroy(&b->results[i].address);
    if (b->results[i].has_status) qa_server_status_destroy(&b->results[i].status);
    memmove(&b->results[i], &b->results[i+1], (b->rcount - i - 1) * sizeof *b->results);
    b->rcount--; break;
  }
  if (b->rcount == b->rcap) { size_t nc = b->rcap ? b->rcap * 2 : 8; b->results = realloc(b->results, nc * sizeof *b->results); b->rcap = nc; }
  struct stored_result *r = &b->results[b->rcount++];
  memset(r, 0, sizeof *r);
  r->handle = handle; r->kind = kind; r->request_kind = request_kind; r->sent_at = sent_at;
  (void)qa_net_address_clone(address, &r->address);
  if (status) { r->has_status = true; qa_server_status_copy(status, &r->status); r->ping_ms = ping_ms; r->completed_at = completed_at; }
}
static void remove_query(qa_server_browser *b, size_t index) {
  free(b->queries[index].challenge);
  qa_net_address_destroy(&b->queries[index].address);
  memmove(&b->queries[index], &b->queries[index+1], (b->qcount - index - 1) * sizeof *b->queries);
  b->qcount--;
}
void qa_server_browser_release_request(qa_server_browser *b, long long handle) {
  for (size_t i = 0; i < b->qcount; i++) if (b->queries[i].handle == handle) { remove_query(b, i); break; }
  for (size_t i = 0; i < b->rcount; i++) if (b->results[i].handle == handle) {
    qa_net_address_destroy(&b->results[i].address);
    if (b->results[i].has_status) qa_server_status_destroy(&b->results[i].status);
    memmove(&b->results[i], &b->results[i+1], (b->rcount - i - 1) * sizeof *b->results);
    b->rcount--; break;
  }
}
static void finish_request(qa_server_browser *b, size_t index, const char *kind,
                           const qa_server_status *status, double ping_ms, double completed_at) {
  struct pending_query p = b->queries[index];
  /* copy fields before removal frees address clone ownership moves */
  qa_net_address address; (void)qa_net_address_clone(&p.address, &address);
  const char *rk = p.request_kind; double sent = p.sent_at; long long handle = p.handle; bool retain = p.retain_result;
  remove_query(b, index);
  if (retain) store_result(b, handle, kind, &address, rk, sent, status, ping_ms, completed_at);
  qa_net_address_destroy(&address);
}
static long long start_request(qa_server_browser *b, const qa_net_address *address, double now,
                               const char *kind, bool retain, int timeout_mode, double timeout_ms) {
  long long handle = b->next_handle++;
  char challenge[32]; snprintf(challenge, sizeof challenge, "%lld", ++b->query_sequence);
  if (b->qcount == b->qcap) { size_t nc = b->qcap ? b->qcap * 2 : 8; b->queries = realloc(b->queries, nc * sizeof *b->queries); b->qcap = nc; }
  struct pending_query *p = &b->queries[b->qcount++];
  memset(p, 0, sizeof *p);
  p->handle = handle; p->challenge = dupstr(challenge);
  (void)qa_net_address_clone(address, &p->address);
  p->request_kind = kind; p->sent_at = now; p->retain_result = retain;
  p->timeout_mode = timeout_mode; p->timeout_ms = timeout_ms;
  if (retain) store_result(b, handle, "pending", address, kind, now, NULL, 0, 0);
  size_t blen = 0;
  uint8_t *bytes = b->wire.query(b->wire.ctx, kind, challenge, &blen);
  bool sent = b->transport.send(b->transport.ctx, address, bytes, blen);
  free(bytes);
  if (sent) return handle;
  qa_server_browser_release_request(b, handle);
  return 0;
}
bool qa_server_browser_query(qa_server_browser *b, const qa_net_address *address, double now, const char *kind) {
  const char *k = kind ? kind : "info";
  char *akey = qa_net_address_key(address, true);
  for (size_t i = 0; i < b->qcount;) {
    struct pending_query *p = &b->queries[i];
    if (!p->retain_result && strcmp(p->request_kind, k) == 0) {
      char *pk = qa_net_address_key(&p->address, true);
      bool same = strcmp(pk, akey) == 0; free(pk);
      if (same) { qa_server_browser_release_request(b, p->handle); continue; }
    }
    i++;
  }
  free(akey);
  return start_request(b, address, now, k, false, 0, 0) != 0;
}
long long qa_server_browser_request(qa_server_browser *b, const qa_net_address *address, double now,
                                    const char *kind, bool has_timeout, bool timeout_is_null, double timeout_ms) {
  const char *k = kind ? kind : "info";
  int timeout_mode = !has_timeout ? 0 : timeout_is_null ? 1 : 2;
  if (timeout_mode == 2 && (!isfinite(timeout_ms) || timeout_ms < 0))
    qa_range_error("Invalid discovery request timeout");
  char *akey = qa_net_address_key(address, true);
  for (size_t i = 0; i < b->qcount; i++) {
    struct pending_query *p = &b->queries[i];
    if (p->retain_result || strcmp(p->request_kind, k) != 0) continue;
    char *pk = qa_net_address_key(&p->address, true);
    bool same = strcmp(pk, akey) == 0; free(pk);
    if (same) {
      p->retain_result = true; p->timeout_mode = timeout_mode; p->timeout_ms = timeout_ms;
      store_result(b, p->handle, "pending", &p->address, p->request_kind, p->sent_at, NULL, 0, 0);
      long long handle = p->handle;
      free(akey);
      return handle;
    }
  }
  free(akey);
  long long handle = start_request(b, address, now, k, true, timeout_mode, timeout_ms);
  return handle ? handle : 0;
}
int qa_server_browser_request_result(qa_server_browser *b, long long handle, qa_discovery_result *out) {
  for (size_t i = 0; i < b->rcount; i++) if (b->results[i].handle == handle) {
    memset(out, 0, sizeof *out);
    out->kind = b->results[i].kind; out->request_kind = b->results[i].request_kind;
    out->sent_at = b->results[i].sent_at;
    (void)qa_net_address_clone(&b->results[i].address, &out->address);
    out->has_status = b->results[i].has_status;
    if (out->has_status) qa_server_status_copy(&b->results[i].status, &out->status);
    out->ping_ms = b->results[i].ping_ms; out->completed_at = b->results[i].completed_at;
    return 1;
  }
  return 0;
}
bool qa_server_browser_cancel_request(qa_server_browser *b, long long handle) {
  for (size_t i = 0; i < b->qcount; i++) if (b->queries[i].handle == handle) {
    finish_request(b, i, "cancelled", NULL, 0, 0);
    return true;
  }
  return false;
}
bool qa_server_browser_receive(qa_server_browser *b, const qa_net_address *address, const qa_server_status *status,
                               const char *challenge, double now, const char *kind) {
  char *key = qa_net_address_key(address, true);
  long long match_index = -1; size_t matches = 0;
  for (size_t i = 0; i < b->qcount; i++) {
    struct pending_query *p = &b->queries[i];
    char *pk = qa_net_address_key(&p->address, true);
    bool same = strcmp(pk, key) == 0; free(pk);
    if (same && (!kind || strcmp(p->request_kind, kind) == 0) && (!challenge || strcmp(p->challenge, challenge) == 0)) {
      matches++; match_index = (long long)i;
    }
  }
  if (matches > 1) { free(key); return false; }
  bool have_direct = match_index >= 0;
  const struct broadcast_rec *broadcast = NULL;
  if (!(kind && strcmp(kind, "status") == 0)) {
    if (!challenge) { if (b->bcount) broadcast = &b->broadcasts[b->bcount - 1]; }
    else for (size_t i = 0; i < b->bcount; i++) if (strcmp(b->broadcasts[i].challenge, challenge) == 0) broadcast = &b->broadcasts[i];
  }
  if (!have_direct && !broadcast) { free(key); return false; }
  double sent_at = have_direct ? b->queries[match_index].sent_at : broadcast->sent_at;
  double ping = fmax(0, now - sent_at);
  const qa_browser_entry *old;
  if (!have_direct) old = qa_server_browser_add(b, address, "lan", now);
  else {
    old = qa_server_browser_get_entry(b, address);
    if (!old) old = qa_server_browser_add(b, address, "direct", now);
  }
  long long at = entry_index(b, key);
  qa_browser_entry *e = &b->entries[at];
  if (e->has_status) qa_server_status_destroy(&e->status);
  qa_server_status_copy(status, &e->status); e->has_status = true;
  e->has_ping = true; e->ping_ms = ping; e->updated_at = now;
  free(key);
  /* match_index may have shifted after add realloc of entries only; queries unaffected. */
  if (have_direct) finish_request(b, (size_t)match_index, "completed", status, ping, now);
  return true;
}
bool qa_server_browser_query_master(qa_server_browser *b, const qa_net_address *address) {
  size_t n = 0; uint8_t *bytes = b->wire.master_query(b->wire.ctx, &n);
  bool sent = b->transport.send(b->transport.ctx, address, bytes, n);
  free(bytes); return sent;
}
void qa_server_browser_receive_master(qa_server_browser *b, const qa_net_address *addresses, size_t count, double now) {
  for (size_t i = 0; i < count; i++) (void)qa_server_browser_add(b, &addresses[i], "master", now);
}
double qa_server_browser_broadcast(qa_server_browser *b, const qa_net_address *addresses, size_t count, double now) {
  double sent = 0;
  char challenge[32]; snprintf(challenge, sizeof challenge, "%lld", ++b->query_sequence);
  if (b->bcount == b->bcap) { size_t nc = b->bcap ? b->bcap * 2 : 4; b->broadcasts = realloc(b->broadcasts, nc * sizeof *b->broadcasts); b->bcap = nc; }
  b->broadcasts[b->bcount].challenge = dupstr(challenge); b->broadcasts[b->bcount].sent_at = now; b->bcount++;
  for (size_t i = 0; i < count; i++) {
    size_t n = 0; uint8_t *bytes = b->wire.query(b->wire.ctx, "info", challenge, &n);
    if (b->transport.send(b->transport.ctx, &addresses[i], bytes, n)) sent++;
    free(bytes);
  }
  if (sent == 0) { free(b->broadcasts[b->bcount - 1].challenge); b->bcount--; }
  return sent;
}
qa_net_address *qa_server_browser_expire_queries(qa_server_browser *b, double now, double timeout_ms, size_t *out_count) {
  size_t w = 0;
  for (size_t i = 0; i < b->bcount; i++) {
    if (now - b->broadcasts[i].sent_at >= timeout_ms) free(b->broadcasts[i].challenge);
    else b->broadcasts[w++] = b->broadcasts[i];
  }
  b->bcount = w;
  size_t cap = 4, n = 0;
  qa_net_address *expired = calloc(cap, sizeof *expired);
  for (size_t i = 0; i < b->qcount;) {
    struct pending_query *p = &b->queries[i];
    if (p->timeout_mode == 1) { i++; continue; }
    double limit = p->timeout_mode == 2 ? p->timeout_ms : timeout_ms;
    if (now - p->sent_at < limit) { i++; continue; }
    char *qk = qa_net_address_key(&p->address, true);
    long long at = entry_index(b, qk);
    if (at >= 0) {
      bool seen = false;
      for (size_t j = 0; j < n; j++) {
        char *ek = qa_net_address_key(&expired[j], true);
        if (strcmp(ek, qk) == 0) seen = true;
        free(ek);
      }
      if (!seen) {
        if (n == cap) { cap *= 2; expired = realloc(expired, cap * sizeof *expired); }
        (void)qa_net_address_clone(&b->entries[at].address, &expired[n++]);
      }
    }
    free(qk);
    finish_request(b, i, "expired", NULL, 0, 0);
  }
  *out_count = n; return expired;
}
qa_net_address *qa_server_browser_favorite_addresses(qa_server_browser *b, size_t *out_count) {
  size_t n = 0;
  qa_net_address *out = calloc(b->count ? b->count : 1, sizeof *out);
  for (size_t i = 0; i < b->count; i++) if (has_source(&b->entries[i], "favorite"))
    (void)qa_net_address_clone(&b->entries[i].address, &out[n++]);
  *out_count = n; return out;
}
static void append_str(char **buf, size_t *len, size_t *cap, const char *text) {
  size_t n = strlen(text);
  if (*len + n + 1 > *cap) { size_t nc = (*cap ? *cap : 256); while (nc < *len + n + 1) nc *= 2; *buf = realloc(*buf, nc); *cap = nc; }
  memcpy(*buf + *len, text, n + 1); *len += n;
}
char *qa_server_browser_save_favorites(qa_server_browser *b) {
  size_t count = 0; qa_net_address *addrs = qa_server_browser_favorite_addresses(b, &count);
  char *buf = NULL; size_t len = 0, cap = 0;
  append_str(&buf, &len, &cap, "[");
  char tmp[1024];
  for (size_t i = 0; i < count; i++) {
    if (i) append_str(&buf, &len, &cap, ",");
    qa_net_address *a = &addrs[i];
    if (a->kind == QA_NET_LOOPBACK) snprintf(tmp, sizeof tmp, "{\"kind\":\"loopback\",\"id\":\"%s\"}", a->host ? a->host : "");
    else if (a->kind == QA_NET_IPV4) snprintf(tmp, sizeof tmp, "{\"kind\":\"ipv4\",\"host\":[%u,%u,%u,%u],\"port\":%lld}", a->ipv4[0], a->ipv4[1], a->ipv4[2], a->ipv4[3], (long long)a->port);
    else if (a->kind == QA_NET_IPV6) snprintf(tmp, sizeof tmp, "{\"kind\":\"ipv6\",\"host\":\"%s\",\"port\":%lld}", a->host ? a->host : "", (long long)a->port);
    else snprintf(tmp, sizeof tmp, "{\"kind\":\"ipx\",\"network\":%u,\"node\":[%u,%u,%u,%u,%u,%u],\"port\":%lld}", a->network, a->node[0], a->node[1], a->node[2], a->node[3], a->node[4], a->node[5], (long long)a->port);
    append_str(&buf, &len, &cap, tmp);
    qa_net_address_destroy(a);
  }
  free(addrs);
  append_str(&buf, &len, &cap, "]");
  return buf;
}
/* Minimal parser for the saved-favorites JSON array of address records. */
static const char *json_find(const char *obj, const char *key) {
  char pat[64]; snprintf(pat, sizeof pat, "\"%s\"", key);
  const char *p = strstr(obj, pat);
  if (!p) return NULL;
  p = strchr(p, ':');
  if (!p) return NULL;
  p++;
  while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') p++;
  return p;
}
static int json_string(const char *obj, const char *key, char *out, size_t outsz) {
  const char *v = json_find(obj, key);
  if (!v || *v != '"') return 0;
  v++;
  size_t n = 0;
  while (*v && *v != '"' && n + 1 < outsz) {
    if (*v == '\\' && v[1]) v++;
    out[n++] = *v++;
  }
  out[n] = '\0';
  return *v == '"';
}
static int json_numbers(const char *obj, const char *key, double *out, size_t max, size_t *count) {
  const char *v = json_find(obj, key);
  if (!v || *v != '[') return 0;
  v++;
  size_t n = 0;
  while (*v && *v != ']' && n < max) {
    while (*v == ' ' || *v == ',') v++;
    if (*v == ']') break;
    char *end = NULL;
    out[n++] = strtod(v, &end);
    if (end == v) return 0;
    v = end;
  }
  *count = n; return 1;
}
void qa_server_browser_restore_favorites(qa_server_browser *b, const char *text) {
  const char *p = text;
  while (*p == ' ' || *p == '\n' || *p == '\t' || *p == '\r') p++;
  if (*p != '[') qa_range_error("Invalid favorites save");
  /* Collect object substrings first (parse before mutating, like TS map). */
  size_t cap = 4, count = 0;
  qa_net_address *addrs = calloc(cap, sizeof *addrs);
  p++;
  for (;;) {
    while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\t' || *p == '\r') p++;
    if (*p == ']') break;
    if (*p != '{') qa_range_error("Invalid favorites save");
    const char *start = p;
    int depth = 0; bool in_str = false, esc = false;
    for (; *p; p++) {
      if (esc) { esc = false; continue; }
      if (*p == '\\' && in_str) { esc = true; continue; }
      if (*p == '"') in_str = !in_str;
      if (in_str) continue;
      if (*p == '{') depth++;
      if (*p == '}' && --depth == 0) { p++; break; }
    }
    if (depth != 0) qa_range_error("Invalid favorites save");
    char *obj = malloc((size_t)(p - start) + 1);
    memcpy(obj, start, (size_t)(p - start)); obj[p - start] = '\0';
    char kind[32] = {0}, host[512] = {0};
    double numbers[8] = {0}; size_t ncount = 0;
    const char *portv = json_find(obj, "port");
    const char *netv = json_find(obj, "network");
    double port = portv ? strtod(portv, NULL) : NAN;
    (void)json_string(obj, "kind", kind, sizeof kind);
    (void)json_string(obj, "host", host, sizeof host);
    if (strcmp(kind, "ipv4") == 0) (void)json_numbers(obj, "host", numbers, 4, &ncount);
    if (strcmp(kind, "ipx") == 0) (void)json_numbers(obj, "node", numbers, 6, &ncount);
    if (strcmp(kind, "loopback") == 0) (void)json_string(obj, "id", host, sizeof host);
    if (count == cap) { cap *= 2; addrs = realloc(addrs, cap * sizeof *addrs); }
    qa_net_address parsed;
    if (strcmp(kind, "loopback") == 0)
      (void)qa_net_parse_address(&parsed, "loopback", host, NULL, 0, 0, false, 0, false);
    else if (strcmp(kind, "ipv6") == 0)
      (void)qa_net_parse_address(&parsed, "ipv6", host, NULL, 0, 0, false, port, portv != NULL);
    else if (strcmp(kind, "ipv4") == 0)
      (void)qa_net_parse_address(&parsed, "ipv4", NULL, numbers, ncount, 0, false, port, portv != NULL);
    else if (strcmp(kind, "ipx") == 0)
      (void)qa_net_parse_address(&parsed, "ipx", NULL, numbers, ncount, netv ? (int64_t)strtod(netv, NULL) : 0, netv != NULL, port, portv != NULL);
    else qa_range_error("Invalid network address fields");
    addrs[count++] = parsed;
    free(obj);
  }
  size_t fav_count = 0; qa_net_address *favs = qa_server_browser_favorite_addresses(b, &fav_count);
  for (size_t i = 0; i < fav_count; i++) { qa_server_browser_remove_favorite(b, &favs[i]); qa_net_address_destroy(&favs[i]); }
  free(favs);
  for (size_t i = 0; i < count; i++) { (void)qa_server_browser_add(b, &addrs[i], "favorite", 0); qa_net_address_destroy(&addrs[i]); }
  free(addrs);
}
void qa_master_heartbeat_service_init(qa_master_heartbeat_service *s, qa_discovery_wire wire, qa_packet_sender transport, double interval_ms) {
  memset(s, 0, sizeof *s);
  s->wire = wire; s->transport = transport; s->interval_ms = interval_ms; s->next_time = -1.0 / 0.0;
}
double qa_master_heartbeat_service_send(qa_master_heartbeat_service *s, const qa_net_address *masters, size_t count, double now, bool active, bool force) {
  if (!force && now < s->next_time) return 0;
  s->next_time = now + s->interval_ms;
  double sent = 0;
  for (size_t i = 0; i < count; i++) {
    size_t n = 0; uint8_t *bytes = s->wire.heartbeat(s->wire.ctx, active, &n);
    if (s->transport.send(s->transport.ctx, &masters[i], bytes, n)) sent++;
    free(bytes);
  }
  return sent;
}

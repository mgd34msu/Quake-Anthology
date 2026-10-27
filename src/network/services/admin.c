/* Port of src/network/services/admin.ts. GPL-2.0-or-later. */
#include "qa/network/services/admin.h"
#include "qa/foundation/support.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
static uint32_t le32(const uint8_t b[4]) { return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24); }
int qa_source_ipv4_filter(const char *text, qa_ipv4_filter *out) {
  uint8_t bytes[4] = {0,0,0,0}, mask[4] = {0,0,0,0};
  size_t cursor = 0, len = strlen(text);
  for (int index = 0; index < 4; index++) {
    size_t start = cursor;
    while (cursor < len && isdigit((unsigned char)text[cursor])) cursor++;
    if (start == cursor) return 0;
    long value = strtol(text + start, NULL, 10) & 255;
    bytes[index] = (uint8_t)value; if (value != 0) mask[index] = 255;
    if (cursor >= len) break;
    cursor++;
  }
  out->mask = le32(mask); out->compare = le32(bytes);
  return 1;
}
qa_ipv4_filter qa_cidr_ipv4_filter(const qa_net_address *address, double prefix_bits) {
  if (prefix_bits != trunc(prefix_bits) || prefix_bits < 0 || prefix_bits > 32) qa_range_error("Invalid IPv4 prefix");
  int bits = (int)prefix_bits;
  uint8_t mask_bytes[4];
  for (int i = 0; i < 4; i++)
    mask_bytes[i] = (uint8_t)(bits >= i * 8 + 8 ? 255 : bits <= i * 8 ? 0 : (255 << (8 - (bits - i * 8))) & 255);
  uint32_t mask = le32(mask_bytes);
  qa_ipv4_filter out; out.mask = mask;
  out.compare = le32(address->ipv4) & mask;
  return out;
}
void qa_ip_filter_list_init(qa_ip_filter_list *l, bool deny_matches) {
  memset(l, 0, sizeof *l); l->deny_matches = deny_matches;
}
void qa_ip_filter_list_destroy(qa_ip_filter_list *l) { free(l->entries); memset(l, 0, sizeof *l); }
void qa_ip_filter_list_add(qa_ip_filter_list *l, qa_ipv4_filter filter) {
  if (l->count == l->capacity) { size_t nc = l->capacity ? l->capacity * 2 : 8; l->entries = realloc(l->entries, nc * sizeof *l->entries); l->capacity = nc; }
  l->entries[l->count++] = filter;
}
bool qa_ip_filter_list_remove(qa_ip_filter_list *l, qa_ipv4_filter filter) {
  for (size_t i = 0; i < l->count; i++) if (l->entries[i].mask == filter.mask && l->entries[i].compare == filter.compare) {
    memmove(&l->entries[i], &l->entries[i+1], (l->count - i - 1) * sizeof *l->entries); l->count--; return true;
  }
  return false;
}
bool qa_ip_filter_list_rejects(const qa_ip_filter_list *l, const qa_net_address *address) {
  if (address->kind != QA_NET_IPV4) return false;
  uint32_t value = le32(address->ipv4);
  bool matches = false;
  for (size_t i = 0; i < l->count; i++) if ((value & l->entries[i].mask) == l->entries[i].compare) matches = true;
  return l->deny_matches ? matches : !matches;
}
qa_ipv4_filter *qa_ip_filter_list_snapshot(const qa_ip_filter_list *l, size_t *out_count) {
  *out_count = l->count;
  qa_ipv4_filter *copy = malloc((l->count ? l->count : 1) * sizeof *copy);
  memcpy(copy, l->entries, l->count * sizeof *copy);
  return copy;
}
void qa_flood_limiter_init(qa_flood_limiter *l, double burst, double interval_ms) {
  if (burst <= 0 || interval_ms <= 0) qa_range_error("Invalid flood limit");
  memset(l, 0, sizeof *l); l->burst = burst; l->interval_ms = interval_ms;
}
void qa_flood_limiter_destroy(qa_flood_limiter *l) {
  for (size_t i = 0; i < l->count; i++) free(l->entries[i].key);
  free(l->entries); memset(l, 0, sizeof *l);
}
bool qa_flood_limiter_allow(qa_flood_limiter *l, const qa_net_address *address, double now) {
  char *key = qa_net_address_key(address, false);
  qa_flood_entry *entry = NULL;
  for (size_t i = 0; i < l->count; i++) if (strcmp(l->entries[i].key, key) == 0) entry = &l->entries[i];
  double elapsed = entry ? now - entry->time : 0;
  if (elapsed < 0) elapsed = 0;
  double tokens = entry ? fmin(l->burst, entry->tokens + elapsed / l->interval_ms) : l->burst;
  bool allowed = tokens >= 1;
  if (!entry) {
    if (l->count == l->capacity) { size_t nc = l->capacity ? l->capacity * 2 : 8; l->entries = realloc(l->entries, nc * sizeof *l->entries); l->capacity = nc; }
    entry = &l->entries[l->count++]; entry->key = key;
  } else free(key);
  entry->tokens = allowed ? tokens - 1 : tokens; entry->time = now;
  return allowed;
}
void qa_flood_limiter_expire(qa_flood_limiter *l, double now) {
  size_t w = 0;
  for (size_t i = 0; i < l->count; i++) {
    if (now - l->entries[i].time > l->burst * l->interval_ms) free(l->entries[i].key);
    else l->entries[w++] = l->entries[i];
  }
  l->count = w;
}
void qa_source_chat_flood_init(qa_source_chat_flood *f, double messages, double seconds, double lock_seconds) {
  if (messages != trunc(messages) || messages < 0 || messages > 10 || seconds < 0 || lock_seconds < 0)
    qa_range_error("Invalid source chat flood policy");
  memset(f, 0, sizeof *f); f->messages = messages; f->seconds = seconds; f->lock_seconds = lock_seconds;
}
const char *qa_source_chat_flood_check(qa_source_chat_flood *f, double now, bool paused, double *seconds) {
  if (f->messages == 0) return "allowed";
  if (!paused && now < f->locked_until) { *seconds = trunc(f->locked_until - now); return "locked"; }
  int idx = (int)(((f->head - (int)f->messages + 11) % 10 + 10) % 10);
  double previous = f->times[idx];
  if (!paused && previous != 0 && now - previous < f->seconds) {
    f->locked_until = now + f->lock_seconds;
    *seconds = trunc(f->lock_seconds);
    return "flood";
  }
  f->head = (f->head + 1) % 10; f->times[f->head] = now;
  return "allowed";
}
char *qa_q3_rcon_command(const char *line) {
  size_t cursor = 4, len = strlen(line);
  while (cursor < len && line[cursor] == ' ') cursor++;
  while (cursor < len && line[cursor] != ' ') cursor++;
  while (cursor < len && line[cursor] == ' ') cursor++;
  size_t n = len - cursor; if (n > 1023) n = 1023;
  char *out = malloc(n + 1); memcpy(out, line + cursor, n); out[n] = '\0'; return out;
}
void qa_rcon_service_init(qa_rcon_service *s, qa_rcon_host host, bool is_q3, double output_bytes) {
  if (output_bytes != trunc(output_bytes) || output_bytes < 5 || output_bytes > 9007199254740991.0)
    qa_range_error("Rcon output buffer must hold a complete character and terminator");
  memset(s, 0, sizeof *s); s->host = host; s->is_q3 = is_q3; s->output_bytes = output_bytes; s->last_time = 0;
}
struct rcon_acc { qa_rcon_service *svc; const qa_net_address *from; char *buf; size_t len, cap; };
static void rcon_flush(struct rcon_acc *a) {
  if (a->len == 0) return;
  a->buf[a->len] = '\0';
  a->svc->host.reply(a->svc->host.ctx, a->from, a->buf);
  a->len = 0;
}
static void rcon_output(void *ctx, const char *text, size_t length) {
  struct rcon_acc *a = ctx;
  for (size_t i = 0; i < length; i++) {
    /* TS iterates code points and counts UTF-8 bytes for the unified
     * profile; C text is already bytes, so both profiles count bytes. */
    size_t size = a->len + 1;
    if ((double)size > a->svc->output_bytes - 1) rcon_flush(a);
    if (a->len + 2 > a->cap) { size_t nc = a->cap ? a->cap * 2 : 256; a->buf = realloc(a->buf, nc); a->cap = nc; }
    a->buf[a->len++] = text[i];
  }
}
const char *qa_rcon_service_handle(qa_rcon_service *s, const qa_net_address *from,
                                   const char *supplied_password, const char *command, double milliseconds) {
  double time = s->is_q3 ? (double)(uint32_t)milliseconds : milliseconds;
  double next = s->is_q3 ? (double)(uint32_t)(s->last_time + 500) : s->last_time + 500;
  if (time < next) return "throttled";
  s->last_time = time;
  const char *password = s->host.password(s->host.ctx);
  if (password[0] == '\0') { s->host.reply(s->host.ctx, from, "No rconpassword set on the server.\n"); return "disabled"; }
  if (strcmp(password, supplied_password) != 0) { s->host.reply(s->host.ctx, from, "Bad rconpassword.\n"); return "denied"; }
  struct rcon_acc acc; memset(&acc, 0, sizeof acc); acc.svc = s; acc.from = from;
  if (command[0]) {
    if (s->is_q3 && strlen(command) > 1023) {
      char *sliced = malloc(1024); memcpy(sliced, command, 1023); sliced[1023] = '\0';
      s->host.execute(s->host.ctx, sliced, rcon_output, &acc); free(sliced);
    } else s->host.execute(s->host.ctx, command, rcon_output, &acc);
  }
  rcon_flush(&acc);
  free(acc.buf);
  return "executed";
}

/* Port of src/network/services/admin.ts. GPL-2.0-or-later. */
#include "qa/network/services/admin.h"
#include "qa/foundation/support.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/js.h"
static uint32_t le32(const uint8_t b[4]) { return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24); }
int qa_source_ipv4_filter(const char *text, qa_ipv4_filter *out) {
  uint8_t bytes[4] = {0,0,0,0}, mask[4] = {0,0,0,0};
  size_t cursor = 0, len = strlen(text);
  for (int index = 0; index < 4; index++) {
    size_t start = cursor;
    while (cursor < len && isdigit((unsigned char)text[cursor])) cursor++;
    if (start == cursor) return 0;
    /* Number.parseInt(digits, 10) & 255; a 400-digit cut-off once read "000...5" as Infinity. */
    int32_t value = qa_js_to_int32(qa_js_parse_int(text + start, 10)) & 255;
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
  if (l->count > 0) memcpy(copy, l->entries, l->count * sizeof *copy);
  return copy;
}
void qa_flood_limiter_init_max(qa_flood_limiter *l, double burst, double interval_ms, double max_entries) {
  if (burst <= 0 || interval_ms <= 0 || max_entries < 1 || isnan(burst) || isnan(interval_ms) || isnan(max_entries))
    qa_range_error("Invalid flood limit");
  memset(l, 0, sizeof *l); l->burst = burst; l->interval_ms = interval_ms; l->max_entries = max_entries;
  if (!qa_peer_table_init(&l->peers, max_entries)) qa_range_error("Out of memory");
  l->tokens = calloc((size_t)max_entries, sizeof *l->tokens);
  l->times = calloc((size_t)max_entries, sizeof *l->times);
  if (!l->tokens || !l->times) qa_range_error("Out of memory");
}
void qa_flood_limiter_init(qa_flood_limiter *l, double burst, double interval_ms) {
  qa_flood_limiter_init_max(l, burst, interval_ms, 8192);
}
void qa_flood_limiter_destroy(qa_flood_limiter *l) {
  if (l->tokens == NULL) return;
  qa_peer_table_destroy(&l->peers); free(l->tokens); free(l->times);
  memset(l, 0, sizeof *l);
}
bool qa_flood_limiter_allow(qa_flood_limiter *l, const qa_net_address *address, double now) {
  int64_t slot = qa_peer_table_find(&l->peers, address);
  if (slot < 0) {
    if ((double)qa_peer_table_count(&l->peers) >= l->max_entries) qa_flood_limiter_expire(l, now);
    slot = qa_peer_table_claim(&l->peers, address);
    if (slot < 0) return false;
    l->tokens[slot] = l->burst;
    l->times[slot] = now;
  }
  double tokens = qa_js_min(l->burst, l->tokens[slot] + qa_js_max(0, now - l->times[slot]) / l->interval_ms);
  l->tokens[slot] = tokens >= 1 ? tokens - 1 : tokens;
  l->times[slot] = now;
  return tokens >= 1;
}
typedef struct flood_expire_scope { const qa_flood_limiter *limiter; double now; } flood_expire_scope;
static bool flood_keep(void *ctx, int64_t slot) {
  const flood_expire_scope *scope = ctx;
  return scope->now - scope->limiter->times[slot] <= scope->limiter->burst * scope->limiter->interval_ms;
}
void qa_flood_limiter_expire(qa_flood_limiter *l, double now) {
  flood_expire_scope scope = { l, now };
  qa_peer_table_retain(&l->peers, flood_keep, &scope);
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
/* UTF-16 length of the first `bytes` bytes of UTF-8 text. */
static size_t utf8_sequence(const unsigned char *p) {
  if (*p < 0x80) return 1;
  size_t need = *p >= 0xf0 ? 4 : *p >= 0xe0 ? 3 : *p >= 0xc0 ? 2 : 1;
  for (size_t i = 1; i < need; i++) if ((p[i] & 0xc0) != 0x80) return 1;
  return need;
}
/* String.prototype.slice(start, start + units) over UTF-8 text, in UTF-16 units. */
static char *slice_units(const char *text, size_t start_byte, size_t units) {
  size_t length = strlen(text), end = start_byte, used = 0;
  while (end < length && used < units) {
    size_t step = utf8_sequence((const unsigned char *)text + end);
    size_t width = step == 4 ? 2 : 1;
    if (used + width > units) break;
    used += width; end += step;
  }
  char *out = malloc(end - start_byte + 1);
  if (!out) qa_range_error("Out of memory");
  memcpy(out, text + start_byte, end - start_byte); out[end - start_byte] = '\0';
  return out;
}
char *qa_q3_rcon_command(const char *line) {
  size_t cursor = 4, len = strlen(line);
  /* line[cursor] past the end is undefined in TS: the scans stop and slice yields "". */
  if (cursor > len) cursor = len;
  while (cursor < len && line[cursor] == ' ') cursor++;
  while (cursor < len && line[cursor] != ' ') cursor++;
  while (cursor < len && line[cursor] == ' ') cursor++;
  return slice_units(line, cursor, 1023);
}
void qa_rcon_service_init(qa_rcon_service *s, qa_rcon_host host, bool is_q3, double output_bytes) {
  if (output_bytes != trunc(output_bytes) || output_bytes < 5 || output_bytes > 9007199254740991.0)
    qa_range_error("Rcon output buffer must hold a complete character and terminator");
  memset(s, 0, sizeof *s); s->host = host; s->is_q3 = is_q3; s->output_bytes = output_bytes;
  if (!qa_peer_table_init(&s->sources, 1024)) qa_range_error("Out of memory");
  s->last_times = calloc(1024, sizeof *s->last_times);
  if (!s->last_times) qa_range_error("Out of memory");
  qa_rcon_guard_init(&s->guard, 3, 1024);
}
void qa_rcon_service_destroy(qa_rcon_service *s) {
  qa_peer_table_destroy(&s->sources); free(s->last_times); s->last_times = NULL;
  qa_rcon_guard_destroy(&s->guard);
}
/* JS `value >>> 0`. */
static double to_uint32(double value) {
  if (!isfinite(value)) return 0;
  double t = fmod(trunc(value), 4294967296.0);
  return t < 0 ? t + 4294967296.0 : t;
}
struct rcon_acc { qa_rcon_service *svc; const qa_net_address *from; char *buf; size_t len, cap, units; };
static void rcon_flush(struct rcon_acc *a) {
  if (a->len == 0) return;
  a->buf[a->len] = '\0';
  a->len = 0; a->units = 0;
  a->svc->host.reply(a->svc->host.ctx, a->from, a->buf);
}
/* for (const character of text): one code point at a time; Q3 counts UTF-16 units of
 * byte characters, the unified profile counts UTF-8 bytes. */
static void rcon_output(void *ctx, const char *text, size_t length) {
  struct rcon_acc *a = ctx;
  for (size_t i = 0; i < length;) {
    size_t step = utf8_sequence((const unsigned char *)text + i);
    if (i + step > length) step = length - i;
    uint32_t code = (unsigned char)text[i];
    if (step == 2) code = (uint32_t)(text[i] & 0x1f) << 6 | (text[i + 1] & 0x3f);
    else if (step > 2) code = 0x800;
    if (a->svc->is_q3 && code > 255) qa_range_error("Source rcon output requires byte characters");
    size_t size = a->svc->is_q3 ? a->units + 1 : a->len + step;
    if ((double)size > a->svc->output_bytes - 1) rcon_flush(a);
    if (a->len + step + 1 > a->cap) {
      size_t nc = a->cap ? a->cap * 2 : 256; while (nc < a->len + step + 1) nc *= 2;
      char *grown = realloc(a->buf, nc);
      if (!grown) qa_range_error("Out of memory");
      a->buf = grown; a->cap = nc;
    }
    memcpy(a->buf + a->len, text + i, step); a->len += step; a->units++;
    i += step;
  }
}
struct execute_scope { qa_rcon_service *s; const char *command; struct rcon_acc *acc; };
static void execute_body(void *ctx) {
  struct execute_scope *scope = ctx;
  scope->s->host.execute(scope->s->host.ctx, scope->command, rcon_output, scope->acc);
}
static void free_text(void *text) { free(text); }
const char *qa_rcon_service_handle(qa_rcon_service *s, const qa_net_address *from,
                                   const char *supplied_password, const char *command, double milliseconds) {
  double time = s->is_q3 ? to_uint32(milliseconds) : milliseconds;
  int64_t known = qa_peer_table_find(&s->sources, from);
  double last = known < 0 ? 0 : s->last_times[known];
  double next = s->is_q3 ? to_uint32(last + 500) : last + 500;
  /* The 500 ms slot is per source so that spoofed packets from other addresses cannot lock
   * the administrator out. */
  if ((known >= 0 && time < next) || !qa_rcon_guard_permitted(&s->guard, from, milliseconds)) return "throttled";
  if (known < 0 && qa_peer_table_count(&s->sources) >= s->sources.capacity) qa_peer_table_clear(&s->sources);
  s->last_times[known >= 0 ? known : qa_peer_table_claim(&s->sources, from)] = time;
  const char *password = s->host.password(s->host.ctx);
  if (password[0] == '\0') { s->host.reply(s->host.ctx, from, "No rconpassword set on the server.\n"); return "disabled"; }
  if (!qa_password_matches(password, supplied_password)) {
    qa_rcon_guard_failed(&s->guard, from, milliseconds);
    s->host.reply(s->host.ctx, from, "Bad rconpassword.\n"); return "denied";
  }
  qa_rcon_guard_succeeded(&s->guard, from);
  struct rcon_acc acc; memset(&acc, 0, sizeof acc); acc.svc = s; acc.from = from;
  /* try { await execute(...) } finally { flush() } */
  if (command[0]) {
    char *sliced = s->is_q3 ? slice_units(command, 0, 1023) : NULL;
    qa_cleanup sliced_guard; qa_cleanup_push(&sliced_guard, free_text, sliced);
    struct execute_scope scope = { s, sliced ? sliced : command, &acc };
    qa_caught_error caught;
    bool ok = qa_try(execute_body, &scope, &caught);
    qa_cleanup_pop(&sliced_guard); free(sliced);
    if (!ok) {
      qa_cleanup buffer_guard; qa_cleanup_push(&buffer_guard, free_text, acc.buf);
      rcon_flush(&acc);
      qa_cleanup_pop(&buffer_guard); free(acc.buf);
      qa_rethrow(&caught);
    }
  }
  qa_cleanup buffer_guard; qa_cleanup_push(&buffer_guard, free_text, acc.buf);
  rcon_flush(&acc);
  qa_cleanup_pop(&buffer_guard);
  free(acc.buf);
  return "executed";
}
void qa_connectionless_gate_init(qa_connectionless_gate *g) {
  qa_flood_limiter_init(&g->per_source, 10, 200);
  qa_flood_limiter_init_max(&g->global, 400, 5, 1);
}
void qa_connectionless_gate_destroy(qa_connectionless_gate *g) {
  qa_flood_limiter_destroy(&g->per_source);
  qa_flood_limiter_destroy(&g->global);
}
bool qa_connectionless_gate_allow(qa_connectionless_gate *g, const qa_net_address *address, double now) {
  if (address->kind == QA_NET_LOOPBACK) return true;
  qa_net_address everyone;
  memset(&everyone, 0, sizeof everyone);
  everyone.kind = QA_NET_IPV4;
  return qa_flood_limiter_allow(&g->per_source, address, now) && qa_flood_limiter_allow(&g->global, &everyone, now);
}

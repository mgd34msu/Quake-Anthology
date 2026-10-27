/* Port of src/network/q3/pak-references.ts. GPL-2.0-or-later. */
#include "qa/network/q3/pak_references.h"
#include "qa/core/commands/text.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define ALL_REFERENCE_FLAGS 0x0f
#define BIG_INFO_PAYLOAD 8191
#define MAX_SEARCH_PATHS 4096
static char *xstrdup(const char *s) { size_t n = strlen(s); char *r = malloc(n + 1); if (r) memcpy(r, s, n + 1); return r; }
struct qa_q3_server_pak_set {
  int64_t *sums; size_t sum_count;
  char **names; size_t name_capacity;
};
bool qa_q3_server_pak_set_init(qa_q3_server_pak_set **out) { *out = calloc(1, sizeof **out); return *out != NULL; }
void qa_q3_server_pak_set_destroy(qa_q3_server_pak_set *s) {
  if (!s) return;
  free(s->sums);
  for (size_t i = 0; i < s->name_capacity; i++) free(s->names[i]);
  free(s->names); free(s);
}
const int64_t *qa_q3_server_pak_set_checksums(const qa_q3_server_pak_set *s, size_t *count) { *count = s->sum_count; return s->sums; }
static bool tokenize(const char *text, qa_command_tokens *out) {
  return qa_tokenize_command(text, QA_COMMAND_DIALECT_Q3, QA_COMMAND_TEXT_SOURCE, out);
}
int64_t qa_q3_server_pak_set_set_checksums(qa_q3_server_pak_set *s, const char *text) {
  qa_command_tokens t;
  if (!tokenize(text, &t)) qa_range_error("pak checksum tokenize allocation failed");
  size_t n = t.argc < MAX_SEARCH_PATHS ? t.argc : MAX_SEARCH_PATHS;
  int64_t *sums = malloc((n ? n : 1) * sizeof *sums);
  if (!sums) { qa_command_tokens_destroy(&t); qa_range_error("pak checksum allocation failed"); }
  for (size_t i = 0; i < n; i++) sums[i] = qa_native_atoi(t.argv[i]);
  qa_command_tokens_destroy(&t);
  free(s->sums); s->sums = sums; s->sum_count = n;
  return (int64_t)n;
}
static void ensure_names(qa_q3_server_pak_set *s, size_t need) {
  if (need <= s->name_capacity) return;
  char **nn = realloc(s->names, need * sizeof *nn);
  if (!nn) qa_range_error("pak name allocation failed");
  for (size_t i = s->name_capacity; i < need; i++) nn[i] = NULL;
  s->names = nn; s->name_capacity = need;
}
void qa_q3_server_pak_set_set_names(qa_q3_server_pak_set *s, const char *text, int64_t clear_count) {
  size_t clear = clear_count < 0 ? s->sum_count : (size_t)clear_count;
  ensure_names(s, clear);
  for (size_t i = 0; i < clear; i++) { free(s->names[i]); s->names[i] = NULL; }
  qa_command_tokens t;
  if (!tokenize(text, &t)) qa_range_error("pak name tokenize allocation failed");
  size_t n = t.argc < MAX_SEARCH_PATHS ? t.argc : MAX_SEARCH_PATHS;
  ensure_names(s, n);
  for (size_t i = 0; i < n; i++) { free(s->names[i]); s->names[i] = xstrdup(t.argv[i]); }
  qa_command_tokens_destroy(&t);
}
bool qa_q3_server_pak_set_snapshot(const qa_q3_server_pak_set *s, qa_q3_server_pak **out, size_t *count) {
  *out = malloc((s->sum_count ? s->sum_count : 1) * sizeof **out);
  if (!*out) return false;
  *count = s->sum_count;
  for (size_t i = 0; i < s->sum_count; i++) {
    (*out)[i].checksum = s->sums[i];
    (*out)[i].name = i < s->name_capacity ? s->names[i] : NULL;
  }
  return true;
}
static uint32_t checksum_pattern(int64_t value, const char *label) {
  if (value < -((int64_t)0x80000000) || value > 0xffffffffLL)
    qa_range_error("%s must be a signed or unsigned 32-bit integer", label);
  return (uint32_t)value;
}
typedef struct { char *buf; size_t len; } big_t;
static void big_append(big_t *b, const char *text) {
  if (b->len >= BIG_INFO_PAYLOAD) return;
  size_t remaining = BIG_INFO_PAYLOAD - b->len;
  size_t n = strlen(text); if (n > remaining) n = remaining;
  memcpy(b->buf + b->len, text, n); b->len += n; b->buf[b->len] = 0;
}
static big_t big_new(void) { big_t b = { calloc(BIG_INFO_PAYLOAD + 1, 1), 0 }; return b; }
static int ascii_lower(int c) { return c >= 65 && c <= 90 ? c + 32 : c; }
static bool suffix_equals(const char *path, const char *suffix) {
  size_t pl = strlen(path), sl = strlen(suffix);
  if (pl < sl) return false;
  size_t off = pl - sl;
  for (size_t i = 0; i < sl; i++) if (ascii_lower((unsigned char)path[off+i]) != (unsigned char)suffix[i]) return false;
  return true;
}
static bool excludes_general(const char *path) {
  return suffix_equals(path, ".shader") || suffix_equals(path, ".txt") || suffix_equals(path, ".cfg")
    || suffix_equals(path, ".config") || strstr(path, "levelshots") != NULL
    || suffix_equals(path, ".bot") || suffix_equals(path, ".arena") || suffix_equals(path, ".menu");
}
bool qa_q3_allowed_pure_loose_path(const char *path) {
  return suffix_equals(path, ".cfg") || suffix_equals(path, ".menu") || suffix_equals(path, ".game")
    || suffix_equals(path, ".dm_68") || suffix_equals(path, ".dat");
}
static int64_t checked_flags(int64_t flags) {
  if (flags < 0 || flags > ALL_REFERENCE_FLAGS)
    qa_range_error("Pak reference flags must use only the mask %d", ALL_REFERENCE_FLAGS);
  return flags;
}
bool qa_q3_is_pak_pure(int64_t checksum, const int64_t *server_checksums, size_t count) {
  uint32_t pattern = checksum_pattern(checksum, "Pak checksum");
  if (count == 0) return true;
  bool matched = false;
  for (size_t i = 0; i < count; i++)
    if (pattern == checksum_pattern(server_checksums[i], "Server pak checksum")) matched = true;
  return matched;
}
void qa_q3_reorder_pure_paks(const qa_q3_pure_search_path *search_paths, size_t count,
                             const int64_t *server_checksums, size_t checksum_count, qa_q3_pure_search_path *out) {
  memcpy(out, search_paths, count * sizeof *out);
  for (size_t i = 0; i < count; i++)
    if (out[i].kind == QA_Q3_PURE_PAK) checksum_pattern(out[i].checksum, "Pak checksum");
  size_t insertion = 0;
  for (size_t s = 0; s < checksum_count; s++) {
    uint32_t pattern = checksum_pattern(server_checksums[s], "Server pak checksum");
    for (size_t index = insertion; index < count; index++) {
      if (out[index].kind != QA_Q3_PURE_PAK || checksum_pattern(out[index].checksum, "Pak checksum") != pattern) continue;
      qa_q3_pure_search_path tmp = out[index];
      memmove(out + insertion + 1, out + insertion, (index - insertion) * sizeof *out);
      out[insertion] = tmp;
      insertion++;
      break;
    }
  }
}
typedef struct { const qa_q3_pak_catalog_entry *pack; int64_t flags; } state_t;
struct qa_q3_pak_references {
  state_t *states; size_t state_count;
  uint32_t checksum_feed;
  int64_t fake_checksum;
  qa_q3_pak_random_fn random; void *random_ctx;
};
static state_t *find_state(qa_q3_pak_references *r, const qa_q3_pak_catalog_entry *pack) {
  for (size_t i = 0; i < r->state_count; i++) if (r->states[i].pack == pack) return &r->states[i];
  return NULL;
}
static void add_state(qa_q3_pak_references *r, const qa_q3_pak_catalog_entry *pack, state_t *out) {
  char label[512];
  snprintf(label, sizeof label, "Checksum for \"%s\"", pack->archive_path);
  checksum_pattern(pack->checksum, label);
  snprintf(label, sizeof label, "Pure checksum for \"%s\"", pack->archive_path);
  checksum_pattern(pack->pure_checksum, label);
  if (find_state(r, pack)) qa_range_error("Pak catalog contains the same identity more than once");
  out->pack = pack; out->flags = 0;
}
bool qa_q3_pak_references_init(qa_q3_pak_references **out, const qa_q3_pak_catalog_entry *const *packs, size_t pack_count,
                               int64_t checksum_feed, qa_q3_pak_random_fn random, void *random_ctx) {
  qa_q3_pak_references *r = calloc(1, sizeof *r);
  if (!r) return false;
  r->checksum_feed = checksum_pattern(checksum_feed, "Checksum feed");
  r->random = random; r->random_ctx = random_ctx;
  r->states = calloc(pack_count ? pack_count : 1, sizeof *r->states);
  if (!r->states) { free(r); return false; }
  for (size_t i = 0; i < pack_count; i++) { r->state_count = i; add_state(r, packs[i], &r->states[i]); }
  r->state_count = pack_count;
  *out = r; return true;
}
void qa_q3_pak_references_destroy(qa_q3_pak_references *r) { if (!r) return; free(r->states); free(r); }
int64_t qa_q3_pak_references_checksum_feed(const qa_q3_pak_references *r) { return r->checksum_feed; }
void qa_q3_pak_references_prepend_pack(qa_q3_pak_references *r, const qa_q3_pak_catalog_entry *pack) {
  state_t st; add_state(r, pack, &st);
  state_t *ns = realloc(r->states, (r->state_count + 1) * sizeof *ns);
  if (!ns) qa_range_error("pak references allocation failed");
  r->states = ns;
  memmove(r->states + 1, r->states, r->state_count * sizeof *ns);
  r->states[0] = st; r->state_count++;
}
void qa_q3_pak_references_reorder_packs(qa_q3_pak_references *r, const qa_q3_pak_catalog_entry *const *packs, size_t pack_count) {
  bool dup = false;
  for (size_t i = 0; i < pack_count && !dup; i++) for (size_t j = i + 1; j < pack_count; j++) if (packs[i] == packs[j]) dup = true;
  if (pack_count != r->state_count || dup)
    qa_range_error("Pak reorder must retain every catalog identity exactly once");
  state_t *ns = malloc((pack_count ? pack_count : 1) * sizeof *ns);
  if (!ns) qa_range_error("pak references allocation failed");
  for (size_t i = 0; i < pack_count; i++) {
    state_t *st = find_state(r, packs[i]);
    if (!st) { free(ns); qa_range_error("Pak reorder contains an unmounted identity"); }
    ns[i] = *st;
  }
  free(r->states); r->states = ns;
}
void qa_q3_pak_references_retain_loose_reference(qa_q3_pak_references *r, const qa_q3_pak_references *previous) {
  r->fake_checksum = previous->fake_checksum;
}
void qa_q3_pak_references_record_packed_open(qa_q3_pak_references *r, const qa_q3_pak_catalog_entry *pack, const char *requested_path) {
  state_t *state = find_state(r, pack);
  if (!state) qa_range_error("Packed open does not belong to this pak catalog");
  if ((state->flags & QA_Q3_PAK_FLAG_GENERAL) == 0 && !excludes_general(requested_path)) state->flags |= QA_Q3_PAK_FLAG_GENERAL;
  if ((state->flags & QA_Q3_PAK_FLAG_QAGAME) == 0 && strstr(requested_path, "qagame.qvm")) state->flags |= QA_Q3_PAK_FLAG_QAGAME;
  if ((state->flags & QA_Q3_PAK_FLAG_CGAME) == 0 && strstr(requested_path, "cgame.qvm")) state->flags |= QA_Q3_PAK_FLAG_CGAME;
  if ((state->flags & QA_Q3_PAK_FLAG_UI) == 0 && strstr(requested_path, "ui.qvm")) state->flags |= QA_Q3_PAK_FLAG_UI;
}
void qa_q3_pak_references_record_loose_open(qa_q3_pak_references *r, const char *requested_path) {
  if (qa_q3_allowed_pure_loose_path(requested_path)) return;
  double random = r->random(r->random_ctx);
  if (!isfinite(random) || random < 0 || random > 1)
    qa_range_error("Filesystem random source must return a finite value from 0 through 1");
  r->fake_checksum = (int64_t)trunc(random);
}
void qa_q3_pak_references_clear(qa_q3_pak_references *r, int64_t flags) {
  int64_t mask = flags == 0 ? ALL_REFERENCE_FLAGS : checked_flags(flags);
  for (size_t i = 0; i < r->state_count; i++) r->states[i].flags &= ~mask;
}
bool qa_q3_pak_references_snapshot(const qa_q3_pak_references *r, qa_q3_pak_reference_snapshot **out, size_t *count) {
  *out = malloc((r->state_count ? r->state_count : 1) * sizeof **out);
  if (!*out) return false;
  *count = r->state_count;
  for (size_t i = 0; i < r->state_count; i++) { (*out)[i].pack = r->states[i].pack; (*out)[i].flags = r->states[i].flags; }
  return true;
}
static void append_signed(big_t *b, int64_t v, bool space) {
  char buf[24]; snprintf(buf, sizeof buf, space ? "%d " : "%d", (int32_t)v);
  big_append(b, buf);
}
char *qa_q3_pak_references_loaded_pak_checksums(const qa_q3_pak_references *r) {
  big_t b = big_new(); if (!b.buf) return NULL;
  for (size_t i = 0; i < r->state_count; i++) append_signed(&b, r->states[i].pack->checksum, true);
  return b.buf;
}
char *qa_q3_pak_references_loaded_pak_names(const qa_q3_pak_references *r) {
  big_t b = big_new(); if (!b.buf) return NULL;
  for (size_t i = 0; i < r->state_count; i++) {
    if (b.len > 0) big_append(&b, " ");
    big_append(&b, r->states[i].pack->basename);
  }
  return b.buf;
}
char *qa_q3_pak_references_loaded_pak_pure_checksums(const qa_q3_pak_references *r) {
  big_t b = big_new(); if (!b.buf) return NULL;
  for (size_t i = 0; i < r->state_count; i++) append_signed(&b, r->states[i].pack->pure_checksum, true);
  return b.buf;
}
static bool included_report(const state_t *state) {
  if (state->flags != 0) return true;
  const char *game = state->pack->game;
  char first[7]; size_t i;
  for (i = 0; i < 6 && game[i]; i++) first[i] = (char)ascii_lower((unsigned char)game[i]);
  first[i] = 0;
  return strcmp(first, "baseq3") != 0;
}
char *qa_q3_pak_references_referenced_pak_checksums(const qa_q3_pak_references *r) {
  big_t b = big_new(); if (!b.buf) return NULL;
  for (size_t i = 0; i < r->state_count; i++)
    if (included_report(&r->states[i])) append_signed(&b, r->states[i].pack->checksum, true);
  return b.buf;
}
char *qa_q3_pak_references_referenced_pak_names(const qa_q3_pak_references *r) {
  big_t b = big_new(); if (!b.buf) return NULL;
  for (size_t i = 0; i < r->state_count; i++) {
    if (b.len > 0) big_append(&b, " ");
    if (included_report(&r->states[i])) {
      big_append(&b, r->states[i].pack->game); big_append(&b, "/"); big_append(&b, r->states[i].pack->basename);
    }
  }
  return b.buf;
}
char *qa_q3_pak_references_referenced_pak_pure_checksums(const qa_q3_pak_references *r) {
  big_t b = big_new(); if (!b.buf) return NULL;
  uint32_t checksum = r->checksum_feed;
  int64_t general_count = 0;
  static const int order[3] = { QA_Q3_PAK_FLAG_CGAME, QA_Q3_PAK_FLAG_UI, QA_Q3_PAK_FLAG_GENERAL };
  for (int f = 0; f < 3; f++) {
    int flag = order[f];
    if (flag == QA_Q3_PAK_FLAG_GENERAL) big_append(&b, "@ ");
    for (size_t i = 0; i < r->state_count; i++) {
      const state_t *state = &r->states[i];
      if ((state->flags & flag) == 0) continue;
      append_signed(&b, state->pack->pure_checksum, true);
      if (flag == QA_Q3_PAK_FLAG_CGAME || flag == QA_Q3_PAK_FLAG_UI) break;
      checksum = checksum ^ (uint32_t)state->pack->pure_checksum;
      general_count++;
    }
    if (r->fake_checksum != 0) { char buf[24]; snprintf(buf, sizeof buf, "%lld ", (long long)r->fake_checksum); big_append(&b, buf); }
  }
  checksum = checksum ^ (uint32_t)general_count;
  append_signed(&b, (int32_t)checksum, false);
  return b.buf;
}
char *qa_q3_pak_references_game_pure_checksum(const qa_q3_pak_references *r) {
  char *info = xstrdup("");
  if (!info) return NULL;
  for (size_t i = 0; i < r->state_count; i++) {
    if ((r->states[i].flags & QA_Q3_PAK_FLAG_QAGAME) != 0) {
      char buf[16]; snprintf(buf, sizeof buf, "%d", (int32_t)r->states[i].pack->checksum);
      free(info); info = xstrdup(buf);
      if (!info) return NULL;
    }
  }
  return info;
}

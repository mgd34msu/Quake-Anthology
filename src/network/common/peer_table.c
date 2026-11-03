/* Port of src/network/common/peer-table.ts. GPL-2.0-or-later. */
#include "qa/network/common/peer_table.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"

enum { KIND_IPV4 = 1, KIND_IPV6 = 2, KIND_LOOPBACK = 3, KIND_IPX = 4 };

/* FNV-1a over charCodeAt units with Math.imul. C strings are bytes; for the ASCII ipv6 hosts and
 * loopback ids this is identical, and the hash only selects a bucket (matches() compares names). */
static uint32_t text_hash(const char *text) {
  uint32_t hash = 0x811c9dc5u;
  for (const unsigned char *p = (const unsigned char *)(text ? text : ""); *p; p++) hash = (hash ^ *p) * 0x01000193u;
  return hash;
}
static uint8_t kind_of(const qa_net_address *a) {
  switch (a->kind) {
    case QA_NET_IPV4: return KIND_IPV4;
    case QA_NET_IPV6: return KIND_IPV6;
    case QA_NET_LOOPBACK: return KIND_LOOPBACK;
    case QA_NET_IPX: return KIND_IPX;
  }
  return 0;
}
static uint32_t first_word(const qa_net_address *a) {
  switch (a->kind) {
    case QA_NET_IPV4: return ((uint32_t)a->ipv4[0] << 24) | ((uint32_t)a->ipv4[1] << 16) | ((uint32_t)a->ipv4[2] << 8) | a->ipv4[3];
    case QA_NET_IPV6:
    case QA_NET_LOOPBACK: return text_hash(a->host);
    case QA_NET_IPX: return a->network;
  }
  return 0;
}
static uint32_t second_word(const qa_net_address *a) {
  return a->kind == QA_NET_IPX ? ((uint32_t)a->node[0] << 24) | ((uint32_t)a->node[1] << 16) | ((uint32_t)a->node[2] << 8) | a->node[3] : 0;
}
static uint32_t third_word(const qa_net_address *a) {
  return a->kind == QA_NET_IPX ? ((uint32_t)a->node[4] << 8) | a->node[5] : 0;
}
static const char *name_of(const qa_net_address *a) {
  return a->kind == QA_NET_IPV6 || a->kind == QA_NET_LOOPBACK ? (a->host ? a->host : "") : NULL;
}
static bool same_name(const char *left, const char *right) {
  if (left == NULL || right == NULL) return left == right;
  return strcmp(left, right) == 0;
}
static bool matches(const qa_peer_table *t, int64_t slot, const qa_net_address *a) {
  return t->kinds[slot] == kind_of(a) && t->first[slot] == first_word(a) && t->second[slot] == second_word(a) &&
         t->third[slot] == third_word(a) && same_name(t->names[slot], name_of(a));
}

bool qa_peer_table_init(qa_peer_table *t, double capacity) {
  if (!(capacity == trunc(capacity) && fabs(capacity) <= 9007199254740991.0) || capacity < 1)
    qa_range_error("Peer table capacity must be a positive safe integer");
  memset(t, 0, sizeof *t);
  size_t n = (size_t)capacity;
  t->capacity = (int64_t)n;
  qa_id_index_init(&t->index);
  qa_id_index_reserve(&t->index, (int64_t)n);
  t->kinds = calloc(n, 1);
  t->first = calloc(n, sizeof *t->first);
  t->second = calloc(n, sizeof *t->second);
  t->third = calloc(n, sizeof *t->third);
  t->names = calloc(n, sizeof *t->names);
  t->free_slots = calloc(n, sizeof *t->free_slots);
  t->used = calloc(n, 1);
  t->born = calloc(n, sizeof *t->born);
  if (!t->kinds || !t->first || !t->second || !t->third || !t->names || !t->free_slots || !t->used || !t->born) {
    qa_peer_table_destroy(t);
    return false;
  }
  for (size_t slot = 0; slot < n; slot++) t->free_slots[slot] = (int32_t)(n - 1 - slot);
  t->free_count = (int64_t)n;
  return true;
}
void qa_peer_table_destroy(qa_peer_table *t) {
  if (t->names) for (int64_t slot = 0; slot < t->capacity; slot++) free(t->names[slot]);
  qa_id_index_destroy(&t->index);
  free(t->kinds); free(t->first); free(t->second); free(t->third); free(t->names);
  free(t->free_slots); free(t->used); free(t->born);
  memset(t, 0, sizeof *t);
}
int64_t qa_peer_table_count(const qa_peer_table *t) { return t->capacity - t->free_count; }
bool qa_peer_table_live(const qa_peer_table *t, int64_t slot) {
  return slot >= 0 && slot < t->capacity && t->used[slot] == 1;
}
int64_t qa_peer_table_find(const qa_peer_table *t, const qa_net_address *a) {
  qa_id_index_cursor cursor = qa_id_index_begin(&t->index, first_word(a), kind_of(a));
  for (int64_t slot = qa_id_index_next(&t->index, &cursor); slot >= 0; slot = qa_id_index_next(&t->index, &cursor))
    if (matches(t, slot, a)) return slot;
  return -1;
}
int64_t qa_peer_table_claim(qa_peer_table *t, const qa_net_address *a) {
  int64_t known = qa_peer_table_find(t, a);
  if (known >= 0 || t->free_count == 0) return known;
  int64_t slot = t->free_slots[--t->free_count];
  if (slot < 0) return -1;
  const char *name = name_of(a);
  char *owned = NULL;
  if (name != NULL) {
    size_t length = strlen(name) + 1;
    owned = malloc(length);
    if (owned == NULL) { t->free_count++; qa_range_error("Out of memory"); }
    memcpy(owned, name, length);
  }
  t->kinds[slot] = kind_of(a);
  t->first[slot] = first_word(a);
  t->second[slot] = second_word(a);
  t->third[slot] = third_word(a);
  t->names[slot] = owned;
  t->used[slot] = 1;
  t->born[slot] = t->sequence++;
  qa_id_index_insert(&t->index, t->first[slot], t->kinds[slot], slot);
  return slot;
}
int64_t qa_peer_table_oldest(const qa_peer_table *t) {
  int64_t best = -1;
  for (int64_t slot = 0; slot < t->capacity; slot++)
    if (t->used[slot] == 1 && (best < 0 || t->born[slot] < t->born[best])) best = slot;
  return best;
}
void qa_peer_table_release(qa_peer_table *t, int64_t slot) {
  /* TS reads used[slot] of a typed array: an out-of-range slot (including -1) reads undefined. */
  if (slot < 0 || slot >= t->capacity || t->used[slot] != 1) return;
  qa_id_index_remove_value(&t->index, t->first[slot], t->kinds[slot], slot);
  t->free_slots[t->free_count++] = (int32_t)slot;
  t->used[slot] = 0;
  free(t->names[slot]);
  t->names[slot] = NULL;
}
static bool keep_live(uint32_t key, uint32_t discriminator, int64_t value, void *ctx) {
  (void)key; (void)discriminator; /* TS (_key, _discriminator, value) => used[value] === 1 */
  return ((const qa_peer_table *)ctx)->used[value] == 1;
}
int64_t qa_peer_table_retain(qa_peer_table *t, qa_peer_table_keep_fn keep, void *ctx) {
  int64_t removed = 0;
  for (int64_t slot = 0; slot < t->capacity; slot++) {
    if (t->used[slot] != 1 || (keep != NULL && keep(ctx, slot))) continue;
    t->used[slot] = 0;
    free(t->names[slot]);
    t->names[slot] = NULL;
    t->free_slots[t->free_count++] = (int32_t)slot;
    removed++;
  }
  if (removed > 0) qa_id_index_retain(&t->index, keep_live, t);
  return removed;
}
void qa_peer_table_clear(qa_peer_table *t) { qa_peer_table_retain(t, NULL, NULL); }

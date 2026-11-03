/* Port of src/network/common/peer-table.ts: fixed-capacity peer slots keyed by network
 * address without its port. Slot numbers index caller-owned arrays, so per-peer state needs no
 * string keys or per-lookup allocation. Exhaustion is explicit (-1). Names (ipv6 host, loopback
 * id) are owned copies. RangeError -> qa_range_error. GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_PEER_TABLE_H
#define QA_NETWORK_COMMON_PEER_TABLE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/foundation/id_index.h"
#include "qa/network/common/endpoint.h"
typedef struct qa_peer_table {
  int64_t capacity;
  qa_id_index index;
  uint8_t *kinds;
  uint32_t *first, *second, *third;
  char **names;
  int32_t *free_slots;
  uint8_t *used;
  int64_t free_count;
  /* Admission order of each live slot; the lowest value is the oldest peer. */
  double *born;
  double sequence;
} qa_peer_table;
/* Aborts unless capacity is a positive safe integer; false only on allocation failure. */
bool qa_peer_table_init(qa_peer_table *table, double capacity);
void qa_peer_table_destroy(qa_peer_table *table);
int64_t qa_peer_table_count(const qa_peer_table *table);
bool qa_peer_table_live(const qa_peer_table *table, int64_t slot);
/* Returns the slot holding this address, or -1. */
int64_t qa_peer_table_find(const qa_peer_table *table, const qa_net_address *address);
/* Returns the existing slot or admits the address into a free one; -1 when the table is full. */
int64_t qa_peer_table_claim(qa_peer_table *table, const qa_net_address *address);
/* Slot of the longest-resident peer, or -1 when empty. */
int64_t qa_peer_table_oldest(const qa_peer_table *table);
void qa_peer_table_release(qa_peer_table *table, int64_t slot);
typedef bool (*qa_peer_table_keep_fn)(void *ctx, int64_t slot);
/* Frees every live slot for which keep returns false in one index rebuild. Returns the number freed. */
int64_t qa_peer_table_retain(qa_peer_table *table, qa_peer_table_keep_fn keep, void *ctx);
void qa_peer_table_clear(qa_peer_table *table);
#endif

/* Port of src/network/common/rcon-guard.ts. GPL-2.0-or-later. */
#include "qa/network/common/rcon_guard.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
#include "qa/network/common/session.h"
#include "qa/foundation/js.h"
bool qa_password_matches(const char *expected, const char *supplied) {
  qa_net_sha256 hash; uint8_t left[32], right[32];
  qa_net_sha256_init(&hash); qa_net_sha256_update(&hash, (const uint8_t *)expected, strlen(expected)); qa_net_sha256_digest(&hash, left);
  qa_net_sha256_init(&hash); qa_net_sha256_update(&hash, (const uint8_t *)supplied, strlen(supplied)); qa_net_sha256_digest(&hash, right);
  volatile uint8_t difference = 0;
  for (int i = 0; i < 32; i++) difference |= (uint8_t)(left[i] ^ right[i]);
  return difference == 0 && expected[0] != '\0';
}
void qa_rcon_guard_init(qa_rcon_guard *guard, double free_attempts, size_t max_sources) {
  memset(guard, 0, sizeof *guard);
  guard->free_attempts = free_attempts; guard->max_sources = max_sources;
  if (!qa_peer_table_init(&guard->sources, (double)max_sources)) qa_range_error("Out of memory");
  guard->failures = calloc(max_sources, sizeof *guard->failures);
  guard->until = calloc(max_sources, sizeof *guard->until);
  if (!guard->failures || !guard->until) qa_range_error("Out of memory");
}
void qa_rcon_guard_destroy(qa_rcon_guard *guard) {
  if (guard->max_sources == 0) return;
  qa_peer_table_destroy(&guard->sources);
  free(guard->failures); free(guard->until);
  memset(guard, 0, sizeof *guard);
}
bool qa_rcon_guard_permitted(const qa_rcon_guard *guard, const qa_net_address *from, double now) {
  int64_t slot = qa_peer_table_find(&guard->sources, from);
  return slot < 0 || now >= guard->until[slot];
}
void qa_rcon_guard_failed(qa_rcon_guard *guard, const qa_net_address *from, double now) {
  int64_t slot = qa_peer_table_find(&guard->sources, from);
  if (slot < 0) {
    if (qa_peer_table_count(&guard->sources) >= guard->sources.capacity)
      qa_peer_table_release(&guard->sources, qa_peer_table_oldest(&guard->sources));
    slot = qa_peer_table_claim(&guard->sources, from);
    guard->failures[slot] = 0;
    guard->until[slot] = 0;
  }
  /* Int32Array store of failures + 1. */
  int32_t failures = qa_js_to_int32((double)guard->failures[slot] + 1);
  guard->failures[slot] = failures;
  if (failures >= guard->free_attempts)
    guard->until[slot] = now + qa_js_min(60000, 1000 * qa_js_pow(2, failures - guard->free_attempts));
}
void qa_rcon_guard_succeeded(qa_rcon_guard *guard, const qa_net_address *from) {
  qa_peer_table_release(&guard->sources, qa_peer_table_find(&guard->sources, from));
}

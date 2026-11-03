/* Port of src/network/common/rcon-guard.ts: digest password comparison and
 * per-source rcon failure backoff with a bounded table. GPL-2.0-or-later. */
#ifndef QA_NETWORK_COMMON_RCON_GUARD_H
#define QA_NETWORK_COMMON_RCON_GUARD_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/network/common/endpoint.h"
#include "qa/network/common/peer_table.h"
/* SHA-256 digests compared in constant time, so a mismatch does not reveal how much of
 * the password matched; an empty expected password never matches. UTF-8 text. */
bool qa_password_matches(const char *expected, const char *supplied);
typedef struct qa_rcon_guard {
  double free_attempts; size_t max_sources; /* max_sources 0: not initialized */
  qa_peer_table sources;
  int32_t *failures; /* Int32Array(maxSources) */
  double *until;     /* Float64Array(maxSources) */
} qa_rcon_guard;
/* TS defaults: freeAttempts 3, maxSources 1024. */
void qa_rcon_guard_init(qa_rcon_guard *guard, double free_attempts, size_t max_sources);
void qa_rcon_guard_destroy(qa_rcon_guard *guard);
bool qa_rcon_guard_permitted(const qa_rcon_guard *guard, const qa_net_address *from, double now);
void qa_rcon_guard_failed(qa_rcon_guard *guard, const qa_net_address *from, double now);
void qa_rcon_guard_succeeded(qa_rcon_guard *guard, const qa_net_address *from);
#endif

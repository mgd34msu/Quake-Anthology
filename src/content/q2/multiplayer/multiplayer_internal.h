/* Shared helpers for the Q2 multiplayer port. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MULTIPLAYER_INTERNAL_H
#define QA_CONTENT_Q2_MULTIPLAYER_INTERNAL_H
#include <math.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/content/q2/base/player/index.h"
#include "qa/content/q2/base/player/spawns.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/weapons/types.h" /* qa_q2f_error (plain TS Error) */
#include "qa/contracts/equipment.h"
#include "qa/core/math.h"
#include "qa/foundation/support.h" /* qa_range_error */
#include "qa/world/actors/body.h"
#include "qa/world/actors/registry.h"
#include "qa/world/gameplay/authority.h"
#include "qa/world/gameplay/inventory.h"

static inline void *qa_q2mp_xalloc(void *p) {
  if (p == NULL) qa_q2f_error("Out of memory");
  return p;
}
static inline char *qa_q2mp_dup(const char *s) {
  size_t n = strlen(s) + 1;
  char *out = qa_q2mp_xalloc(malloc(n));
  memcpy(out, s, n);
  return out;
}
/* TS String(number) for the values formatted by this slice. */
static inline void qa_q2mp_format_number(char *buf, size_t size, double value) {
  if (isnan(value)) { snprintf(buf, size, "NaN"); return; }
  if (isinf(value)) { snprintf(buf, size, "%s", value > 0 ? "Infinity" : "-Infinity"); return; }
  if (value == trunc(value) && fabs(value) < 1e21) { snprintf(buf, size, "%.0f", value); return; }
  snprintf(buf, size, "%.15g", value);
}
static inline double qa_q2mp_now(qa_q2_game_services *g) { return g->host->now(g->host->ctx); }
static inline qa_combat_state_result qa_q2mp_combat_read(qa_q2_game_services *g, qa_actor_id a) {
  return qa_gameplay_authority_read(g->host->combat, a);
}
static inline double qa_q2mp_health_or(qa_q2_game_services *g, qa_actor_id a, double fallback) {
  qa_combat_state_result r = qa_q2mp_combat_read(g, a);
  return r.has_value ? r.value.health : fallback;
}
static inline double qa_q2mp_inv_count(qa_q2_game_services *g, qa_actor_id a, qa_item_id item) {
  return qa_shared_inventory_table_count(g->host->inventory, a, item);
}
static inline qa_trace_result qa_q2mp_trace(qa_q2_game_services *g, qa_vec3 start, qa_vec3 end,
                                            const qa_bounds *bounds, const qa_actor_id *ignore, double mask) {
  qa_q2_trace_request req = {0};
  req.start = start; req.end = end; req.bounds = bounds; req.ignore = ignore; req.mask = mask;
  return g->host->trace(g->host->ctx, req);
}
/* src/content/q2/base/player/spawns.ts helpers (owner port:
 * qa/content/q2/base/player/spawns.h), under the multiplayer
 * slice's qa_q2mp_* names. Results are malloc'd snapshot arrays. */
#define qa_q2mp_entities_named qa_q2_entities_named
#define qa_q2mp_players_range qa_q2_players_range
#define qa_q2mp_mirror_kill_box qa_q2_player_kill_box
#define qa_q2mp_spawn_origin qa_q2_spawn_origin
/* TS selectQ2Spawn takes the player state; the multiplayer slice
 * only carries the slot, so this wrapper adapts it (impl in
 * spawns_mirror.c). Aborts when no spot exists. */
qa_q2_entity *qa_q2mp_select_spawn(qa_q2_game_services *g, double slot, const char *spawn_point);
/* TS q2Userinfo(source).get(key) from base/player/index.ts; writes
 * the value into buf and returns it, or NULL when absent. */
#define qa_q2mp_userinfo_get qa_q2_userinfo_get
#endif

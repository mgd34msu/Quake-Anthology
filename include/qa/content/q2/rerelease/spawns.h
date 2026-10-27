/*
 * Port of src/content/q2/rerelease/spawns.ts. GPL-2.0-or-later.
 * Base helpers (TS owner: src/content/q2/base/player/spawns.ts and
 * landmarks.ts, unported) are fn+ctx in qa_q2rr_spawn_helpers;
 * q2EntitiesNamed defaults to a scan of game.entities when unset.
 */
#ifndef QA_CONTENT_Q2_RERELEASE_SPAWNS_H
#define QA_CONTENT_Q2_RERELEASE_SPAWNS_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/types.h"
extern const qa_bounds QA_Q2_RERELEASE_PLAYER_BOUNDS;
typedef struct qa_q2rr_spawn_helpers {
  void *ctx;
  qa_q2_entity **(*entities_named)(void *ctx, qa_q2_game_services *g, const char *name, size_t *n, qa_q2_entity **buf, size_t cap);
  double (*players_range)(void *ctx, qa_q2_game_services *g, qa_q2_entity *spot);
  bool (*fix_stuck_player)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_vec3 origin, qa_bounds b, qa_vec3 *out);
  bool (*place_landmark)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const qa_q2_landmark_carry *lm,
                         qa_q2_entity *spot, qa_bounds b, qa_vec3 *origin, qa_vec3 *angles, qa_vec3 *velocity);
} qa_q2rr_spawn_helpers;
extern qa_q2rr_spawn_helpers qa_q2rr_spawns; /* set by base port/test */
/* Fills buf (cap entries), returns total count. */
size_t qa_q2rr_entities_named(qa_q2_game_services *g, const char *name, qa_q2_entity **buf, size_t cap);
qa_q2_spawn_module qa_q2_get_rerelease_spawns(void);
qa_q2_callback_definitions qa_q2_rerelease_spawns_callbacks(void);
qa_q2_entity *qa_q2_rerelease_single_spawn(qa_q2_game_services *g, const char *spawn_point);
qa_q2_entity *qa_q2_select_rerelease_spawn(qa_q2_game_services *g, qa_q2_entity *player, qa_bounds b,
                                           qa_q2_rerelease_options o, const char *spawn_point, bool force);
#endif

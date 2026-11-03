/*
 * Port of src/content/q2/rerelease/spawns.ts. GPL-2.0-or-later.
 * Uses the base q2EntitiesNamed, q2PlayersRange and fixQ2StuckPlayer
 * (content/q2/base/player) directly, as the TS imports them.
 */
#ifndef QA_CONTENT_Q2_RERELEASE_SPAWNS_H
#define QA_CONTENT_Q2_RERELEASE_SPAWNS_H
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/rerelease/types.h"
extern const qa_bounds QA_Q2_RERELEASE_PLAYER_BOUNDS;
/* TS q2EntitiesNamed order; fills buf (cap entries), returns total count. */
size_t qa_q2rr_entities_named(qa_q2_game_services *g, const char *name, qa_q2_entity **buf, size_t cap);
qa_q2_spawn_module qa_q2_get_rerelease_spawns(void);
qa_q2_callback_definitions qa_q2_rerelease_spawns_callbacks(void);
qa_q2_entity *qa_q2_rerelease_single_spawn(qa_q2_game_services *g, const char *spawn_point);
qa_q2_entity *qa_q2_select_rerelease_spawn(qa_q2_game_services *g, qa_q2_entity *player, qa_bounds b,
                                           qa_q2_rerelease_options o, const char *spawn_point, bool force);
#endif

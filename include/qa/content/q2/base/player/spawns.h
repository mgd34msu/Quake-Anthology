/* Port of src/content/q2/base/player/spawns.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_PLAYER_SPAWNS_H
#define QA_CONTENT_Q2_BASE_PLAYER_SPAWNS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/callbacks.h"

/* TS q2EntitiesNamed. The returned array is malloc'd; caller frees. */
qa_q2_entity **qa_q2_entities_named(qa_q2_game_services *game, const char *classname, size_t *out_count);
/* TS q2PlayerSpawnCallbacks: think SP_CreateCoopSpots and SP_FixCoopSpots (static data). */
const qa_q2_callback_definitions *qa_q2_player_spawn_callbacks(void);
/* TS q2PlayerSpawns spawn module. */
bool qa_q2_player_spawns_spawn(qa_q2_entity *entity, qa_q2_game_services *game);
qa_q2_spawn_module qa_q2_get_player_spawns(void);
double qa_q2_players_range(qa_q2_game_services *game, qa_q2_entity *spot);
/* TS selectQ2Spawn (aborts with the TS Error when no spawn matches). */
qa_q2_entity *qa_q2_select_spawn(qa_q2_game_services *game, qa_q2_base_player_state *state, const char *spawn_point);
/* TS q2KillBox (distinct from foundation scenery's killQ2Box). */
bool qa_q2_player_kill_box(qa_q2_entity *entity, qa_q2_game_services *game);
qa_vec3 qa_q2_spawn_origin(qa_q2_entity *entity, qa_q2_game_services *game);

#endif

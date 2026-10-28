/* Port of src/content/q3/team-arena/arenas.ts. GPL-2.0-or-later.
 * s.legsAnim/torsoAnim have no landed entity-state mirror fields; the
 * animation stores are skipped (think/event side effects kept). */
#ifndef QA_CONTENT_Q3_TEAM_ARENA_ARENAS_H
#define QA_CONTENT_Q3_TEAM_ARENA_ARENAS_H
#include <stdbool.h>
#include "qa/content/q3/base/game/entities.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/game/utilities.h"
#include "qa/content/q3/base/world.h"
#include "qa/content/q3/team_arena/match.h"
#include "qa/core/cvars/index.h"
#include "qa/persistence/value.h"

/* TS ArenaHost. */
typedef struct qa_q3_ta_arena_host {
  qa_q3_ta_match_runtime *match;
  qa_q3_base_server_world *world;
  qa_cvar_registry *cvars;
  qa_q3_config_string_registry *config;
} qa_q3_ta_arena_host;

/* TS captureSaveState() result shape. has_* false is TS null. */
typedef struct qa_q3_ta_arena_save_state {
  bool has_podium1, has_podium2, has_podium3;
  double podium1_slot, podium2_slot, podium3_slot;
} qa_q3_ta_arena_save_state;

/* TS ArenaRuntime. Podium entities borrowed from the match pool. */
typedef struct qa_q3_ta_arena_runtime {
  qa_q3_ta_arena_host host;
  qa_q3_game_entity *podium1, *podium2, *podium3;
} qa_q3_ta_arena_runtime;
void qa_q3_ta_arena_runtime_init(qa_q3_ta_arena_runtime *runtime, qa_q3_ta_arena_host host);
qa_q3_ta_arena_save_state qa_q3_ta_arena_runtime_capture_save_state(qa_q3_ta_arena_runtime *runtime);
void qa_q3_ta_arena_runtime_restore_save_state(qa_q3_ta_arena_runtime *runtime, const qa_save_value *value);
void qa_q3_ta_arena_runtime_reset_podium_players(qa_q3_ta_arena_runtime *runtime);
void qa_q3_ta_arena_runtime_update_tournament_info(qa_q3_ta_arena_runtime *runtime);
void qa_q3_ta_arena_runtime_spawn_models_on_victory_pads(qa_q3_ta_arena_runtime *runtime);
void qa_q3_ta_arena_runtime_abort_podium(qa_q3_ta_arena_runtime *runtime);
void qa_q3_ta_arena_runtime_bind_save_callbacks(qa_q3_ta_arena_runtime *runtime);
#endif

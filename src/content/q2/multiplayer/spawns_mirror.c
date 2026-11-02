/* Slot adapter for TS selectQ2Spawn (owner:
 * src/content/q2/base/player/spawns.ts, qa_q2_select_spawn): the
 * multiplayer slice's callers carry only the player slot (the
 * one state field selectQ2Spawn reads). The remaining spawns
 * helpers bind directly to the owner in multiplayer_internal.h.
 * GPL-2.0-or-later. */
#include "multiplayer_internal.h"

qa_q2_entity *qa_q2mp_select_spawn(qa_q2_game_services *g, double slot,
                                   const char *spawn_point) {
  qa_q2_base_player_state state;
  qa_q2_base_player_state_init(&state, slot, 0);
  qa_q2_entity *spot = qa_q2_select_spawn(g, &state, spawn_point);
  qa_q2_base_player_state_destroy(&state);
  return spot;
}

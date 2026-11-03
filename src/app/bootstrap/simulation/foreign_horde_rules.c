/* Port of src/app/bootstrap/simulation/foreign-horde-rules.ts (HordeRules): moved out of
 * shared_simulation_worlds.c as TS moved it out of runtime.ts (no behavior change). The rules run over the
 * simulation, which is the TS ForeignMatchHost; the shared helpers are in foreign_match_host.c
 * (foreign_match_host.h). GPL-2.0-or-later. */
#include "shared_simulation_internal.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "qa/app/bootstrap/network/types.h"
#include "qa/app/bootstrap/simulation/provider_frames.h"
#include "qa/app/bootstrap/simulation/runtime.h"
#include "qa/app/bootstrap/simulation/monster_sources.h"
#include "qa/app/bootstrap/simulation/monster_placement.h"
#include "qa/app/bootstrap/simulation/q3_commands.h"
#include "qa/content/catalog/monsters.h"
#include "qa/content/q1/base/monsters.h"
#include "qa/content/q2/rerelease/players.h"
#include "qa/content/q1/foundation/runtime.h"
#include "qa/content/composition/q1_hipnotic_supply.h"
#include "qa/content/composition/q1_q2_supply.h"
#include "qa/content/composition/q1_q3_supply.h"
#include "qa/content/monsters/definitions.h"
#include "qa/content/q1/base/creatures.h"
#include "qa/content/q1/base/species.h"
#include "qa/content/q1/foundation/pickups.h"
#include "qa/app/bootstrap/simulation/native_mod_host.h"
#include "qa/bots/navigation/rerelease_path.h"
#include "qa/content/catalog/timing.h"
#include "qa/formats/q1_map/entities.h"
#include "qa/network/common/js_number.h"
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/base/shared/definitions.h"
#include "qa/formats/q3_map/entities.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"
#include "qa/movement/q3/constants.h"
#include "qa/world/actors/body.h"
#include "qa/content/q3/base/combat_bridge.h"
#include "qa/content/composition/q2_q3_items.h"
#include "foreign_match_host.h"

/* ---- Q3 horde (5873-5920) ---- */
static void wl_request_horde_bot(qa_app_shared_simulation *sim, double player_count) {
  if (player_count == sim->horde_admission_players && qa_fm_time(sim) - sim->horde_admission_at < 1) return;
  if (qa_app_sim_bot_services_try_add_bot(&sim->bot_services)) {
    sim->horde_admission_at = qa_fm_time(sim);
    sim->horde_admission_players = player_count;
  }
}
void qa_app_shared_sim_request_horde_bot(qa_app_shared_simulation *sim, double player_count) { wl_request_horde_bot(sim, player_count); }
static double wl_q3_native_health(qa_app_shared_simulation *sim, qa_actor_id actor) {
  qa_q3_game_entity *native = qa_fm_q3_native(sim, actor);
  return native ? qa_q3_game_entity_health(native) : qa_fm_health(sim, actor);
}
void qa_app_shared_sim_tick_horde_wave(qa_app_shared_simulation *sim) {
  if (!qa_ss_is(sim->recipe.match.provider, "q3:horde") || (sim->strategy->as_q3(sim) == NULL)) return;
  size_t player_count; qa_actor_id *players = qa_fm_players(sim, &player_count);
  qa_actor_id *bots = qa_fm_alloc(player_count, sizeof *bots); size_t bot_count = 0;
  for (size_t i = 0; i < player_count; i++) if (qa_app_sim_bot_services_is_bot(&sim->bot_services, players[i])) bots[bot_count++] = players[i];
  bool humans_alive = false;
  for (size_t i = 0; i < player_count && !humans_alive; i++)
    humans_alive = !qa_app_sim_bot_services_is_bot(&sim->bot_services, players[i]) && wl_q3_native_health(sim, players[i]) > 0;
  if (!humans_alive) goto done;
  if (sim->horde_wave == 0) {
    if (qa_fm_time(sim) >= sim->horde_next_wave_at) {
      sim->horde_wave = sim->horde_pending_wave;
      qa_ss_map_clear(&sim->horde_fragged);
      wl_request_horde_bot(sim, (double)player_count);
    }
    goto done;
  }
  /* Register wave bots with a death baseline (PERS_KILLED). */
  for (size_t i = 0; i < bot_count; i++) {
    qa_q3_game_entity *native = qa_fm_q3_native(sim, bots[i]);
    double deaths = native && native->client ? qa_q3_base_player_state_slots_get(&native->client->ps.persistant, QA_Q3_BASE_PERS_KILLED) : 0;
    qa_ss_horde_frag *entry = qa_ss_map_get(&sim->horde_fragged, &bots[i]);
    if (!entry) { qa_ss_horde_frag fresh = {deaths, false}; qa_ss_map_set(&sim->horde_fragged, &bots[i], &fresh); }
    else if (deaths > entry->deaths) entry->fragged = true;
  }
  size_t admitted_count = 0;
  qa_actor_id *admitted = qa_fm_alloc(sim->horde_fragged.count, sizeof *admitted);
  for (size_t i = 0; i < sim->horde_fragged.count; i++) {
    qa_actor_id actor = qa_ss_map_key_at(&sim->horde_fragged, i)->actor;
    if (qa_session_actor_registry_is_live(sim->actors, actor)) admitted[admitted_count++] = actor;
  }
  double humans = (double)(player_count - bot_count);
  double target = qa_js_min(qa_js_min(1 + sim->horde_wave, 8), qa_js_max(1, sim->options.max_clients - humans));
  if ((double)admitted_count < target) { wl_request_horde_bot(sim, (double)player_count); free(admitted); goto done; }
  for (size_t i = 0; i < admitted_count; i++) {
    qa_ss_horde_frag *entry = qa_ss_map_get(&sim->horde_fragged, &admitted[i]);
    if (!entry || !entry->fragged) { free(admitted); goto done; }
  }
  /* Wave cleared: retire survivors through the bot transport and schedule the next. */
  for (size_t i = 0; i < admitted_count; i++)
    if (!qa_app_sim_bot_services_try_disconnect_bot(&sim->bot_services, admitted[i])) qa_app_shared_sim_disconnect_player(sim, admitted[i]);
  free(admitted);
  qa_ss_map_clear(&sim->horde_fragged);
  sim->horde_pending_wave = sim->horde_wave >= 15 ? 1 : sim->horde_wave + 1;
  sim->horde_wave = 0;
  sim->horde_next_wave_at = qa_fm_time(sim) + 5;
done:
  free(bots); free(players);
}

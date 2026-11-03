/* Port of src/content/q1/missionpacks/player.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_PLAYER_H
#define QA_CONTENT_Q1_MISSIONPACKS_PLAYER_H
#include <stdint.h>

#include "qa/content/q1/missionpacks/types.h"

/* TS MissionPackPlayers. The struct must not move after init: the game
 * borrows source_effects/extension/pickup_rules and the callbacks receive
 * the struct as ctx. pickup_rules is the storage registered by
 * qa_q1mp_register_mission_pack_pickup_rules (TS creates that object in
 * pickup-rules.ts). states is the TS Map<ActorId, Q1Actor>. */
typedef struct qa_q1mp_player_state_entry { qa_actor_id actor; qa_q1_actor *state; } qa_q1mp_player_state_entry;
typedef struct qa_q1mp_mission_pack_players {
  qa_q1_services *game;
  qa_q1mp_pack pack;
  qa_q1_damage_source_effects source_effects;
  qa_q1_player_extension extension;
  qa_q1_pickup_rules pickup_rules;
  qa_q1mp_player_state_entry *states; size_t state_count, state_capacity;
  int64_t release_subscription;
} qa_q1mp_mission_pack_players;
void qa_q1mp_mission_pack_players_init(qa_q1mp_mission_pack_players *players, qa_q1_services *game, qa_q1mp_pack pack);
/* C ownership: unsubscribes host.actors.onRelease and frees the state map. */
void qa_q1mp_mission_pack_players_destroy(qa_q1mp_mission_pack_players *players);
void qa_q1mp_players_attach(qa_q1mp_mission_pack_players *players, qa_q1_player_state *player);
void qa_q1mp_players_enable_combos(qa_q1mp_mission_pack_players *players, qa_q1_player_state *player);
void qa_q1mp_players_powerup(qa_q1mp_mission_pack_players *players, qa_q1_player_state *player, qa_q1_powerup powerup, double seconds);
double qa_q1mp_players_gravity_scale(qa_q1mp_mission_pack_players *players, qa_actor_id player);
bool qa_q1mp_players_sphere(qa_q1mp_mission_pack_players *players, qa_q1_actor *item, qa_q1_player_state *player);

#endif

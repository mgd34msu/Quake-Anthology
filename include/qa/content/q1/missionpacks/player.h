/* Port of src/content/q1/missionpacks/player.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_PLAYER_H
#define QA_CONTENT_Q1_MISSIONPACKS_PLAYER_H
#include "qa/content/q1/missionpacks/types.h"

/* TS MissionPackPlayers. source_effects is the opaque
 * Q1DamageSourceEffects hook handed to register_damage_source_effects. */
typedef struct qa_q1mp_mission_pack_players {
  qa_q1mp_game *game; qa_q1mp_pack pack; void *source_effects;
} qa_q1mp_mission_pack_players;
void qa_q1mp_mission_pack_players_init(qa_q1mp_mission_pack_players *players, qa_q1mp_game *game, qa_q1mp_pack pack);
void qa_q1mp_players_attach(qa_q1mp_mission_pack_players *players, qa_q1mp_player_state *player);
void qa_q1mp_players_enable_combos(qa_q1mp_mission_pack_players *players, qa_q1mp_player_state *player);
void qa_q1mp_players_powerup(qa_q1mp_mission_pack_players *players, qa_q1mp_player_state *player, const char *powerup, double seconds);
double qa_q1mp_players_gravity_scale(qa_q1mp_mission_pack_players *players, qa_actor_id actor);
bool qa_q1mp_players_sphere(qa_q1mp_mission_pack_players *players, qa_q1mp_actor *item, qa_q1mp_player_state *player);

#endif

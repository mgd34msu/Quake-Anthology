/*
 * Port of src/content/q2/base/player/index.ts. Source player phases
 * are invoked by the common simulation. This is not a second
 * G_RunFrame. GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q2_BASE_PLAYER_INDEX_H
#define QA_CONTENT_Q2_BASE_PLAYER_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/base/player/checkpoint.h"
#include "qa/content/q2/base/player/types.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/host.h"

/** TS Q2PlayerAdmission. carry NULL = TS undefined;
 * has_use_q2_weapons / has_use_q2_inventory false = TS undefined. */
typedef struct qa_q2_player_admission {
  double slot;
  const char *userinfo;
  bool initialize_inventory;
  bool has_use_q2_weapons; bool use_q2_weapons;
  bool has_use_q2_inventory; bool use_q2_inventory;
  const qa_q2_base_player_carry *carry;
} qa_q2_player_admission;

/** TS Q2ConnectionResult. userinfo is written to the caller's
 * buffer; reason is "" when allowed. */
typedef struct qa_q2_connection_result {
  bool allowed;
  char userinfo[1024];
  char reason[128];
} qa_q2_connection_result;

/** TS Q2Intermission union on kind. */
typedef struct qa_q2_intermission {
  const char *kind; /* "playing"|"intermission" */
  const char *map; double started; bool exit;
  bool has_landmark; qa_q2_landmark_carry landmark;
} qa_q2_intermission;

/** TS q2Userinfo lookup: value for key or NULL when absent
 * (first occurrence wins, as in the TS Map build). */
const char *qa_q2_userinfo_get(const char *source, const char *key, char *scratch, size_t scratch_size);

typedef struct qa_q2_players qa_q2_players;
/* TS Q2Players constructor. NULL only on OOM. map_list in rules is
 * borrowed; a shuffle replaces it with an owned copy (freed on
 * destroy). */
qa_q2_players *qa_q2_players_create(qa_q2_item_module *items, qa_q2_base_weapons weapons,
                                    qa_q2_player_hooks hooks, const qa_q2_base_player_rules *rules, uint32_t rules_mask);
void qa_q2_players_destroy(qa_q2_players *players);
qa_q2_item_module *qa_q2_players_items(qa_q2_players *players);
qa_q2_base_weapons *qa_q2_players_weapons(qa_q2_players *players);
qa_q2_player_hooks *qa_q2_players_hooks(qa_q2_players *players);
qa_q2_base_player_rules *qa_q2_players_rules(qa_q2_players *players);
qa_q2_intermission qa_q2_players_intermission(qa_q2_players *players);
qa_q2_spawn_module qa_q2_players_as_spawn(qa_q2_players *players);
bool qa_q2_players_spawn(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
/* Capture's players array and nested arrays are malloc'd; release
 * with qa_q2_base_players_checkpoint_clear. */
qa_q2_base_players_checkpoint qa_q2_players_capture(qa_q2_players *players);
void qa_q2_base_players_checkpoint_clear(qa_q2_base_players_checkpoint *checkpoint);
void qa_q2_players_restore(qa_q2_players *players, qa_q2_game_services *game, qa_q2_base_players_checkpoint checkpoint);
qa_q2_callback_definitions qa_q2_players_callbacks(qa_q2_players *players);
/* TS context() (aborts when the player has not been admitted). */
qa_q2_player_context qa_q2_players_context(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
/* TS states map access. */
qa_q2_base_player_state *qa_q2_players_get_state(qa_q2_players *players, qa_actor_id actor);
size_t qa_q2_players_state_count(qa_q2_players *players);
bool qa_q2_players_state_at(qa_q2_players *players, size_t index, qa_actor_id *out_actor, qa_q2_base_player_state **out_state);
/* The TS player-context closures. */
qa_q2_player_powerups qa_q2_player_context_powerups(qa_q2_players *players, qa_q2_entity *entity);
bool qa_q2_player_context_weapon_state(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_character_weapon *out);
void qa_q2_player_context_environment_damage(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                             double amount, double means, double flags);

qa_q2_connection_result qa_q2_players_connect(qa_q2_players *players, qa_q2_game_services *game, const char *userinfo);
qa_q2_base_player_state *qa_q2_players_attach(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_player_admission admission);
void qa_q2_players_userinfo_changed(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, const char *source);
/* The returned carry owns its inventory copy (clear it). */
qa_q2_base_player_carry qa_q2_players_save_carry(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_restore_carry(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_base_player_carry carry);
void qa_q2_players_consumed_key(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
/* TS spawnPlacement (protected in TS, exposed for subclasses). */
typedef struct qa_q2_spawn_placement { qa_vec3 origin, angles, velocity; bool from_landmark; } qa_q2_spawn_placement;
qa_q2_spawn_placement qa_q2_players_spawn_placement(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                                    const qa_q2_landmark_carry *landmark);
bool qa_q2_players_kill_box_placement(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
qa_q2_spawn_placement qa_q2_players_select_teleport_spawn(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_put_in_server(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                 bool restore_loadout, const qa_q2_landmark_carry *landmark);
void qa_q2_players_respawn(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_copy_to_body_queue(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_death(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction);
/* TS protected extension points, exposed for subclasses. */
void qa_q2_players_obituary(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction);
void qa_q2_players_apply_score(qa_q2_players *players, qa_q2_entity *victim, qa_q2_game_services *game,
                               qa_death_reaction reaction, qa_q2_base_player_state *recipient, double change);
void qa_q2_players_clear_death_inventory(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
bool qa_q2_players_can_drop_coop_stay_items(qa_q2_players *players, qa_q2_game_services *game);
bool qa_q2_players_record_death(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction);
/* Call after the common movement commit, trigger callbacks, and Q2
 * movement contacts. */
void qa_q2_players_after_client_think(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_begin_frame(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_dead_frame(qa_q2_players *players, qa_q2_player_context *context);
void qa_q2_players_end_frame(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_record_damage(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_damage_decision decision);
void qa_q2_players_weapon_event(qa_q2_players *players, qa_q2w_event event);
void qa_q2_players_teleport_player(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 origin, qa_vec3 angles);
void qa_q2_players_disconnect(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_scoreboard(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, bool reliable);
/* direction: 1 | -1. */
void qa_q2_players_chase(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, double direction, bool toggle);
void qa_q2_players_update_chase(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_begin_intermission(qa_q2_players *players, qa_q2_game_services *game, const char *map, const qa_q2_landmark_carry *landmark);
void qa_q2_players_check_rules(qa_q2_players *players, qa_q2_game_services *game);
void qa_q2_players_before_exit_level(qa_q2_players *players, qa_q2_game_services *game, const char *map);
void qa_q2_players_end_deathmatch_level(qa_q2_players *players, qa_q2_game_services *game);
double qa_q2_players_need_password(qa_q2_players *players);
bool qa_q2_players_chat_allowed(qa_q2_players *players, qa_actor_id actor, qa_q2_game_services *game);
bool qa_q2_players_client_command(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                  const char *command, const char *const *args, size_t arg_count);

#endif

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
#include "qa/content/q2/base/player/view.h"
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
  char userinfo[4096];
  char reason[256];
} qa_q2_connection_result;

/** TS Q2Intermission union on kind. */
typedef struct qa_q2_intermission {
  const char *kind; /* "playing"|"intermission" */
  const char *map; double started; bool exit;
  bool has_landmark; qa_q2_landmark_carry landmark;
  /* TS key order only: Q2RereleasePlayers builds { kind, map, landmark,
   * started, exit }, every other site { kind, map, started, exit,
   * landmark }; the checkpoint encoder writes keys in that order. */
  bool landmark_first;
} qa_q2_intermission;

/** TS q2Userinfo lookup: value for key or NULL when absent
 * (first occurrence wins, as in the TS Map build). */
const char *qa_q2_userinfo_get(const char *source, const char *key, char *scratch, size_t scratch_size);

typedef struct qa_q2_players qa_q2_players;

/* TS spawnPlacement result. */
typedef struct qa_q2_spawn_placement { qa_vec3 origin, angles, velocity; bool from_landmark; } qa_q2_spawn_placement;

/**
 * The overridable (TS non-private) Q2Players methods. A subclass such as
 * TS Q2RereleasePlayers supplies a table with its overrides; a NULL entry
 * inherits the Q2Players body. Every qa_q2_players_<method> call, including
 * the calls Q2Players makes on itself, dispatches through this table with
 * the subclass's self; qa_q2_players_super_<method> is TS super.<method>().
 */
typedef struct qa_q2_players_vtable {
  bool (*spawn)(void *self, qa_q2_entity *entity, qa_q2_game_services *game);
  qa_q2_callback_definitions (*callbacks)(void *self);
  qa_q2_connection_result (*connect)(void *self, qa_q2_game_services *game, const char *userinfo, bool is_bot);
  qa_q2_base_player_state *(*attach)(void *self, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_player_admission admission);
  void (*userinfo_changed)(void *self, qa_q2_entity *entity, qa_q2_game_services *game, const char *source);
  qa_q2_base_player_carry (*save_carry)(void *self, qa_q2_entity *entity, qa_q2_game_services *game);
  void (*restore_carry)(void *self, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_base_player_carry carry);
  qa_q2_spawn_placement (*spawn_placement)(void *self, qa_q2_entity *entity, qa_q2_game_services *game,
                                           const qa_q2_landmark_carry *landmark);
  bool (*kill_box)(void *self, qa_q2_entity *entity, qa_q2_game_services *game);
  void (*put_in_server)(void *self, qa_q2_entity *entity, qa_q2_game_services *game, bool restore_loadout,
                        const qa_q2_landmark_carry *landmark);
  void (*respawn)(void *self, qa_q2_entity *entity, qa_q2_game_services *game);
  void (*death)(void *self, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction);
  void (*obituary)(void *self, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction);
  void (*clear_death_inventory)(void *self, qa_q2_entity *entity, qa_q2_game_services *game);
  bool (*can_drop_coop_stay_items)(void *self, qa_q2_game_services *game);
  bool (*record_death)(void *self, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction);
  void (*after_client_think)(void *self, qa_q2_entity *entity, qa_q2_game_services *game);
  void (*begin_frame)(void *self, qa_q2_entity *entity, qa_q2_game_services *game);
  void (*dead_frame)(void *self, qa_q2_player_context *context);
  void (*world_effects)(void *self, qa_q2_player_context *context);
  void (*falling_damage)(void *self, qa_q2_player_context *context);
  qa_q2_damage_feedback_result (*damage_feedback)(void *self, qa_q2_player_context *context, double pain_index);
  qa_q2_base_player_view (*build_view)(void *self, qa_q2_player_context *context, double flashes, bool intermission);
  void (*update_bob)(void *self, qa_q2_player_context *context);
  void (*client_animation)(void *self, qa_q2_player_context *context);
  void (*end_frame)(void *self, qa_q2_entity *entity, qa_q2_game_services *game);
  void (*record_damage)(void *self, qa_q2_entity *entity, qa_q2_game_services *game, qa_damage_decision decision);
  void (*begin_intermission)(void *self, qa_q2_game_services *game, const char *map, const qa_q2_landmark_carry *landmark);
  void (*check_rules)(void *self, qa_q2_game_services *game);
  void (*before_exit_level)(void *self, qa_q2_game_services *game, const char *map);
  bool (*client_command)(void *self, qa_q2_entity *entity, qa_q2_game_services *game,
                         const char *command, const char *const *args, size_t arg_count);
} qa_q2_players_vtable;

/* TS Q2Players constructor. NULL only on OOM. weapons is the TS Q2Weapons
 * (borrowed). map_list in rules is borrowed; a shuffle replaces it with an
 * owned copy (freed on destroy). */
qa_q2_players *qa_q2_players_create(qa_q2_item_module *items, qa_q2w_weapons *weapons,
                                    qa_q2_player_hooks hooks, const qa_q2_base_player_rules *rules, uint32_t rules_mask);
/* The same constructor for a subclass: vtable (borrowed, outlives the
 * object) holds its overrides and self is passed to each of them. */
qa_q2_players *qa_q2_players_create_derived(qa_q2_item_module *items, qa_q2w_weapons *weapons,
                                            qa_q2_player_hooks hooks, const qa_q2_base_player_rules *rules,
                                            uint32_t rules_mask, const qa_q2_players_vtable *vtable, void *self);
void qa_q2_players_destroy(qa_q2_players *players);
qa_q2_item_module *qa_q2_players_items(qa_q2_players *players);
qa_q2w_weapons *qa_q2_players_weapons(qa_q2_players *players);
qa_q2_player_hooks *qa_q2_players_hooks(qa_q2_players *players);
qa_q2_base_player_rules *qa_q2_players_rules(qa_q2_players *players);
qa_q2_intermission qa_q2_players_intermission(qa_q2_players *players);
/* TS `players.intermission = value`. The map string is copied. */
void qa_q2_players_set_intermission(qa_q2_players *players, qa_q2_intermission intermission);
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
/* TS states.set(actor, state): takes ownership of a malloc'd, initialized
 * state (replacing and freeing any previous one in place). */
void qa_q2_players_set_state(qa_q2_players *players, qa_actor_id actor, qa_q2_base_player_state *state);
/* TS weapons.states.get(actor): NULL when unbound (or when no Q2Weapons
 * was supplied, as unit hosts without an arsenal do). */
qa_q2w_state *qa_q2_players_weapon_state(qa_q2_players *players, qa_actor_id actor);
/* The TS player-context closures. */
qa_q2_player_powerups qa_q2_player_context_powerups(qa_q2_players *players, qa_q2_entity *entity);
bool qa_q2_player_context_weapon_state(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_character_weapon *out);
void qa_q2_player_context_environment_damage(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                             double amount, double means, double flags);
/* TS passes a Q2PlayerContext where a Q2CharacterContext is expected; this
 * builds that view. adapter backs the closures and must outlive the use. */
typedef struct qa_q2_player_character_adapter {
  qa_q2_players *players;
  qa_q2_entity *entity;
  qa_q2_game_services *game;
} qa_q2_player_character_adapter;
qa_q2_character_context qa_q2_player_context_as_character(const qa_q2_player_context *context,
                                                          qa_q2_player_character_adapter *adapter);

/* TS connect(game, userinfo): dispatches with is_bot false. */
qa_q2_connection_result qa_q2_players_connect(qa_q2_players *players, qa_q2_game_services *game, const char *userinfo);
qa_q2_base_player_state *qa_q2_players_attach(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_player_admission admission);
void qa_q2_players_userinfo_changed(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, const char *source);
/* The returned carry owns its inventory copy (clear it). */
qa_q2_base_player_carry qa_q2_players_save_carry(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_restore_carry(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_base_player_carry carry);
void qa_q2_players_consumed_key(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
/* TS spawnPlacement (protected in TS, exposed for subclasses). */
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
/* TS chase(entity, game, direction, toggle, from): from NULL is TS null. */
void qa_q2_players_chase_from(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, double direction, bool toggle,
                              const qa_actor_id *from);
void qa_q2_players_update_chase(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_begin_intermission(qa_q2_players *players, qa_q2_game_services *game, const char *map, const qa_q2_landmark_carry *landmark);
void qa_q2_players_check_rules(qa_q2_players *players, qa_q2_game_services *game);
void qa_q2_players_before_exit_level(qa_q2_players *players, qa_q2_game_services *game, const char *map);
void qa_q2_players_end_deathmatch_level(qa_q2_players *players, qa_q2_game_services *game);
double qa_q2_players_need_password(qa_q2_players *players);
bool qa_q2_players_chat_allowed(qa_q2_players *players, qa_actor_id actor, qa_q2_game_services *game);
bool qa_q2_players_client_command(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                  const char *command, const char *const *args, size_t arg_count);
/* TS protected view/environment extension points (dispatching). */
void qa_q2_players_world_effects(qa_q2_players *players, qa_q2_player_context *context);
void qa_q2_players_falling_damage(qa_q2_players *players, qa_q2_player_context *context);
qa_q2_damage_feedback_result qa_q2_players_damage_feedback(qa_q2_players *players, qa_q2_player_context *context, double pain_index);
qa_q2_base_player_view qa_q2_players_build_view(qa_q2_players *players, qa_q2_player_context *context, double flashes, bool intermission);
void qa_q2_players_update_bob(qa_q2_players *players, qa_q2_player_context *context);
void qa_q2_players_client_animation(qa_q2_players *players, qa_q2_player_context *context);

/* TS super.<method>(): the Q2Players bodies, for subclass overrides. */
bool qa_q2_players_super_spawn(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
qa_q2_callback_definitions qa_q2_players_super_callbacks(qa_q2_players *players);
qa_q2_connection_result qa_q2_players_super_connect(qa_q2_players *players, qa_q2_game_services *game, const char *userinfo);
qa_q2_base_player_state *qa_q2_players_super_attach(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_player_admission admission);
void qa_q2_players_super_userinfo_changed(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, const char *source);
qa_q2_base_player_carry qa_q2_players_super_save_carry(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_super_restore_carry(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_q2_base_player_carry carry);
qa_q2_spawn_placement qa_q2_players_super_spawn_placement(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                                          const qa_q2_landmark_carry *landmark);
bool qa_q2_players_super_kill_box(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_super_put_in_server(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                       bool restore_loadout, const qa_q2_landmark_carry *landmark);
void qa_q2_players_super_respawn(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_super_death(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction);
void qa_q2_players_super_obituary(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction);
void qa_q2_players_super_clear_death_inventory(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
bool qa_q2_players_super_can_drop_coop_stay_items(qa_q2_players *players, qa_q2_game_services *game);
bool qa_q2_players_super_record_death(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_death_reaction reaction);
void qa_q2_players_super_after_client_think(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_super_begin_frame(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_super_dead_frame(qa_q2_players *players, qa_q2_player_context *context);
void qa_q2_players_super_world_effects(qa_q2_players *players, qa_q2_player_context *context);
void qa_q2_players_super_falling_damage(qa_q2_players *players, qa_q2_player_context *context);
qa_q2_damage_feedback_result qa_q2_players_super_damage_feedback(qa_q2_players *players, qa_q2_player_context *context, double pain_index);
qa_q2_base_player_view qa_q2_players_super_build_view(qa_q2_players *players, qa_q2_player_context *context, double flashes, bool intermission);
void qa_q2_players_super_update_bob(qa_q2_players *players, qa_q2_player_context *context);
void qa_q2_players_super_client_animation(qa_q2_players *players, qa_q2_player_context *context);
void qa_q2_players_super_end_frame(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game);
void qa_q2_players_super_record_damage(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game, qa_damage_decision decision);
void qa_q2_players_super_begin_intermission(qa_q2_players *players, qa_q2_game_services *game, const char *map, const qa_q2_landmark_carry *landmark);
void qa_q2_players_super_check_rules(qa_q2_players *players, qa_q2_game_services *game);
void qa_q2_players_super_before_exit_level(qa_q2_players *players, qa_q2_game_services *game, const char *map);
bool qa_q2_players_super_client_command(qa_q2_players *players, qa_q2_entity *entity, qa_q2_game_services *game,
                                        const char *command, const char *const *args, size_t arg_count);

#endif

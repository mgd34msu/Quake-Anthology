/*
 * Port of src/content/q2/rerelease/players.ts. GPL-2.0-or-later.
 * TS `Q2RereleasePlayers extends Q2Players`: `base` is that Q2Players
 * object (base/player/index.h), created with this object's overrides in
 * its vtable, so every qa_q2_players_<method>(base, ...) call dispatches
 * to the rerelease bodies exactly as TS virtual calls do. Player state,
 * hooks, rules, carry, context and view are the base types; the
 * rerelease-only per-player data is the TS `rereleaseStates` map of
 * qa_q2rr_player_extra_state (rerelease/types.h). The object must stay at
 * the address passed to _init (base holds it as its subclass self).
 */
#ifndef QA_CONTENT_Q2_RERELEASE_PLAYERS_H
#define QA_CONTENT_Q2_RERELEASE_PLAYERS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/base/player/index.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/rerelease/checkpoint.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/world.h"

/** TS Q2RereleasePlayerExtension (fn+ctx). */
typedef struct qa_q2_rerelease_player_extension {
  void *ctx;
  void (*admitted)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*end_player_frame)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*spawned)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*before_level_change)(void *ctx, qa_q2_game_services *g);
  void (*end_of_unit)(void *ctx, qa_q2_game_services *g);
  void (*leave_unit)(void *ctx);
  void (*begin_player_frame)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*help)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
} qa_q2_rerelease_player_extension;

typedef struct qa_q2rr_extra_pair { qa_actor_id actor; qa_q2rr_player_extra_state *state; } qa_q2rr_extra_pair;
/* TS squadSpawns / selectedSpawns map values keyed by actor. */
typedef struct qa_q2rr_placement { qa_actor_id actor; qa_vec3 origin, angles, velocity; bool from_landmark; } qa_q2rr_placement;
struct qa_q2rr_release_record;

/** TS Q2RereleasePlayers. Owns the extra states and the base object. */
typedef struct qa_q2_rerelease_players {
  qa_q2_players *base;
  qa_q2_rerelease_hooks rerelease_hooks;
  qa_q2rr_extra_pair *rerelease_states; size_t extra_count, extra_cap;
  qa_q2_rerelease_options rerelease_options;
  double coop_restart_time;
  bool deadly_kill_box;
  double intermission_flags;
  bool has_intermission_fade_until; double intermission_fade_until;
  bool has_intermission_camera; qa_vec3 intermission_camera_origin, intermission_camera_angles;
  bool intermission_camera_set;
  bool has_extension; qa_q2_rerelease_player_extension extension;
  qa_q2rr_placement *squad_spawns; size_t squad_count, squad_cap;
  qa_q2rr_placement *selected_spawns; size_t selected_count, selected_cap;
  struct qa_q2rr_release_record **releases; size_t release_count, release_cap;
  /* Storage behind callbacks(): {...super.callbacks, ...spawns.callbacks}. */
  qa_q2_callback_definitions merged_callbacks;
} qa_q2_rerelease_players;

/* Extra-state map helpers (TS rereleaseStates get/set/delete). set takes
 * ownership of a malloc'd state; delete destroys and frees it. */
qa_q2rr_player_extra_state *qa_q2rr_players_get_extra_or_null(qa_q2_rerelease_players *p, qa_actor_id a);
bool qa_q2rr_players_set_extra(qa_q2_rerelease_players *p, qa_actor_id a, qa_q2rr_player_extra_state *s);
void qa_q2rr_players_delete_extra(qa_q2_rerelease_players *p, qa_actor_id a);

/* TS constructor(items, weapons, hooks, rereleaseHooks, options = {},
 * rules = {}). weapons is the TS Q2Weapons (borrowed). options NULL and
 * rules NULL / mask 0 are TS {}. False only on OOM. */
bool qa_q2_rerelease_players_init(qa_q2_rerelease_players *p, qa_q2_item_module *items, qa_q2w_weapons *weapons,
                                  qa_q2_player_hooks hooks, qa_q2_rerelease_hooks rr_hooks,
                                  const qa_q2_rerelease_options_changes *options,
                                  const qa_q2_base_player_rules *rules, uint32_t rules_mask);
void qa_q2_rerelease_players_destroy(qa_q2_rerelease_players *p);
/* TS extra(): aborts when not admitted. */
qa_q2rr_player_extra_state *qa_q2_rerelease_players_get_extra(qa_q2_rerelease_players *p, qa_actor_id a);
/* TS connect(game, userinfo, isBot) (the override's extra parameter). */
qa_q2_connection_result qa_q2_rerelease_players_connect(qa_q2_rerelease_players *p, qa_q2_game_services *g,
                                                        const char *userinfo, bool is_bot);
void qa_q2_rerelease_players_weapon_picked(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_item_id item, bool first);
void qa_q2_rerelease_players_record_weapon_fire(qa_q2_rerelease_players *p, qa_actor_id a, double now);
/* TS cloakAlpha: false is TS null; otherwise *out is the alpha. */
bool qa_q2_rerelease_players_cloak_alpha(qa_q2_rerelease_players *p, qa_actor_id a, double now, double *out);
void qa_q2_rerelease_players_reveal_invisibility(qa_q2_rerelease_players *p, qa_actor_id a, double until);
void qa_q2_rerelease_players_movement_impact(qa_q2_rerelease_players *p, qa_actor_id a, double impact_delta, bool on_ladder);
void qa_q2_rerelease_players_begin_rerelease_intermission(qa_q2_rerelease_players *p, qa_q2_game_services *g, const char *map,
                                                          const qa_q2_landmark_carry *lm, double flags);
void qa_q2_rerelease_players_fade_frame(qa_q2_rerelease_players *p, qa_q2_game_services *g);
void qa_q2_rerelease_players_move_to_camera(qa_q2_rerelease_players *p, qa_q2_game_services *g, qa_vec3 origin, qa_vec3 angles, bool entering);
void qa_q2_rerelease_players_finish_camera(qa_q2_rerelease_players *p, qa_q2_game_services *g);
void qa_q2_rerelease_players_emit_flashlight(qa_q2_rerelease_players *p, qa_actor_id a, qa_q2_game_services *g);
bool qa_q2_rerelease_players_find_respawn_spot(qa_q2_rerelease_players *p, qa_q2_entity *player, qa_q2_game_services *g, qa_vec3 *out);
/* TS captureRerelease / restoreRerelease. The captured checkpoint owns its
 * arrays and each player's help_points copy; release it with
 * qa_q2_rerelease_players_checkpoint_clear. Restore deep-copies its input. */
qa_q2_rerelease_players_checkpoint qa_q2_rerelease_players_capture(const qa_q2_rerelease_players *p);
void qa_q2_rerelease_players_checkpoint_clear(qa_q2_rerelease_players_checkpoint *checkpoint);
void qa_q2_rerelease_players_restore(qa_q2_rerelease_players *p, qa_q2_game_services *g,
                                     const qa_q2_rerelease_players_checkpoint *checkpoint);

#endif

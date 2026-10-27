/*
 * Port of src/content/composition/q2/match.ts: Q2ProductMatch dispatch
 * over the selected source mode (Tag/DeathBall/CTF/LMCTF/Horde).
 * Mirrored surfaces from unported modules (qa_comp_q2_* hooks; TS
 * owners: src/content/q2/foundation/host.ts (Q2Entity/Q2GameServices),
 * q2/base/player (states, spawns), q2/missionpacks/modes,
 * q2/multiplayer/ctf + lmctf + horde). instanceof checks become the
 * stored mode kind. The landed qa_q2_damage_source_effects is reused.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q2_MATCH_H
#define QA_CONTENT_COMPOSITION_Q2_MATCH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/composition/q2/types.h"
#include "qa/contracts/gameplay.h"
#include "qa/world/gameplay/policies.h"

/* Q2Entity surface composition touches. */
typedef struct qa_comp_q2_entity {
  qa_owned_actor actor;
  const char *classname;
  int32_t effects;
} qa_comp_q2_entity;

/* Q2PlayerState surface used here (q2/base/player/types.ts). */
typedef struct qa_comp_q2_player_state {
  double score;
  const char *skin;
  const char *userinfo;
  bool use_q2_weapons;
  bool has_use_q2_inventory;
  bool use_q2_inventory;
} qa_comp_q2_player_state;

/* Q2GameServices surface used by match.ts. */
typedef struct qa_comp_q2_game_services {
  void *ctx;
  const char *edition; /* options.edition */
  double (*deathmatch_flags)(void *ctx);
  void (*remove)(void *ctx, qa_comp_q2_entity *entity);
  void (*show)(void *ctx, qa_comp_q2_entity *entity);
  qa_comp_q2_entity *(*entity)(void *ctx, qa_actor_id actor);
  qa_vec3 (*body_angles)(void *ctx, qa_comp_q2_entity *entity);
  void (*move_velocity)(void *ctx, qa_comp_q2_entity *entity, qa_vec3 velocity); /* game.move */
  void (*diagnostic)(void *ctx, const char *text);                               /* host.diagnostic */
  void (*set_traits_team)(void *ctx, qa_owned_actor actor, const char *team);
  void (*inventory_configure)(void *ctx, qa_owned_actor actor, qa_inventory_entry entry);
  /* q2/base/player/spawns.ts helpers. */
  void (*select_spawn_point)(void *ctx, struct qa_comp_q2_game_services *game, qa_comp_q2_entity *entity,
                             qa_vec3 *out_origin, qa_vec3 *out_angles);
  const qa_comp_q2_entity *const *(*entities_named)(void *ctx, struct qa_comp_q2_game_services *game,
                                                    const char *classname, size_t *out_count);
  double (*players_range)(void *ctx, struct qa_comp_q2_game_services *game, const qa_comp_q2_entity *spot);
  /* Player table access (players().states / userinfoChanged). */
  qa_comp_q2_player_state *(*player_state)(void *ctx, qa_actor_id actor);
  void (*userinfo_changed)(void *ctx, qa_comp_q2_entity *entity, struct qa_comp_q2_game_services *game,
                           const char *userinfo);
  void (*end_deathmatch_level)(void *ctx, struct qa_comp_q2_game_services *game);
} qa_comp_q2_game_services;

/* Q2CtfHooks minus emit (q2/multiplayer/ctf/types.ts), as built by
 * index.ts and forwarded to the CTF/LMCTF constructors. */
typedef struct qa_comp_q2_ctf_hooks {
  void *ctx;
  void *items;   /* Q2ItemModule handle */
  void *weapons; /* Q2Weapons handle */
  qa_comp_q2_player_state *(*player)(void *ctx, qa_actor_id actor);
  void (*set_skin)(void *ctx, qa_actor_id actor, const char *skin);
  void (*spawn_player)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  void (*observer)(void *ctx, qa_comp_q2_entity *entity);
  void (*teleport)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                   qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity);
  void (*chase)(void *ctx, qa_actor_id actor);
  void (*set_grapple_prediction)(void *ctx, qa_actor_id actor, bool suppressed);
  double (*gravity)(void *ctx);
  void (*end_level)(void *ctx, qa_comp_q2_game_services *game, const char *map);
  void (*kick)(void *ctx, qa_actor_id actor);
  void (*set_deathmatch_flags)(void *ctx, double flags);
  bool (*chat_allowed)(void *ctx, qa_actor_id actor, qa_comp_q2_game_services *game);
} qa_comp_q2_ctf_hooks;

/* Mode emit closures match.ts builds for CTF/LMCTF. */
typedef void (*qa_comp_q2_ctf_emit_fn)(void *ctx, const qa_comp_q2_ctf_event *event);
typedef void (*qa_comp_lmctf_emit_fn)(void *ctx, const qa_comp_lmctf_event *event);

/* Tag/DeathBall constructor bindings built in match.c (TS closures). */
typedef struct qa_comp_q2_tag_bindings {
  void *ctx;
  void *items;
  void (*add_score)(void *ctx, qa_actor_id actor, double amount);
  void (*select_spawn)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                       qa_vec3 *out_origin, qa_vec3 *out_angles);
  qa_comp_q2_entity *(*farthest_spawn)(void *ctx, qa_comp_q2_game_services *game);
} qa_comp_q2_tag_bindings;
typedef struct qa_comp_q2_deathball_bindings {
  void *ctx;
  const qa_comp_q2_match_selection *(*settings)(void *ctx);
  void (*add_score)(void *ctx, qa_actor_id actor, double amount);
  void (*select_spawn)(void *ctx, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                       qa_vec3 *out_origin, qa_vec3 *out_angles);
  const char *(*skin)(void *ctx, qa_actor_id actor);
  void (*set_skin)(void *ctx, qa_actor_id actor, const char *skin);
  void (*end_level)(void *ctx);
  double (*spawn_distance)(void *ctx, qa_comp_q2_entity *spot, qa_comp_q2_game_services *game);
} qa_comp_q2_deathball_bindings;

/* CTF per-player state fields read in playerSpawned (team 0|1|2). */
typedef struct qa_comp_q2_mode_hooks {
  void *ctx;
  void *(*create_ctf)(void *ctx, const qa_comp_q2_ctf_hooks *hooks, qa_comp_q2_ctf_emit_fn emit,
                      void *emit_ctx, const qa_shared_grapple_control *grapple);
  void *(*create_lmctf)(void *ctx, const qa_comp_q2_ctf_hooks *hooks, qa_comp_lmctf_emit_fn emit,
                        void *emit_ctx, const void *rules, const qa_comp_lmctf_travel *travel,
                        const qa_shared_grapple_control *grapple);
  void *(*create_tag)(void *ctx, const qa_comp_q2_tag_bindings *bindings);
  void *(*create_deathball)(void *ctx, const qa_comp_q2_deathball_bindings *bindings);
  void *(*create_horde)(void *ctx);
  /* Shared mode module surface (Q2SpawnModule-ish). */
  const void *(*callbacks)(void *ctx, void *source);
  bool (*spawn)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  void (*post_spawn)(void *ctx, void *source, qa_comp_q2_game_services *game);
  void (*ctf_after_spawn)(void *ctx, void *source, qa_comp_q2_game_services *game);
  void (*admitted)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  void (*client_begin)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  /* selectSpawn result shape differs per mode; opaque here. */
  const void *(*select_spawn)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  void (*ctf_score)(void *ctx, void *source, qa_comp_q2_entity *victim, qa_comp_q2_entity *attacker,
                    qa_comp_q2_game_services *game, double change, double means, qa_comp_q2_entity *recipient);
  void (*tag_score)(void *ctx, void *source, qa_comp_q2_entity *recipient, qa_comp_q2_entity *victim,
                    qa_comp_q2_game_services *game, double change, double means);
  void (*death)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  void (*player_death)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  void (*disconnect)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  double (*change_damage)(void *ctx, void *source, qa_actor_id target, const qa_actor_id *attacker, double amount);
  double (*change_knockback)(void *ctx, void *source, qa_actor_id target, double amount, double means,
                             qa_comp_q2_game_services *game);
  bool (*check_rules)(void *ctx, void *source, qa_comp_q2_game_services *game);
  void (*drop_inventory)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  bool (*command)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game,
                  const char *name, const char *const *args, size_t arg_count);
  void (*before_player)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  void (*lmctf_player_spawned)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  /* CTF state reads: team -1 when no state is admitted. */
  int (*ctf_state_team)(void *ctx, void *source, qa_actor_id actor);
  bool (*ctf_grapple_native_enabled)(void *ctx, void *source);
  void (*ctf_assign_skin)(void *ctx, void *source, qa_comp_q2_entity *entity);
  bool (*ctf_pickups_allowed)(void *ctx, void *source);
  qa_comp_q2_weapon_input (*ctf_weapon_input)(void *ctx, void *source, qa_comp_q2_entity *entity,
                                              qa_comp_q2_game_services *game, qa_comp_q2_weapon_input input);
  double (*lmctf_gravity_scale)(void *ctx, void *source, qa_actor_id actor);
  double (*lmctf_ctf_flags)(void *ctx, void *source);
  /* Techs / runes / flags. */
  double (*tech_strength)(void *ctx, void *source, const qa_actor_id *attacker, qa_comp_q2_game_services *game,
                          double damage);
  double (*tech_resistance)(void *ctx, void *source, qa_actor_id target, qa_comp_q2_game_services *game, double take);
  void (*flags_hurt_carrier)(void *ctx, void *source, qa_actor_id target, const qa_actor_id *attacker,
                             qa_comp_q2_game_services *game);
  double (*rune_damage)(void *ctx, void *source, const qa_actor_id *attacker, double damage,
                        qa_comp_q2_game_services *game);
  double (*rune_after_power_armor)(void *ctx, void *source, qa_actor_id target, double take,
                                   qa_comp_q2_game_services *game);
  void (*rune_after_health)(void *ctx, void *source, qa_actor_id target, const qa_actor_id *attacker,
                            double applied, qa_comp_q2_game_services *game);
  void (*ctf_after_player)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  void (*lmctf_player_frame)(void *ctx, void *source, qa_comp_q2_entity *entity, qa_comp_q2_game_services *game);
  int32_t (*tag_effects)(void *ctx, void *source, qa_actor_id actor);
} qa_comp_q2_mode_hooks;

/* TS Q2ProductMatch. players/world are the constructor's lazy accessors. */
typedef struct qa_comp_q2_product_match {
  qa_comp_q2_match_selection selection;
  qa_comp_q2_match_kind source_kind; /* STANDARD == TS null source */
  void *source;
  const qa_comp_q2_mode_hooks *hooks;
  const qa_comp_q2_services *services;
  void *players_ctx;
  qa_comp_q2_player_state *(*player_state)(void *ctx, qa_actor_id actor);
  qa_comp_q2_tag_bindings tag_bindings;
  qa_comp_q2_deathball_bindings deathball_bindings;
  /* Game accessor for the DeathBall setSkin closure (world()). */
  qa_comp_q2_game_services *(*world)(void *ctx);
  void *world_ctx;
} qa_comp_q2_product_match;

/**
 * TS constructor. items is the Q2ItemModule handle for the Tag bindings;
 * player_state/world are the players()/world() accessor closures.
 */
void qa_comp_q2_product_match_init(qa_comp_q2_product_match *match, const qa_comp_q2_match_selection *selection,
                                   qa_comp_q2_player_state *(*player_state)(void *ctx, qa_actor_id actor),
                                   void *players_ctx, void *items,
                                   qa_comp_q2_game_services *(*world)(void *ctx), void *world_ctx,
                                   const qa_comp_q2_ctf_hooks *ctf_hooks,
                                   const qa_comp_q2_services *services, const qa_comp_q2_mode_hooks *hooks);

const void *qa_comp_q2_product_match_callbacks(const qa_comp_q2_product_match *match);
bool qa_comp_q2_product_match_spawn(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                    qa_comp_q2_game_services *game);
void qa_comp_q2_product_match_after_spawn(qa_comp_q2_product_match *match, qa_comp_q2_game_services *game);
void qa_comp_q2_product_match_admitted(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                       qa_comp_q2_game_services *game);
const void *qa_comp_q2_product_match_select_spawn(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                                  qa_comp_q2_game_services *game);
/* attacker NULL defaults to recipient, as the TS default parameter. */
void qa_comp_q2_product_match_score(qa_comp_q2_product_match *match, qa_comp_q2_entity *victim,
                                    qa_comp_q2_game_services *game, double change, double means,
                                    qa_comp_q2_entity *recipient, qa_comp_q2_entity *attacker);
void qa_comp_q2_product_match_death(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                    qa_comp_q2_game_services *game);
void qa_comp_q2_product_match_disconnect(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                         qa_comp_q2_game_services *game);
double qa_comp_q2_product_match_damage(qa_comp_q2_product_match *match, qa_actor_id target,
                                       const qa_actor_id *attacker, double amount);
double qa_comp_q2_product_match_knockback(qa_comp_q2_product_match *match, qa_actor_id target, double amount,
                                          double means, qa_comp_q2_game_services *game);
bool qa_comp_q2_product_match_check_rules(qa_comp_q2_product_match *match, qa_comp_q2_game_services *game);
void qa_comp_q2_product_match_drop_inventory(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                             qa_comp_q2_game_services *game);
bool qa_comp_q2_product_match_command(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                      qa_comp_q2_game_services *game, const char *name,
                                      const char *const *args, size_t arg_count);
void qa_comp_q2_product_match_before_player(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                            qa_comp_q2_game_services *game);
void qa_comp_q2_product_match_player_spawned(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                             qa_comp_q2_game_services *game);
double qa_comp_q2_product_match_gravity_scale(qa_comp_q2_product_match *match, qa_actor_id actor);
/** Fills the landed effects table (ctx = match + game adapter state in
 *  caller storage: effects_game is read at call time like TS's `game`). */
typedef struct qa_comp_q2_match_effects_state {
  qa_comp_q2_product_match *match;
  qa_comp_q2_game_services *game;
} qa_comp_q2_match_effects_state;
qa_q2_damage_source_effects qa_comp_q2_product_match_source_effects(qa_comp_q2_match_effects_state *state);
void qa_comp_q2_product_match_effects(qa_comp_q2_product_match *match, qa_comp_q2_entity *entity,
                                      qa_comp_q2_game_services *game);

#endif

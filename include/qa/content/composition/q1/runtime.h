/*
 * Port of src/content/composition/q1/runtime.ts: registers one official
 * source program on the session's existing world and actors.
 * All calls into unported modules are mirrored as fn+ctx hooks
 * (collision-proof qa_comp_q1_* names). TS owners:
 *  - Q1Foundation/base/missionpacks/addons handles: src/content/q1/ tree
 *  - QcFinaleAcknowledgement: src/compat/qc/presentation-host.ts
 *  - Q1Map: src/formats/q1-map/index.ts
 * Closures the TS constructor builds (campaign forwarding, addon/ctf/
 * horde services, pack services) are implemented in runtime.c and
 * passed to the registration hooks as fn+ctx binding structs.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_COMPOSITION_Q1_RUNTIME_H
#define QA_CONTENT_COMPOSITION_Q1_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/composition/q1/clients.h"
#include "qa/content/composition/q1/commands.h"
#include "qa/content/composition/q1/types.h"
#include "qa/contracts/gameplay.h"

typedef struct qa_comp_q1_map qa_comp_q1_map;
typedef struct qa_comp_q1_pose { bool has_frame; double frame; } qa_comp_q1_pose;

/* Q1ObituaryActor (src/content/q1/base). weapon is a weapon name or NULL. */
typedef struct qa_comp_q1_obituary_actor {
  qa_actor_id actor;
  const char *name;
  const char *classname;
  bool is_player;
  bool is_monster;
  double team;
  double health;
  const char *water_type;
  double water_level;
  const char *weapon;
  double quad_expires;
  double invulnerable_expires;
  bool brush;
  const char *kill_string;
} qa_comp_q1_obituary_actor;

typedef double (*qa_comp_q1_random_fn)(void *ctx);

typedef struct qa_comp_q1_obituary_input {
  const char *edition;
  qa_comp_q1_obituary_actor victim;
  bool has_attacker;
  qa_comp_q1_obituary_actor attacker;
  bool has_telefrag_owner;
  qa_comp_q1_obituary_actor telefrag_owner;
  double teamplay;
  const char *death_type;
} qa_comp_q1_obituary_input;

#define QA_COMP_Q1_OBITUARY_MAX_ARGS 4
typedef struct qa_comp_q1_obituary {
  bool has_message;
  const char *message_text;
  const char *message_args[QA_COMP_Q1_OBITUARY_MAX_ARGS];
  size_t message_arg_count;
  bool has_score;
  qa_actor_id score_actor;
  double score_delta;
  bool has_achievement;
  qa_actor_id achievement_actor;
  const char *achievement_id;
} qa_comp_q1_obituary;

typedef struct qa_comp_q1_notice {
  const char *text;
  const char *args[QA_COMP_Q1_OBITUARY_MAX_ARGS];
  size_t arg_count;
  double score_delta;
} qa_comp_q1_notice;

/* Q1IntermissionResult: kind string ("finale", "sell-screen", ...) plus
 * the opaque finale payload for level-presentation events. */
typedef struct qa_comp_q1_intermission {
  const char *kind;
  const qa_comp_q1_finale *finale;
} qa_comp_q1_intermission;

/* Bindings runtime.c builds for the registration hooks (TS closures). */
typedef struct qa_comp_q1_base_bindings {
  void *ctx;
  qa_comp_q1_campaign_binding campaign;
  bool registered;
  bool official_campaign;
  bool (*same_level)(void *ctx);
  void (*player_exited)(void *ctx, qa_actor_id actor);
  void (*finish_campaign)(void *ctx);
  bool (*finale_finished)(void *ctx);
} qa_comp_q1_base_bindings;

typedef struct qa_comp_q1_addon_bindings {
  void *ctx;
  void (*emit)(void *ctx, const qa_comp_q1_addon_event *event);
  bool (*cheat_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  bool (*is_monster)(void *ctx, qa_actor_id actor);
  double (*cvar)(void *ctx, const char *name);
  void (*set_cvar)(void *ctx, const char *name, const char *value);
} qa_comp_q1_addon_bindings;

typedef struct qa_comp_q1_horde_bindings {
  void *ctx;
  int (*dead_flag)(void *ctx, qa_actor_id actor);
  bool (*no_target)(void *ctx, qa_actor_id actor);
  bool (*is_bot)(void *ctx, qa_actor_id actor);
  void (*add_score)(void *ctx, qa_actor_id actor, double delta);
  void (*respawn_teammate)(void *ctx, qa_actor_id actor);
  void (*restart_session)(void *ctx, const char *map, double flags);
} qa_comp_q1_horde_bindings;

typedef struct qa_comp_q1_pack_bindings {
  void *ctx;
  bool (*cheat_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  double (*gamecfg)(void *ctx);
  double (*team_color)(void *ctx, qa_actor_id actor);
  void (*set_team_color)(void *ctx, qa_actor_id actor, double color);
  void (*add_frags)(void *ctx, qa_actor_id actor, double delta);
  double (*frags)(void *ctx, qa_actor_id actor);
  void (*disconnect)(void *ctx, qa_actor_id actor);
  double (*player_frame)(void *ctx, qa_actor_id actor);
  const char *(*player_name)(void *ctx, qa_actor_id actor);
  bool (*cheats_allowed)(void *ctx);
  void (*developer_message)(void *ctx, const char *text);
  bool (*footsteps)(void *ctx);
} qa_comp_q1_pack_bindings;

typedef struct qa_comp_q1_ctf_bindings {
  void *ctx;
  const char *(*name)(void *ctx, qa_actor_id actor);
  double (*score)(void *ctx, qa_actor_id actor);
  void (*add_score)(void *ctx, qa_actor_id actor, double delta);
  bool (*is_bot)(void *ctx, qa_actor_id actor);
  double (*captures)(void *ctx, const char *team);
  void (*add_capture)(void *ctx, const char *team);
  /* input(): attack/jump from the source player, impulse from the client. */
  void (*input)(void *ctx, qa_actor_id actor, bool *attack, bool *jump, double *impulse,
                bool *grapple_selected, qa_vec3 *view_angles, double *teleport_until, double *frame);
  void (*consume_impulse)(void *ctx, qa_actor_id actor);
  bool (*observer)(void *ctx, qa_actor_id actor);
  void (*set_observer)(void *ctx, qa_actor_id actor, bool enabled);
  void (*respawn)(void *ctx, qa_actor_id actor, qa_comp_q1_actor *spot);
  void (*disconnect)(void *ctx, qa_actor_id actor);
  void (*colors)(void *ctx, qa_actor_id actor, double shirt, double pants);
  bool (*prompt_supported)(void *ctx, qa_actor_id actor);
  void (*prompt)(void *ctx, qa_actor_id actor, const char *title,
                 const qa_comp_q1_prompt_choice *choices, size_t choice_count);
  void (*clear_prompt)(void *ctx, qa_actor_id actor);
  void (*teleport)(void *ctx, qa_actor_id actor, qa_vec3 origin, qa_vec3 angles, qa_vec3 velocity, double until);
  void (*select_grapple)(void *ctx, qa_actor_id actor);
  qa_item_id (*selected_weapon)(void *ctx, qa_actor_id actor);
  qa_item_id (*selected_ammo)(void *ctx, qa_actor_id actor);
  void (*weapon_changed)(void *ctx, qa_actor_id actor, qa_item_id acquired);
  void (*haste)(void *ctx, qa_actor_id actor);
  void (*status)(void *ctx, qa_actor_id actor, const qa_comp_ctf_status *status);
  void (*log)(void *ctx, qa_actor_id actor, const char *action);
} qa_comp_q1_ctf_bindings;

/* State-extension registration (game.registerStateExtension). */
typedef uint8_t *(*qa_comp_q1_capture_fn)(void *ctx, size_t *out_length);
typedef bool (*qa_comp_q1_restore_fn)(void *ctx, const uint8_t *bytes, size_t length);

typedef struct qa_comp_q1_runtime_hooks {
  void *ctx;
  /* Q1Foundation reads. */
  double (*time)(void *ctx);
  const char *(*map_name)(void *ctx);
  double (*health)(void *ctx, qa_actor_id actor);
  qa_comp_q1_actor *(*entity)(void *ctx, qa_actor_id actor);
  double (*entity_movement_flags)(void *ctx, qa_comp_q1_actor *entity);
  const char *(*entity_movement)(void *ctx, qa_comp_q1_actor *entity);
  const char *(*entity_solid)(void *ctx, qa_comp_q1_actor *entity);
  bool (*entity_owner)(void *ctx, qa_comp_q1_actor *entity, qa_actor_id *out);
  const char *(*entity_text)(void *ctx, qa_comp_q1_actor *entity, const char *key);
  double (*entity_number)(void *ctx, qa_comp_q1_actor *entity, const char *key);
  double (*entity_water_level)(void *ctx, qa_comp_q1_actor *entity);
  const char *(*classname)(void *ctx, qa_actor_id actor);
  const qa_actor_id *(*players)(void *ctx, size_t *out_count);
  double (*random)(void *ctx);
  void (*emit_achievement)(void *ctx, qa_actor_id player, const char *id);
  double (*powerup_until)(void *ctx, qa_actor_id actor, const char *kind);
  void (*set_base_team_health)(void *ctx, bool enabled);
  void (*register_coop_bots_effect)(void *ctx);
  void (*register_mg3_buddha_effect)(void *ctx);
  void (*register_state_extension)(void *ctx, const char *id, qa_comp_q1_capture_fn capture,
                                   qa_comp_q1_restore_fn restore, void *extension_ctx);
  void (*spawn_map)(void *ctx, qa_comp_q1_map *map);
  const char *(*map_source)(void *ctx, qa_comp_q1_map *map);
  void (*spawn_entities)(void *ctx, const void *entity_list, const void *model_bounds,
                         const char *map_name, const int64_t *keep, size_t keep_count, bool has_keep);
  void (*player_input)(void *ctx, qa_owned_actor actor, bool attack, bool jump, double teleport_until);
  bool (*body_origin)(void *ctx, qa_actor_id actor, qa_vec3 *out);
  qa_comp_q1_actor *(*drop_backpack)(void *ctx, qa_vec3 origin, const char *weapon,
                                     double shells, double nails, double rockets, double cells);
  /* Base registration + level rules. */
  void *(*register_base)(void *ctx, const qa_comp_q1_base_bindings *bindings);
  qa_comp_q1_intermission (*level_client_connected)(void *ctx, void *base, double time, bool same_level);
  void (*level_reset_player)(void *ctx, void *base, qa_owned_actor actor);
  void (*level_check_limits)(void *ctx, void *base, double time, const double *frags, size_t count,
                             double timelimit, double fraglimit);
  qa_comp_q1_intermission (*level_request_exit)(void *ctx, void *base, double time, bool pressed, bool same_level);
  void (*level_note_attack)(void *ctx, void *base, qa_owned_actor actor, bool axe);
  void (*level_note_damage)(void *ctx, void *base, qa_owned_actor actor, double applied);
  void (*base_reset_finale)(void *ctx, void *base);
  void (*base_dismiss_finale)(void *ctx, void *base);
  qa_comp_q1_actor *(*spawn_select)(void *ctx, void *base, bool force);
  /* Base travel. capture's weapon is a weapon name or NULL. */
  qa_comp_q1_travel *(*new_base_travel)(void *ctx);
  qa_comp_q1_travel *(*capture_base_travel)(void *ctx, qa_owned_actor actor, const char *weapon);
  qa_comp_q1_travel *(*decode_base_travel)(void *ctx, qa_comp_q1_travel *state, double flags);
  void (*admit_base_travel)(void *ctx, qa_owned_actor actor, qa_comp_q1_travel *state);
  /* Mission packs. */
  void *(*register_mission_pack)(void *ctx, void *base, qa_comp_q1_program program,
                                 const qa_comp_q1_pack_bindings *bindings);
  void (*packs_player_spawned)(void *ctx, void *packs, qa_actor_id actor);
  bool (*packs_impulse)(void *ctx, void *packs, qa_actor_id actor, double impulse);
  void (*packs_confirmed_damage)(void *ctx, void *packs, qa_actor_id actor, const qa_actor_id *attacker);
  qa_comp_q1_obituary (*packs_obituary)(void *ctx, void *packs, const qa_comp_q1_obituary_input *input,
                                        const qa_actor_id *inflictor, qa_comp_q1_random_fn random, void *random_ctx);
  void (*packs_player_died)(void *ctx, void *packs, qa_actor_id actor, const qa_actor_id *attacker);
  bool (*packs_select_spawn)(void *ctx, void *packs, qa_actor_id actor, qa_comp_q1_actor **out);
  qa_comp_q1_travel *(*packs_new_travel)(void *ctx, void *packs);
  qa_comp_q1_travel *(*packs_capture_travel)(void *ctx, void *packs, qa_owned_actor actor);
  qa_comp_q1_travel *(*packs_decode_travel)(void *ctx, void *packs, qa_comp_q1_travel *state, double flags);
  void (*packs_admit_travel)(void *ctx, void *packs, qa_owned_actor actor, qa_comp_q1_travel *state);
  qa_comp_q1_actor *(*packs_drop_backpack)(void *ctx, void *packs, qa_owned_actor actor);
  qa_comp_q1_pose (*packs_character_pose)(void *ctx, void *packs, qa_actor_id actor);
  void (*packs_character_frame)(void *ctx, void *packs, qa_actor_id actor, const void *presentation);
  /* Addons. program is the addon id string ("ctf", "dopa", "mg1", "mg3"). */
  void *(*new_addon_context)(void *ctx, void *base, const char *program,
                             const qa_comp_q1_addon_bindings *bindings);
  void *(*register_campaign_addons)(void *ctx, void *base, const char *program,
                                    const qa_comp_q1_addon_bindings *bindings);
  const char *(*addon_program)(void *ctx, void *addon);
  void (*addon_frame)(void *ctx, void *addon, double elapsed_seconds);
  double (*addon_player_number)(void *ctx, void *addon, qa_actor_id actor, const char *name);
  void (*frame_addon_player)(void *ctx, void *addon, qa_actor_id actor, qa_vec3 view_offset);
  bool (*addon_impulse)(void *ctx, void *addon, qa_actor_id actor, double impulse);
  bool (*mg3_item_impulse)(void *ctx, void *addon, qa_actor_id actor, double impulse,
                           void (*emit_text)(void *text_ctx, const char *text), void *text_ctx);
  double (*mg3_hammer_body_frame)(void *ctx, void *addon, qa_comp_q1_player_state *player);
  qa_comp_q1_travel *(*new_addon_travel)(void *ctx, void *addon);
  qa_comp_q1_travel *(*capture_addon_travel)(void *ctx, void *addon, qa_owned_actor actor);
  qa_comp_q1_travel *(*decode_addon_travel)(void *ctx, void *addon, qa_comp_q1_travel *state);
  void (*admit_addon_travel)(void *ctx, void *addon, qa_owned_actor actor, qa_comp_q1_travel *state);
  /* CTF. */
  void *(*register_ctf)(void *ctx, void *addon, const qa_comp_q1_ctf_bindings *bindings,
                        const qa_shared_grapple_control *grapple);
  void (*ctf_spawn_player)(void *ctx, void *ctf, qa_actor_id actor, bool first_admission);
  void (*ctf_player_frame)(void *ctx, void *ctf, qa_actor_id actor);
  void (*ctf_after_physics)(void *ctx, void *ctf, qa_actor_id actor);
  bool (*ctf_impulse)(void *ctx, void *ctf, qa_actor_id actor);
  void (*ctf_death)(void *ctx, void *ctf, qa_actor_id actor, const qa_actor_id *attacker);
  void (*ctf_disconnect_player)(void *ctx, void *ctf, qa_actor_id actor);
  qa_comp_q1_actor *(*ctf_select_spawn)(void *ctx, void *ctf, qa_actor_id actor);
  qa_comp_q1_pose (*ctf_character_pose)(void *ctx, void *ctf, qa_actor_id actor);
  bool (*ctf_fall_damage_allowed)(void *ctx, void *ctf, qa_actor_id actor);
  void (*ctf_suicide)(void *ctx, void *ctf, qa_actor_id actor);
  qa_comp_q1_travel *(*new_ctf_travel)(void *ctx, void *ctf);
  qa_comp_q1_travel *(*capture_ctf_travel)(void *ctx, void *ctf, qa_owned_actor actor);
  qa_comp_q1_travel *(*decode_ctf_travel)(void *ctx, void *ctf, qa_comp_q1_travel *state);
  /* Horde. */
  void *(*register_horde)(void *ctx, void *addon, const qa_comp_q1_horde_bindings *bindings);
  void (*horde_restore_keys)(void *ctx, void *horde, qa_actor_id actor);
  bool (*horde_request_respawn)(void *ctx, void *horde);
  void (*horde_teammate_killed)(void *ctx, void *horde, qa_actor_id actor);
  /* Base free functions. */
  qa_comp_q1_notice (*client_notice)(void *ctx, const char *edition, const char *event,
                                     const char *name, double frags);
  qa_comp_q1_obituary (*obituary)(void *ctx, const qa_comp_q1_obituary_input *input,
                                  qa_comp_q1_random_fn random, void *random_ctx);
  /* QcFinaleAcknowledgement mirror. */
  bool (*finale_poll)(void *ctx, double time, const qa_actor_id *actors, const bool *attack_held, size_t count);
  void (*finale_reset)(void *ctx);
  void (*finale_dismiss)(void *ctx, double time);
  /* clients' game surface (see clients.h), supplied by the caller. */
  qa_comp_q1_client_game client_game;
} qa_comp_q1_runtime_hooks;

/* TS Q1SourceComposition. game/services/hooks are borrowed; the opaque
 * base/packs/addon/ctf/horde handles come from the registration hooks. */
typedef struct qa_comp_q1_source_composition {
  qa_comp_q1_game *game;
  qa_comp_q1_source_selection selection;
  const qa_comp_q1_services *services;
  const qa_comp_q1_runtime_hooks *hooks;
  qa_comp_q1_source_clients clients;
  void *base;
  void *packs;
  void *addon;
  void *ctf;
  void *horde;
  void *horde_addon;
  qa_comp_q1_campaign_binding base_campaign; /* forwarding binding */
  /* Binding tables handed to the registration hooks (TS closures). */
  qa_comp_q1_base_bindings base_bindings;
  qa_comp_q1_addon_bindings addon_bindings;
  qa_comp_q1_horde_bindings horde_bindings;
  qa_comp_q1_pack_bindings pack_bindings;
  qa_comp_q1_ctf_bindings ctf_bindings;
} qa_comp_q1_source_composition;

/** TS constructor. Returns false only on binding allocation failure. */
bool qa_comp_q1_source_composition_init(qa_comp_q1_source_composition *composition, qa_comp_q1_game *game,
                                        const qa_comp_q1_source_selection *selection,
                                        const qa_comp_q1_services *services,
                                        const qa_comp_q1_runtime_hooks *hooks);
void qa_comp_q1_source_composition_destroy(qa_comp_q1_source_composition *composition);

/* The two damage-source effect bodies the constructor registers. */
typedef struct qa_comp_q1_effect_result { bool cancel; double amount; } qa_comp_q1_effect_result;
qa_comp_q1_effect_result qa_comp_q1_coop_bots_before_quad(qa_comp_q1_source_composition *composition,
                                                          const qa_damage_request *request, double amount);
typedef struct qa_comp_q1_lethal_result { double health; const char *reaction; } qa_comp_q1_lethal_result;
qa_comp_q1_lethal_result qa_comp_q1_mg3_buddha_lethal_health(qa_comp_q1_source_composition *composition,
                                                             const qa_damage_request *request, double health);

void qa_comp_q1_source_composition_spawn_map(qa_comp_q1_source_composition *composition, qa_comp_q1_map *map);
void qa_comp_q1_source_composition_spawn_entities(qa_comp_q1_source_composition *composition,
                                                  const void *entity_list, const void *model_bounds,
                                                  const char *map_name, const int64_t *keep,
                                                  size_t keep_count, bool has_keep);
bool qa_comp_q1_source_composition_attach(qa_comp_q1_source_composition *composition, qa_owned_actor actor,
                                          const qa_comp_q1_client_admission *admission);
void qa_comp_q1_source_composition_spawned(qa_comp_q1_source_composition *composition, qa_actor_id actor,
                                           bool first_admission);
bool qa_comp_q1_source_composition_userinfo(qa_comp_q1_source_composition *composition, qa_actor_id actor,
                                            const qa_comp_userinfo_entry *values, size_t count);
bool qa_comp_q1_source_composition_no_target(qa_comp_q1_source_composition *composition, qa_actor_id actor);
void qa_comp_q1_source_composition_set_no_target(qa_comp_q1_source_composition *composition,
                                                 qa_actor_id actor, bool enabled);
void qa_comp_q1_source_composition_input(qa_comp_q1_source_composition *composition, qa_actor_id actor,
                                         qa_comp_q1_source_input input);
void qa_comp_q1_source_composition_pre_frame(qa_comp_q1_source_composition *composition, double elapsed_seconds);
void qa_comp_q1_source_composition_player_pre_think(qa_comp_q1_source_composition *composition, qa_actor_id actor);
void qa_comp_q1_source_composition_player_post_think(qa_comp_q1_source_composition *composition, qa_actor_id actor);
bool qa_comp_q1_source_composition_impulse(qa_comp_q1_source_composition *composition, qa_actor_id actor);
void qa_comp_q1_source_composition_fired(qa_comp_q1_source_composition *composition, qa_actor_id actor,
                                         qa_item_id weapon);
void qa_comp_q1_source_composition_before_reaction(qa_comp_q1_source_composition *composition,
                                                   qa_owned_actor actor, const qa_damage_decision *decision);
void qa_comp_q1_source_composition_disconnect(qa_comp_q1_source_composition *composition, qa_actor_id actor);
void qa_comp_q1_source_composition_request_respawn(qa_comp_q1_source_composition *composition,
                                                   qa_actor_id actor, qa_comp_q1_travel *coop_entry);
qa_comp_q1_actor *qa_comp_q1_source_composition_select_spawn(qa_comp_q1_source_composition *composition,
                                                             bool has_actor, qa_actor_id actor, bool force);
qa_comp_q1_travel *qa_comp_q1_source_composition_new_travel(qa_comp_q1_source_composition *composition);
qa_comp_q1_travel *qa_comp_q1_source_composition_capture_travel(qa_comp_q1_source_composition *composition,
                                                                qa_owned_actor actor);
qa_comp_q1_travel *qa_comp_q1_source_composition_decode_travel(qa_comp_q1_source_composition *composition,
                                                               qa_comp_q1_travel *state);
void qa_comp_q1_source_composition_admit_travel(qa_comp_q1_source_composition *composition,
                                                qa_owned_actor actor, qa_comp_q1_travel *state);
qa_comp_q1_actor *qa_comp_q1_source_composition_drop_inventory(qa_comp_q1_source_composition *composition,
                                                               qa_owned_actor actor);
qa_comp_q1_intermission qa_comp_q1_source_composition_request_intermission_exit(
    qa_comp_q1_source_composition *composition, bool pressed, bool has_input, qa_actor_id input_actor,
    bool input_attack);
void qa_comp_q1_source_composition_dismiss_finale(qa_comp_q1_source_composition *composition);
qa_comp_q1_pose qa_comp_q1_source_composition_character_pose(qa_comp_q1_source_composition *composition,
                                                             qa_actor_id actor);
bool qa_comp_q1_source_composition_fall_damage_allowed(qa_comp_q1_source_composition *composition,
                                                       qa_actor_id actor);
void qa_comp_q1_source_composition_character_frame(qa_comp_q1_source_composition *composition,
                                                   qa_actor_id actor, const void *presentation);
void qa_comp_q1_source_composition_suicide(qa_comp_q1_source_composition *composition, qa_actor_id actor);

/* createQ1SourceComposition(): the foundation-construction mirror. The
 * options view carries the fields composition observes; the create_game
 * hook builds the Q1Foundation with skill/teamplay/gravity read from
 * services cvars (use qa_comp_q1_skill_from_cvar for the skill getter). */
typedef struct qa_comp_q1_foundation_options {
  const char *edition;
  bool coop;
  double deathmatch;
  bool has_max_clients;
  double max_clients;
  const char *precache_program; /* set here: "id1" only for the id1 program */
} qa_comp_q1_foundation_options;
int qa_comp_q1_skill_from_cvar(double value);
bool qa_comp_create_q1_source_composition(
    qa_comp_q1_source_composition *composition,
    qa_comp_q1_game *(*create_game)(void *ctx, const qa_comp_q1_foundation_options *options,
                                    const qa_comp_q1_services *services),
    void *create_ctx, const qa_comp_q1_foundation_options *options,
    const qa_comp_q1_source_selection *selection, const qa_comp_q1_services *services,
    const qa_comp_q1_runtime_hooks *hooks, qa_comp_q1_game **out_game);

#endif

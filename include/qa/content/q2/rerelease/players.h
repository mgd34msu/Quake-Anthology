/*
 * Port of src/content/q2/rerelease/players.ts. GPL-2.0-or-later.
 * TS owner src/content/q2/base/player/ is unported, so the base
 * surface this file uses is mirrored here with the collision-proof
 * qa_q2rr_ prefix (Q2PlayerState, Q2PlayerHooks, Q2PlayerRules,
 * Q2PlayerCarry, Q2PlayerContext, Q2Weapons state surface,
 * Q2Intermission). TS `super.*` calls route through
 * qa_q2rr_players_base_ops (fn+ctx supplied by the future base
 * port); a NULL op aborts via qa_q2rr_error. Maps are dynamic
 * arrays keyed by qa_same_actor. Strings the TS mutates are fixed
 * buffers (userinfo truncated as in TS).
 */
#ifndef QA_CONTENT_Q2_RERELEASE_PLAYERS_H
#define QA_CONTENT_Q2_RERELEASE_PLAYERS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/rerelease/types.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/world.h"

/* ---- base/player/types.ts mirrors ---- */
typedef struct qa_q2rr_player_movement {
  qa_vec3 view_angles, command_angles;
  double water_level, water_type;
  bool grounded, ducked;
  double buttons;
  qa_bounds standing_bounds;
  bool animate_q2;
} qa_q2rr_player_movement;
/* kind: "spawn"|"teleport"|"freeze"|"noclip". */
typedef struct qa_q2rr_movement_change {
  const char *kind;
  qa_vec3 origin, velocity, angles, command_angles;
  double hold_milliseconds;
  bool spectator, enabled;
} qa_q2rr_movement_change;
typedef struct qa_q2rr_player_view {
  qa_vec3 angles, offset, kick_angles, gun_angles, gun_offset;
  qa_vec4 blend;
  double fov;
  bool underwater;
  double flashes, health, armor, ammo, score;
  qa_item_id selected_item; /* nullable */
  bool has_timer; qa_item_id timer_item; double timer_seconds;
  bool spectator;
  double layouts;
} qa_q2rr_player_view;
typedef struct qa_q2rr_score_row { double slot; const char *name; double score, ping, minutes; bool spectator; } qa_q2rr_score_row;
/** TS Q2PlayerEvent union on kind (exact strings). */
typedef struct qa_q2rr_player_event {
  const char *kind;
  bool has_target; qa_actor_id target; const char *level, *text; /* print */
  bool has_actor; qa_actor_id actor; double slot; const char *name, *skin; /* userinfo */
  qa_q2rr_player_view view; /* view */
  const qa_q2rr_score_row *rows; size_t row_count; bool has_killer; qa_actor_id killer; bool reliable; /* scoreboard */
  const qa_inventory_entry *entries; size_t entry_count; bool has_visible; bool visible;
  bool has_selected; qa_item_id selected; /* inventory/help */
  qa_vec3 origin; double time; /* trail */
  bool has_chase_target; qa_actor_id chase_target; /* chase */
} qa_q2rr_player_event;
/* TS Q2CharacterWeapon. q2_name exact weapon string, NULL = null. */
typedef struct qa_q2rr_character_weapon {
  const char *q2_name;
  qa_item_id ammo;
  qa_vec3 kick_angles, kick_origin;
  const char *loop_sound;
} qa_q2rr_character_weapon;
typedef struct qa_q2rr_weapon_result { bool has; qa_q2rr_character_weapon v; } qa_q2rr_weapon_result;
typedef struct qa_q2rr_spawn_choice { bool has; qa_vec3 origin, angles; } qa_q2rr_spawn_choice;

/** TS Q2PlayerHooks (subset shape kept whole). Optional may be NULL. */
typedef struct qa_q2rr_player_hooks {
  void *ctx;
  double (*quad_fire_drop_until)(void *ctx, qa_actor_id actor);
  bool (*grant_selected_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  bool (*give_selected_item)(void *ctx, qa_actor_id actor, const char *const *args, size_t n);
  qa_q2rr_weapon_result (*weapon_state)(void *ctx, qa_actor_id actor);
  qa_q2rr_player_movement (*movement)(void *ctx, qa_actor_id actor);
  void (*set_movement)(void *ctx, qa_actor_id actor, qa_q2rr_movement_change change);
  void (*emit)(void *ctx, qa_q2rr_player_event event);
  void (*noise)(void *ctx, qa_actor_id actor, qa_vec3 origin);
  /* weaponInput returns opaque input via out blob (base weapons mirror). */
  void (*weapon_input)(void *ctx, qa_actor_id actor, void *out_input);
  bool (*banned)(void *ctx, const char *address);
  void (*score)(void *ctx, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *g, double change, double means, qa_q2_entity *recipient);
  void (*player_spawned)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*persistent_inventory_initialized)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  qa_q2rr_spawn_choice (*select_spawn)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*death)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const qa_attack_provenance *attack);
  void (*drop_inventory)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const qa_attack_provenance *attack);
  void (*before_death_inventory)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const qa_attack_provenance *attack);
  void (*disconnect)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  bool (*command)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const char *name, const char *const *args, size_t n);
} qa_q2rr_player_hooks;

typedef struct qa_q2rr_player_rules {
  char password[128], spectator_password[128];
  double max_spectators;
  bool cheats;
  double time_limit_minutes, frag_limit;
  const char *const *map_list; size_t map_list_count;
  bool map_list_shuffle;
  char next_map[128], spawn_point[128];
  double flood_messages, flood_seconds, flood_wait_seconds;
  double roll_speed, roll_angle, run_pitch, run_roll, bob_up, bob_pitch, bob_roll;
  qa_vec3 gun_offset;
} qa_q2rr_player_rules;
void qa_q2rr_create_player_rules(qa_q2rr_player_rules *out);

/** TS Q2PlayerCarry. inventory malloc'd; free with _carry_free. */
typedef struct qa_q2rr_player_carry {
  double health, maximum_health;
  qa_armor_state armor;
  qa_inventory_entry *inventory; size_t inventory_count;
  const char *weapon; /* Q2WeaponName or NULL */
  qa_item_id selected_item;
  double score, flags, power_cubes;
} qa_q2rr_player_carry;
void qa_q2rr_carry_free(qa_q2rr_player_carry *c);
bool qa_q2rr_carry_clone(qa_q2rr_player_carry *dst, const qa_q2rr_player_carry *src);

/** TS Q2PlayerState (base). flood_times malloc'd. */
typedef struct qa_q2rr_player_state {
  double slot, entered_at;
  bool use_q2_weapons, use_q2_inventory;
  qa_inventory_entry *spawn_inventory; size_t spawn_inventory_count;
  char userinfo[2048], name[64], skin[128];
  const char *gender; /* "male"|"female"|"neutral" */
  double fov;
  qa_q2rr_hand hand;
  bool spectator, requested_spectator, connected, dead, gibbed, noclip, god, notarget;
  double score, ping, respawn_time, air_finished, next_drown_time, drown_damage;
  double old_water_level, breather_sound, pain_debounce;
  double damage_blood, damage_armor, damage_power_armor, damage_knockback;
  qa_vec3 damage_from, damage_blend;
  double damage_alpha, bonus_alpha, damage_pitch, damage_roll, damage_time;
  double power_armor_time, fall_time, fall_value;
  bool landmark_free_fall;
  double landmark_noise_time;
  qa_vec3 old_velocity, old_view_angles;
  double killer_yaw, buttons, latched_buttons;
  bool weapon_thunk;
  double bob_time, bob_move;
  char event[64];
  double animation_priority, animation_end;
  bool animation_duck, animation_run;
  char loop_sound[128];
  qa_item_id selected_item;
  bool show_scores, show_inventory, show_help;
  bool has_chase_target; qa_actor_id chase_target;
  bool has_coop_respawn; qa_q2rr_player_carry coop_respawn;
  double *flood_times; size_t flood_time_count, flood_time_cap;
  double flood_lock_until;
} qa_q2rr_player_state;
void qa_q2rr_player_state_init(qa_q2rr_player_state *s, double slot, double entered_at);
void qa_q2rr_player_state_destroy(qa_q2rr_player_state *s);
bool qa_q2rr_player_state_clone(qa_q2rr_player_state *dst, const qa_q2rr_player_state *src);

/** TS Q2Weapons state surface mirror (foundation/weapons/index.ts). */
typedef struct qa_q2rr_weapon_state_entry {
  qa_actor_id actor;
  const char *weapon;  /* Q2WeaponName or NULL */
  const char *pending; /* or NULL */
  const char *phase;   /* e.g. "firing" */
  bool grenade_blew_up;
  double grenade_time;
  const char *view_model; /* nullable-ish */
  double last_firing_time;
  bool has_last_firing_time;
  char loop_sound[128];
} qa_q2rr_weapon_state_entry;
typedef struct qa_q2rr_weapon_definition { qa_item_id item; const char *name; qa_item_id ammo; double quantity; } qa_q2rr_weapon_definition;
typedef struct qa_q2rr_weapons {
  void *ctx;
  qa_q2rr_weapon_state_entry *(*get_state)(void *ctx, qa_actor_id actor); /* NULL = absent */
  const qa_q2rr_weapon_definition *(*registered_definitions)(void *ctx, size_t *n);
  bool (*has_state)(void *ctx, qa_actor_id actor);
  void *impl; /* opaque base weapons object */
} qa_q2rr_weapons;

/** TS Q2PlayerContext / Q2CharacterContext (closures as fn+ctx). */
typedef struct qa_q2rr_player_context {
  qa_q2_entity *entity;
  qa_q2_game_services *game;
  qa_q2rr_player_state *state;
  qa_q2rr_player_movement movement;
  const qa_q2rr_player_rules *rules;
  qa_q2_item_module *items;
  qa_q2rr_weapons *weapons;
  qa_q2rr_player_hooks *hooks;
  void *call_ctx;
  qa_q2_player_powerups (*powerups)(void *ctx);
  qa_q2rr_weapon_result (*weapon_state)(void *ctx);
  void (*environment_damage)(void *ctx, double amount, double means, double flags);
  /* Character services view used by view.ts sound filtering. */
  void (*sound)(void *ctx, qa_q2_entity *e, const char *path, double channel, double volume, double attenuation);
} qa_q2rr_player_context;

/* TS Q2Intermission. map fixed buffer; landmark has=false = null. */
typedef struct qa_q2rr_intermission {
  bool playing; /* true = {kind:"playing"} */
  char map[256];
  double started;
  bool exit;
  bool has_landmark; qa_q2_landmark_carry landmark;
} qa_q2rr_intermission;

typedef struct qa_q2rr_player_admission {
  double slot;
  const char *userinfo;
  bool initialize_inventory;
  bool has_use_q2_weapons; bool use_q2_weapons;
  bool has_use_q2_inventory; bool use_q2_inventory;
  const qa_q2rr_player_carry *carry; /* optional */
} qa_q2rr_player_admission;
typedef struct qa_q2rr_connection_result { bool allowed; char userinfo[4096]; char reason[256]; } qa_q2rr_connection_result;

/** TS super (Q2Players) operations supplied by the base port. */
typedef struct qa_q2rr_players_base_ops {
  void *ctx;
  qa_q2rr_player_state *(*attach)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_q2rr_player_admission a);
  qa_q2rr_player_carry (*save_carry)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*restore_carry)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const qa_q2rr_player_carry *c);
  bool (*record_death)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r);
  void (*death)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r);
  void (*obituary)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r);
  void (*clear_death_inventory)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*record_damage)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_damage_decision d);
  void (*apply_score)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r, qa_q2rr_player_state *recipient, double change);
  void (*after_client_think)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*begin_frame)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*end_frame)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*check_rules)(void *ctx, qa_q2_game_services *g);
  void (*before_exit_level)(void *ctx, qa_q2_game_services *g, const char *map);
  bool (*client_command)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const char *cmd, const char *const *args, size_t n);
  void (*put_in_server)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, bool restore, const qa_q2_landmark_carry *lm);
  void (*respawn)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*copy_to_body_queue)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  void (*chase)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, double step);
  void (*scoreboard)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g);
  /* spawnPlacement super result */
  void (*spawn_placement)(void *ctx, qa_q2_entity *e, qa_q2_game_services *g, const qa_q2_landmark_carry *lm,
                          qa_vec3 *origin, qa_vec3 *angles, qa_vec3 *velocity, bool *from_landmark);
  qa_q2_callback_definitions (*callbacks)(void *ctx);
} qa_q2rr_players_base_ops;

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

typedef struct qa_q2rr_state_pair { qa_actor_id actor; qa_q2rr_player_state *state; } qa_q2rr_state_pair;
typedef struct qa_q2rr_extra_pair { qa_actor_id actor; qa_q2rr_player_extra_state *state; } qa_q2rr_extra_pair;
typedef struct qa_q2rr_placement { qa_actor_id actor; qa_vec3 origin, angles, velocity; bool from_landmark; } qa_q2rr_placement;

/** TS Q2RereleasePlayers extends Q2Players. Owns state objects. */
typedef struct qa_q2_rerelease_players {
  qa_q2_item_module *items;
  qa_q2rr_weapons *weapons;
  qa_q2rr_player_hooks hooks;
  qa_q2_rerelease_hooks rerelease_hooks;
  qa_q2rr_player_rules rules;
  qa_q2rr_players_base_ops base;
  qa_q2rr_state_pair *states; size_t state_count, state_cap;
  qa_q2rr_extra_pair *rerelease_states; size_t extra_count, extra_cap;
  qa_q2_rerelease_options rerelease_options;
  double coop_restart_time;
  bool deadly_kill_box;
  double intermission_flags;
  bool has_intermission_fade_until; double intermission_fade_until;
  bool has_intermission_camera; qa_vec3 intermission_camera_origin, intermission_camera_angles;
  bool intermission_camera_set;
  bool has_extension; qa_q2_rerelease_player_extension extension;
  qa_q2rr_intermission intermission;
  qa_q2rr_placement *squad_spawns; size_t squad_count, squad_cap;
  qa_q2rr_placement *selected_spawns; size_t selected_count, selected_cap;
} qa_q2_rerelease_players;

/* Map helpers (also used by sibling rerelease modules). */
qa_q2rr_player_state *qa_q2rr_players_get_state(qa_q2_rerelease_players *p, qa_actor_id a);
qa_q2rr_player_extra_state *qa_q2rr_players_get_extra_or_null(qa_q2_rerelease_players *p, qa_actor_id a);
bool qa_q2rr_players_set_state(qa_q2_rerelease_players *p, qa_actor_id a, qa_q2rr_player_state *s);
bool qa_q2rr_players_set_extra(qa_q2_rerelease_players *p, qa_actor_id a, qa_q2rr_player_extra_state *s);
void qa_q2rr_players_delete_state(qa_q2_rerelease_players *p, qa_actor_id a);
void qa_q2rr_players_delete_extra(qa_q2_rerelease_players *p, qa_actor_id a);

bool qa_q2_rerelease_players_init(qa_q2_rerelease_players *p, qa_q2_item_module *items,
                                  qa_q2rr_weapons *weapons, qa_q2rr_player_hooks hooks,
                                  qa_q2_rerelease_hooks rr_hooks,
                                  const qa_q2_rerelease_options_changes *options,
                                  const qa_q2rr_player_rules *rules /* NULL = defaults */);
void qa_q2_rerelease_players_destroy(qa_q2_rerelease_players *p);
bool qa_q2_rerelease_players_spawn(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g);
qa_q2_callback_definitions qa_q2_rerelease_players_callbacks(qa_q2_rerelease_players *p);
/* TS extra(): aborts when not admitted. _get_ avoids type-name clash. */
qa_q2rr_player_extra_state *qa_q2_rerelease_players_get_extra(qa_q2_rerelease_players *p, qa_actor_id a);
qa_q2rr_player_context qa_q2_rerelease_players_context(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g);
qa_q2rr_connection_result qa_q2_rerelease_players_connect(qa_q2_rerelease_players *p, qa_q2_game_services *g, const char *userinfo, bool is_bot);
qa_q2rr_player_state *qa_q2_rerelease_players_attach(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_q2rr_player_admission a);
void qa_q2_rerelease_players_userinfo_changed(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, const char *source);
void qa_q2_rerelease_players_weapon_picked(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_item_id item, bool first);
void qa_q2_rerelease_players_obituary(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r);
void qa_q2_rerelease_players_clear_death_inventory(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g);
bool qa_q2_rerelease_players_can_drop_coop_stay_items(qa_q2_rerelease_players *p, qa_q2_game_services *g);
void qa_q2_rerelease_players_death(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r);
void qa_q2_rerelease_players_record_damage(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_damage_decision d);
qa_q2rr_player_carry qa_q2_rerelease_players_save_carry(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_rerelease_players_restore_carry(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, const qa_q2rr_player_carry *c);
bool qa_q2_rerelease_players_record_death(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, qa_death_reaction r);
void qa_q2_rerelease_players_after_client_think(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_rerelease_players_record_weapon_fire(qa_q2_rerelease_players *p, qa_actor_id a, double now);
void qa_q2_rerelease_players_reveal_invisibility(qa_q2_rerelease_players *p, qa_actor_id a, double until);
void qa_q2_rerelease_players_movement_impact(qa_q2_rerelease_players *p, qa_actor_id a, double impact_delta, bool on_ladder);
void qa_q2_rerelease_players_begin_frame(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g);
void qa_q2_rerelease_players_dead_frame(qa_q2_rerelease_players *p, qa_q2rr_player_context *c);
void qa_q2_rerelease_players_begin_intermission(qa_q2_rerelease_players *p, qa_q2_game_services *g, const char *map, const qa_q2_landmark_carry *lm);
void qa_q2_rerelease_players_begin_rerelease_intermission(qa_q2_rerelease_players *p, qa_q2_game_services *g, const char *map, const qa_q2_landmark_carry *lm, double flags);
void qa_q2_rerelease_players_check_rules(qa_q2_rerelease_players *p, qa_q2_game_services *g);
void qa_q2_rerelease_players_before_exit_level(qa_q2_rerelease_players *p, qa_q2_game_services *g, const char *map);
void qa_q2_rerelease_players_fade_frame(qa_q2_rerelease_players *p, qa_q2_game_services *g);
void qa_q2_rerelease_players_move_to_camera(qa_q2_rerelease_players *p, qa_q2_game_services *g, qa_vec3 origin, qa_vec3 angles, bool entering);
void qa_q2_rerelease_players_finish_camera(qa_q2_rerelease_players *p, qa_q2_game_services *g);
void qa_q2_rerelease_players_emit_flashlight(qa_q2_rerelease_players *p, qa_actor_id a, qa_q2_game_services *g);
void qa_q2_rerelease_players_end_frame(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g);
bool qa_q2_rerelease_players_client_command(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, const char *cmd, const char *const *args, size_t n);
void qa_q2_rerelease_players_put_in_server(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g, bool restore_loadout, const qa_q2_landmark_carry *lm);
void qa_q2_rerelease_players_respawn(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g);
bool qa_q2_rerelease_players_kill_box(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g);
bool qa_q2_rerelease_players_find_respawn_spot(qa_q2_rerelease_players *p, qa_q2_entity *player, qa_q2_game_services *g, qa_vec3 *out);
void qa_q2_rerelease_players_spawn_placement(qa_q2_rerelease_players *p, qa_q2_entity *e, qa_q2_game_services *g,
                                             const qa_q2_landmark_carry *lm, qa_vec3 *origin, qa_vec3 *angles,
                                             qa_vec3 *velocity, bool *from_landmark);
void qa_q2_rerelease_players_world_effects(qa_q2_rerelease_players *p, qa_q2rr_player_context *c);
void qa_q2_rerelease_players_falling_damage_noop(qa_q2rr_player_context *c);
void qa_q2_rerelease_players_damage_feedback(qa_q2_rerelease_players *p, qa_q2rr_player_context *c, double pain_index, double *flashes, double *out_pain);
qa_q2rr_player_view qa_q2_rerelease_players_build_view(qa_q2_rerelease_players *p, qa_q2rr_player_context *c, double flashes, bool intermission);
void qa_q2_rerelease_players_update_bob(qa_q2_rerelease_players *p, qa_q2rr_player_context *c);
void qa_q2_rerelease_players_client_animation(qa_q2_rerelease_players *p, qa_q2rr_player_context *c);

#endif

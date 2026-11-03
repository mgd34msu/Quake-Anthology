/*
 * Port of src/content/q2/base/player/types.ts. Player behavior adapted
 * from id Software Quake II p_client.c, p_view.c, p_hud.c and g_cmds.c.
 * GPL-2.0-or-later.
 * C accommodations: TS Q2Weapons / Q2WeaponState are the real
 * qa_q2w_weapons / qa_q2w_state (foundation/weapons/player.h).
 * Live-state strings the TS mutates in place (userinfo/name/skin) are
 * fixed buffers bounded by the largest TS truncation (Q2RereleasePlayers
 * keeps 2047/31 characters; the base 511/15). Type names use
 * the qa_q2_base_* prefix where contracts/protocol.h or
 * persistence/q2-players.h already own the plain qa_q2_* spelling.
 */
#ifndef QA_CONTENT_Q2_BASE_PLAYER_TYPES_H
#define QA_CONTENT_Q2_BASE_PLAYER_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/q2/foundation/host.h"
#include "qa/content/q2/foundation/items.h"
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/contracts/gameplay.h"

/** TS Q2PlayerMovement. */
typedef struct qa_q2_player_movement {
  qa_vec3 view_angles, command_angles;
  double water_level, water_type;
  bool grounded, ducked;
  double buttons;
  /* TS upMove: latest command upmove; a spectator pressing jump cycles its chase target. */
  double up_move;
  qa_bounds standing_bounds;
  bool animate_q2;
} qa_q2_player_movement;

/** TS Q2PlayerMovementChange union on kind:
 * "spawn"|"teleport" (all fields), "freeze" (origin/angles),
 * "noclip" (enabled). */
typedef struct qa_q2_player_movement_change {
  const char *kind;
  qa_vec3 origin, velocity, angles, command_angles;
  double hold_milliseconds;
  bool spectator;
  bool enabled;
} qa_q2_player_movement_change;

/** TS Q2PlayerView. selected_item/timer_item NULL = TS null. */
typedef struct qa_q2_base_player_view {
  qa_vec3 angles, offset, kick_angles, gun_angles, gun_offset;
  qa_vec4 blend;
  double fov;
  bool underwater;
  double flashes, health, armor, ammo, score;
  qa_item_id selected_item;
  bool has_timer;
  qa_item_id timer_item;
  double timer_seconds;
  bool spectator;
  double layouts;
  bool help_icon; /* TS helpIcon?: STAT_HELPICON shows i_help while an objective change is pending */
} qa_q2_base_player_view;

/** TS Q2ScoreRow. */
typedef struct qa_q2_score_row {
  double slot;
  const char *name;
  double score, ping, minutes;
  bool spectator;
} qa_q2_score_row;

typedef struct qa_q2_player_item_label { qa_item_id item; const char *name; } qa_q2_player_item_label;

/** TS Q2PlayerEvent union on kind (exact strings). Unused fields zero. */
typedef struct qa_q2_player_event {
  const char *kind;
  bool has_target; qa_actor_id target;   /* print (null = all) */
  const char *level;                     /* print */
  const char *text;                      /* print/stufftext */
  qa_actor_id actor;                     /* all but print */
  double slot;                           /* userinfo */
  const char *name, *skin;               /* userinfo */
  qa_q2_base_player_view view;           /* view */
  const qa_q2_score_row *rows; size_t row_count; /* scoreboard */
  bool has_killer; qa_actor_id killer;   /* scoreboard */
  bool reliable;                         /* scoreboard */
  const qa_inventory_entry *entries; size_t entry_count; /* inventory */
  bool has_visible; bool visible;        /* inventory/help */
  bool has_selected; qa_item_id selected; /* inventory (NULL selected = null) */
  const qa_q2_player_item_label *labels; size_t label_count; /* inventory */
  qa_vec3 origin; double time;           /* trail */
  bool has_chase_target; qa_actor_id chase_target; /* chase */
} qa_q2_player_event;

/** TS Q2CharacterWeapon. */
typedef struct qa_q2_character_weapon {
  const char *q2_name; /* nullable */
  qa_item_id ammo;     /* nullable */
  qa_vec3 kick_angles, kick_origin;
  const char *loop_sound;
} qa_q2_character_weapon;

/** TS Q2PlayerHooks. Optional TS members may be NULL.
 * weapon_state false return is TS null. select_spawn false is TS
 * null/undefined. score's attacker may be NULL. */
typedef struct qa_q2_player_hooks {
  void *ctx;
  double (*quad_fire_drop_until)(void *ctx, qa_actor_id actor);
  bool (*grant_selected_arsenal)(void *ctx, qa_actor_id actor, const char *category);
  bool (*give_selected_item)(void *ctx, qa_actor_id actor, const char *const *args, size_t arg_count);
  bool (*weapon_state)(void *ctx, qa_actor_id actor, qa_q2_character_weapon *out);
  qa_q2_player_movement (*movement)(void *ctx, qa_actor_id actor);
  void (*set_movement)(void *ctx, qa_actor_id actor, qa_q2_player_movement_change change);
  void (*emit)(void *ctx, qa_q2_player_event event);
  void (*noise)(void *ctx, qa_actor_id actor, qa_vec3 origin);
  qa_q2w_input (*weapon_input)(void *ctx, qa_actor_id actor);
  bool (*banned)(void *ctx, const char *address);
  /* TS selectEmpty?(): g_select_empty, `use` may select a weapon that has no ammo. NULL = absent. */
  bool (*select_empty)(void *ctx);
  void (*score)(void *ctx, qa_q2_entity *victim, qa_q2_entity *attacker, qa_q2_game_services *game,
                double change, double means_of_death, qa_q2_entity *recipient);
  void (*player_spawned)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  void (*persistent_inventory_initialized)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  bool (*select_spawn)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, qa_vec3 *out_origin, qa_vec3 *out_angles);
  void (*death)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, const qa_attack_provenance *attack);
  void (*drop_inventory)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, const qa_attack_provenance *attack);
  void (*before_death_inventory)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, const qa_attack_provenance *attack);
  void (*disconnect)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game);
  bool (*command)(void *ctx, qa_q2_entity *entity, qa_q2_game_services *game, const char *name,
                  const char *const *args, size_t arg_count);
} qa_q2_player_hooks;

/** TS Q2PlayerRules. map_list strings are borrowed. */
typedef struct qa_q2_base_player_rules {
  const char *password, *spectator_password;
  double max_spectators;
  bool cheats;
  double time_limit_minutes, frag_limit;
  const char *const *map_list; size_t map_list_count;
  bool map_list_shuffle;
  const char *next_map, *spawn_point;
  double flood_messages, flood_seconds, flood_wait_seconds;
  double roll_speed, roll_angle, run_pitch, run_roll, bob_up, bob_pitch, bob_roll;
  qa_vec3 gun_offset;
} qa_q2_base_player_rules;

/* Present-field mask for TS Partial<Q2PlayerRules> arguments. */
enum {
  QA_Q2_RULE_PASSWORD = 1u << 0, QA_Q2_RULE_SPECTATOR_PASSWORD = 1u << 1,
  QA_Q2_RULE_MAX_SPECTATORS = 1u << 2, QA_Q2_RULE_CHEATS = 1u << 3,
  QA_Q2_RULE_TIME_LIMIT = 1u << 4, QA_Q2_RULE_FRAG_LIMIT = 1u << 5,
  QA_Q2_RULE_MAP_LIST = 1u << 6, QA_Q2_RULE_MAP_LIST_SHUFFLE = 1u << 7,
  QA_Q2_RULE_NEXT_MAP = 1u << 8, QA_Q2_RULE_SPAWN_POINT = 1u << 9,
  QA_Q2_RULE_FLOOD_MESSAGES = 1u << 10, QA_Q2_RULE_FLOOD_SECONDS = 1u << 11,
  QA_Q2_RULE_FLOOD_WAIT = 1u << 12, QA_Q2_RULE_ROLL_SPEED = 1u << 13,
  QA_Q2_RULE_ROLL_ANGLE = 1u << 14, QA_Q2_RULE_RUN_PITCH = 1u << 15,
  QA_Q2_RULE_RUN_ROLL = 1u << 16, QA_Q2_RULE_BOB_UP = 1u << 17,
  QA_Q2_RULE_BOB_PITCH = 1u << 18, QA_Q2_RULE_BOB_ROLL = 1u << 19,
  QA_Q2_RULE_GUN_OFFSET = 1u << 20
};
/* TS createQ2PlayerRules(changes): changes NULL or mask 0 = defaults. */
qa_q2_base_player_rules qa_q2_create_player_rules(const qa_q2_base_player_rules *changes, uint32_t present_mask);

/** TS Q2PlayerCarry. inventory entries are an owned copy in live
 * state (free with qa_q2_base_player_carry_clear). */
typedef struct qa_q2_base_player_carry {
  double health, maximum_health;
  qa_armor_state armor;
  qa_inventory_entry *inventory; size_t inventory_count;
  const char *weapon; /* nullable */
  qa_item_id selected_item; /* nullable */
  double score, flags, power_cubes;
} qa_q2_base_player_carry;
void qa_q2_base_player_carry_set(qa_q2_base_player_carry *carry, qa_q2_base_player_carry value);
void qa_q2_base_player_carry_clear(qa_q2_base_player_carry *carry);

/**
 * TS Q2PlayerState. Fields here belong to Q2's player
 * rules/presentation, never duplicate shared live health or inventory.
 * gender: "male"|"female"|"neutral"; hand: "right"|"left"|"center".
 */
typedef struct qa_q2_base_player_state {
  double slot, entered_at;
  bool use_q2_weapons, use_q2_inventory;
  qa_inventory_entry *spawn_inventory; size_t spawn_inventory_count;
  char userinfo[2048];
  char name[32];
  char skin[2048];
  const char *gender;
  double fov;
  const char *hand;
  bool spectator, requested_spectator, connected, dead, gibbed, noclip, god, notarget;
  double score, ping, respawn_time, air_finished, next_drown_time, drown_damage;
  double old_water_level, breather_sound, pain_debounce;
  double damage_blood, damage_armor, damage_power_armor, damage_knockback;
  qa_vec3 damage_from, damage_blend;
  double damage_alpha, bonus_alpha, damage_pitch, damage_roll, damage_time;
  double power_armor_time, fall_time, fall_value;
  /* pers.helpchanged / pers.game_helpchanged of the classic help computer. */
  double help_changed, game_help_changed;
  bool landmark_free_fall;
  double landmark_noise_time;
  qa_vec3 old_velocity, old_view_angles;
  double killer_yaw, buttons, latched_buttons;
  bool weapon_thunk;
  double bob_time, bob_move;
  const char *event;
  double animation_priority, animation_end;
  bool animation_duck, animation_run;
  const char *loop_sound;
  qa_item_id selected_item; /* nullable */
  bool jump_held;
  bool show_scores, show_inventory, show_help;
  bool has_chase_target; qa_actor_id chase_target;
  bool has_coop_respawn; qa_q2_base_player_carry coop_respawn;
  double *flood_times; size_t flood_time_count, flood_time_cap;
  double flood_lock_until;
} qa_q2_base_player_state;
/* TS Q2PlayerState constructor. */
void qa_q2_base_player_state_init(qa_q2_base_player_state *state, double slot, double entered_at);
void qa_q2_base_player_state_destroy(qa_q2_base_player_state *state);

/** TS Q2PlayerContext. The TS powerups()/weaponState()/
 * environmentDamage() closures become the qa_q2_player_context_*
 * helpers in player/index.h; players is the object they close over. */
struct qa_q2_players;
typedef struct qa_q2_player_context {
  qa_q2_entity *entity;
  qa_q2_game_services *game;
  qa_q2_base_player_state *state;
  qa_q2_player_movement movement;
  qa_q2_base_player_rules *rules;
  qa_q2_item_module *items;
  qa_q2w_weapons *weapons;
  qa_q2_player_hooks *hooks;
  struct qa_q2_players *players;
} qa_q2_player_context;

/** TS Q2CharacterServices. sound carries the TS defaulted channel,
 * volume and attenuation explicitly. */
typedef struct qa_q2_character_services {
  void *host_ctx;
  double (*now)(void *ctx);
  double (*random)(void *ctx);
  qa_gameplay_authority *combat;
  qa_shared_inventory_table *inventory;
  double (*point_contents)(void *ctx, qa_vec3 point);
  void (*emit)(void *ctx, qa_q2_presentation_event event);
  qa_q2_mode mode;
  double deathmatch_flags;
  qa_q2_edition edition;
  void *ctx;
  qa_body_state (*body)(void *ctx, qa_owned_actor actor);
  void (*move)(void *ctx, qa_q2_entity *entity, qa_q2_body_changes changes, bool link);
  void (*sound)(void *ctx, qa_q2_entity *entity, const char *path, double channel, double volume, double attenuation);
} qa_q2_character_services;

/** TS Q2CharacterContext. cb_ctx backs the three closures; the
 * scratch ids keep an adapter's attack pointers alive. */
typedef struct qa_q2_character_context {
  qa_q2_entity *entity;
  qa_q2_character_services game;
  qa_q2_base_player_state *state;
  qa_q2_player_movement movement;
  qa_q2_base_player_rules *rules;
  void *noise_ctx;
  void (*noise)(void *ctx, qa_actor_id actor, qa_vec3 origin);
  void *cb_ctx;
  qa_q2_player_powerups (*powerups)(void *ctx);
  bool (*weapon_state)(void *ctx, qa_q2_character_weapon *out); /* false = null */
  void (*environment_damage)(void *ctx, double amount, double means, double flags);
  qa_actor_id scratch_world, scratch_inflictor;
} qa_q2_character_context;

#endif

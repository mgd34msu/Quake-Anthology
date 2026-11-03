/*
 * Port of src/content/q2/foundation/weapons/types.ts. Weapons run on the
 * one Q2 game: qa_q2_entity / qa_q2_game_services / qa_q2_foundation_host
 * (foundation/host.h). Plain TS Errors raise qa_raise("Error", ...).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q2_FOUNDATION_WEAPONS_TYPES_H
#define QA_CONTENT_Q2_FOUNDATION_WEAPONS_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/content/monsters/target.h"
#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/session.h"
#include "qa/contracts/weapon_behavior.h"
#include "qa/contracts/world.h"
#include "qa/core/random/q2_rerelease.h"
#include "qa/content/q2/foundation/callbacks.h"
#include "qa/content/q2/foundation/checkpoint.h"
#include "qa/content/q2/foundation/fields.h"
#include "qa/content/q2/foundation/host.h"
#include "qa/foundation/support.h"


/* ---- weapons/types.ts ---- */
typedef struct qa_q2w_owner { qa_owned_actor actor; double view_height; } qa_q2w_owner;
typedef const char *qa_q2w_name; /* Q2WeaponName / Q2BaseWeaponName */
typedef enum qa_q2w_phase { QA_Q2W_ACTIVATING, QA_Q2W_READY, QA_Q2W_FIRING, QA_Q2W_DROPPING } qa_q2w_phase;
const char *qa_q2w_phase_name(qa_q2w_phase phase);

typedef struct qa_q2w_definition {
  qa_q2w_name name;
  qa_item_id item;
  const char *classname;
  qa_item_id ammo; /* nullable */
  double quantity, warning;
  const char *view_model, *world_model;
  double player_model, activate_last, fire_last, idle_last, deactivate_last;
  const double *pauses; size_t pause_count;
  const double *fires; size_t fire_count;
  bool repeating;
  /* TS noHaste?: IF_NO_HASTE, Dualfire and the Haste tech do not speed the firing frames (false = undefined). */
  bool no_haste;
} qa_q2w_definition;
typedef qa_q2w_definition qa_q2w_base_definition;

typedef enum qa_q2w_event_kind {
  QA_Q2W_EVENT_MUZZLEFLASH, QA_Q2W_EVENT_BEAM, QA_Q2W_EVENT_VIEW_WEAPON,
  QA_Q2W_EVENT_PLAYER_ANIMATION, QA_Q2W_EVENT_INVISIBILITY_REVEAL
} qa_q2w_event_kind;
typedef struct qa_q2w_event {
  qa_q2w_event_kind kind;
  const qa_actor_id *actor; /* nullable (beam) */
  double flash; bool silenced;                 /* muzzleflash */
  const char *effect; qa_vec3 start, end; double duration; /* beam */
  qa_q2w_name weapon; const char *model; double player_model, frame, skin, rate; /* view-weapon */
  qa_vec3 kick_origin, kick_angles;
  const char *priority; double first, last; bool reset_time; /* player-animation */
  double until;                                 /* invisibility-reveal */
} qa_q2w_event;

typedef struct qa_q2w_hooks {
  void *ctx;
  double (*quad_multiplier)(void *ctx, qa_actor_id actor);          /* optional */
  double (*source_damage_multiplier)(void *ctx, qa_actor_id actor); /* optional */
  double (*firing_interval)(void *ctx, qa_actor_id actor, double seconds); /* optional */
  void (*emit)(void *ctx, qa_q2w_event event);
  void (*noise)(void *ctx, qa_actor_id actor, qa_vec3 origin, bool secondary);
  void (*dodge)(void *ctx, qa_actor_id monster, qa_actor_id attacker, double eta_seconds, qa_trace_result trace);
  bool lag_is_history;
  /* history begin(); returned token passed to lag_end (TS closure). */
  void *(*lag_begin)(void *ctx, qa_actor_id actor, qa_vec3 start, qa_vec3 direction);
  void (*lag_end)(void *ctx, void *token);
  void (*ammo_changed)(void *ctx, qa_actor_id actor, qa_item_id ammo);
  bool (*can_target)(void *ctx, const qa_actor_id *attacker, qa_actor_id target);
} qa_q2w_hooks;

typedef enum qa_q2w_hand { QA_Q2W_HAND_RIGHT, QA_Q2W_HAND_LEFT, QA_Q2W_HAND_CENTER } qa_q2w_hand;
typedef struct qa_q2w_input {
  bool attack, latched_attack, holster;
  qa_vec3 angles;
  bool ducked, spectator, notarget;
  qa_q2w_hand hand;
  bool animate_player;
  double quad_until, double_until, quad_fire_until;
  bool haste, no_stack_double, instant_switch, quick_switch, infinite_ammo, players_collide;
  double gravity;
  bool weapon_thunk;
} qa_q2w_input;

typedef enum qa_q2w_hand_reservation { QA_Q2W_RES_NONE, QA_Q2W_RES_FINITE, QA_Q2W_RES_INFINITE } qa_q2w_hand_reservation;
typedef enum qa_q2w_handoff { QA_Q2W_HANDOFF_ACTIVE, QA_Q2W_HANDOFF_HOLSTERING, QA_Q2W_HANDOFF_HOLSTERED } qa_q2w_handoff;

typedef struct qa_q2w_state {
  qa_q2w_handoff primary_handoff;
  qa_q2w_name weapon, last_weapon, pending; /* nullable */
  qa_q2w_phase phase;
  double frame, think_time, fire_finished;
  bool fire_buffered, latched_attack;
  double machinegun_shots, empty_sound_time;
  qa_q2w_hand_reservation hand_reservation;
  double grenade_time, grenade_finished;
  bool grenade_blew_up;
  qa_vec3 kick_origin, kick_angles;
  double kick_time, kick_until, kick_duration;
  const char *loop_sound;
  const char *view_model; /* nullable */
  double view_skin, last_firing_time;
  bool source_firing;
  double gun_rate;
} qa_q2w_state;
/* TS constructor(weapon = "blaster"); pass NULL name for null weapon
 * via qa_q2w_state_init_null. Default-name form: qa_q2w_state_init. */
void qa_q2w_state_init(qa_q2w_state *state, qa_q2w_name weapon);
void qa_q2w_state_init_default(qa_q2w_state *state);

typedef struct qa_q2w_grenade_adjustment { double right, up, gravity; } qa_q2w_grenade_adjustment;

/* TS MOD record as constants. */
enum {
  QA_Q2W_MOD_BLASTER = 1, QA_Q2W_MOD_SHOTGUN = 2, QA_Q2W_MOD_SUPERSHOTGUN = 3,
  QA_Q2W_MOD_MACHINEGUN = 4, QA_Q2W_MOD_CHAINGUN = 5, QA_Q2W_MOD_GRENADE = 6,
  QA_Q2W_MOD_GRENADE_SPLASH = 7, QA_Q2W_MOD_ROCKET = 8, QA_Q2W_MOD_ROCKET_SPLASH = 9,
  QA_Q2W_MOD_HYPERBLASTER = 10, QA_Q2W_MOD_RAILGUN = 11, QA_Q2W_MOD_BFG_LASER = 12,
  QA_Q2W_MOD_BFG_BLAST = 13, QA_Q2W_MOD_BFG_EFFECT = 14, QA_Q2W_MOD_HAND_GRENADE = 15,
  QA_Q2W_MOD_HAND_GRENADE_SPLASH = 16, QA_Q2W_MOD_HELD_GRENADE = 24, QA_Q2W_MOD_HIT = 32
};
#define QA_Q2W_SHOT_MASK ((double)(1 | 2 | 0x2000000 | 0x4000000))
#define QA_Q2W_PLAYER_CONTENTS ((double)0x40000000)
#define QA_Q2W_PROJECTILE_MASK ((double)((1 | 2 | 0x2000000 | 0x4000000) | 0x40000000 | 0x4000))
#define QA_Q2W_WATER_MASK ((double)(8 | 16 | 32))

#endif

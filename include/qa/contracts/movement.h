/*
 * Port of src/contracts/movement.ts (types only): movement states,
 * profiles, inputs, effects, and provider shapes. Q2MovementState /
 * Q2RereleaseMovementState are re-exported from protocol.h exactly as
 * the TS re-exports them from protocol.ts.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_MOVEMENT_H
#define QA_CONTRACTS_MOVEMENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/mod_client_outputs.h"
#include "qa/contracts/numeric.h"
#include "qa/contracts/protocol.h"
#include "qa/contracts/scene.h"
#include "qa/contracts/time.h"
#include "qa/contracts/world.h"

/* TS MovementCommand = UserCommand. */
typedef qa_user_command qa_movement_command;

typedef struct qa_fixed_movement_pose qa_fixed_movement_pose;

typedef struct qa_equipment_movement {
  bool has_speed_multiplier;
  double speed_multiplier;
  const qa_fixed_movement_pose *pose; /* optional */
  const qa_vec3 *velocity;            /* optional */
  double gravity_scale;
  bool prediction_suppressed;
} qa_equipment_movement;

typedef struct qa_q1_movement_state {
  /* kind "q1-netquake" */
  qa_vec3 origin, velocity, angles, old_origin, angular_velocity;
  qa_vec3 view_angles, punch_angles;
  double move_type, flags;
  qa_trace_hit ground;
  double water_level, water_type, teleport_time_seconds;
  qa_vec3 water_jump_direction;
  double ideal_pitch;
  bool fix_angle;
  double health;
} qa_q1_movement_state;

typedef struct qa_qw_movement_state {
  /* kind "q1-quakeworld" */
  qa_vec3 origin, velocity, angles;
  double old_buttons, water_jump_time_seconds;
  bool dead;
  double spectator;
  qa_trace_hit ground;
} qa_qw_movement_state;

/* TS Q3MovementState extends Pick<Q3PlayerState,
 * "commandTimeMilliseconds" | "movementType" | "bobCycle" |
 * "movementFlags" | "movementTimeMilliseconds" | "origin" |
 * "velocity" | "gravity" | "speed" | "deltaAngleWords" |
 * "movementDirection" | "grapplePoint" | "flags" | "viewAngles" |
 * "viewHeight"> with kind "q3"; the picked fields are inlined
 * below in that order, before ground. */
typedef struct qa_q3_movement_state {
  double command_time_milliseconds, movement_type, bob_cycle;
  double movement_flags, movement_time_milliseconds;
  qa_vec3 origin, velocity;
  double gravity, speed;
  qa_angle_words delta_angle_words;
  double movement_direction;
  qa_vec3 grapple_point;
  double flags;
  qa_vec3 view_angles;
  double view_height;
  qa_trace_hit ground;
  double predictable_event_sequence;
  const qa_actor_id *jump_pad; /* nullable */
  double movement_frame, jump_pad_frame;
} qa_q3_movement_state;

/* TS MovementState union on kind. */
typedef enum qa_movement_state_kind {
  QA_MOVEMENT_STATE_Q1, QA_MOVEMENT_STATE_QW, QA_MOVEMENT_STATE_Q2,
  QA_MOVEMENT_STATE_Q2_RERELEASE, QA_MOVEMENT_STATE_Q3
} qa_movement_state_kind;
typedef struct qa_movement_state {
  qa_movement_state_kind kind;
  union {
    qa_q1_movement_state q1;
    qa_qw_movement_state qw;
    qa_q2_movement_state q2;
    qa_q2_rerelease_movement_state q2_rerelease;
    qa_q3_movement_state q3;
  } as;
} qa_movement_state;

typedef struct qa_q1_movement_parameters {
  double gravity, stop_speed, max_speed, spectator_max_speed;
  double accelerate, air_accelerate, water_accelerate;
  double friction, water_friction, entity_gravity;
} qa_q1_movement_parameters;

/* Movement profiles. TS common fields: id, clock, numeric. Kinds:
 * "q1-netquake"(edition "classic"|"rerelease"|"quake64", parameters,
 * edgeFriction, noClipAngleHack) | "q1-quakeworld"(parameters) |
 * "q2-classic"(airAccelerate, snapInitial, optional strafejumpHack) |
 * "q2-rerelease"(airAccelerate, n64Physics) | "q3"(product
 * "baseq3"|"missionpack", fixedMilliseconds number|null, noFootsteps). */
typedef struct qa_movement_profile {
  const char *kind;
  qa_provider_id id;
  qa_clock_profile clock;
  qa_numeric_profile numeric;
  const char *edition;
  qa_q1_movement_parameters parameters;
  double edge_friction;
  bool no_clip_angle_hack;
  double air_accelerate;
  bool snap_initial;
  bool has_strafejump_hack;
  bool strafejump_hack;
  bool n64_physics;
  const char *product;
  bool has_fixed_milliseconds;
  double fixed_milliseconds;
  bool no_footsteps;
} qa_movement_profile;
typedef qa_movement_profile qa_q1_movement_profile;
typedef qa_movement_profile qa_qw_movement_profile;
typedef qa_movement_profile qa_q2_movement_profile;
typedef qa_movement_profile qa_q2_rerelease_movement_profile;
typedef qa_movement_profile qa_q3_movement_profile;

/* TS WeaponState union on kind: "q1"(frame, attackFinishedSeconds,
 * sourceWeapon) | "q2"(gunFrame, state, pendingWeapon,
 * machinegunShots, grenadeTime, grenadeBlewUp) | "q3"(sourceWeapon,
 * state, timeMilliseconds). */
typedef struct qa_weapon_state {
  const char *kind;
  double frame, attack_finished_seconds, source_weapon;
  double gun_frame, state;
  qa_item_id pending_weapon; /* nullable */
  double machinegun_shots;
  qa_source_time grenade_time;
  bool grenade_blew_up;
  double time_milliseconds;
} qa_weapon_state;

typedef struct qa_arsenal_state {
  qa_provider_id provider;
  qa_item_id active_weapon; /* nullable */
  qa_weapon_state state;
  const qa_inventory_entry *ammo; size_t ammo_count;
} qa_arsenal_state;

/* TS AnimationState union on kind: "q1"(frame,nextFrameSeconds) |
 * "q2"(frame,endFrame,priority,duck,run) | "q3"(legs,torso,timers). */
typedef struct qa_animation_state {
  const char *kind;
  double frame, next_frame_seconds, end_frame, priority;
  bool duck, run;
  double legs, torso, legs_timer_milliseconds, torso_timer_milliseconds;
} qa_animation_state;

typedef struct qa_actor_animation_state {
  qa_provider_id provider;
  qa_animation_state state;
} qa_actor_animation_state;

struct qa_fixed_movement_pose {
  /* TS kind is the literal "fixed". */
  bool crouched;
  qa_bounds bounds;
  double view_height;
};

typedef struct qa_movement_environment {
  const qa_mod_client_movement_outputs *client_outputs; /* optional */
  bool has_speed_multiplier;
  double speed_multiplier;
  const qa_fixed_movement_pose *pose; /* optional */
  double health;
  bool flight, haste, invulnerable;
  double gravity_multiplier;
} qa_movement_environment;

/* Movement inputs share MovementInputFields; per-kind command/state/
 * profile types are carried in the matching members. execution is
 * "authoritative" | "prediction" (exact strings). Q2 rerelease adds
 * viewOffset + snapInitial. */
typedef struct qa_movement_input {
  const char *kind; /* "q1-netquake"|"q1-quakeworld"|"q2-classic"|"q2-rerelease"|"q3" */
  qa_owned_actor actor;
  double command_sequence;
  qa_frame_context frame;
  qa_trace_shape shape;
  const qa_bounds *current_bounds; /* optional */
  qa_movement_environment environment;
  qa_arsenal_state arsenal;
  qa_actor_animation_state animation;
  const char *execution;
  qa_user_command command;
  qa_movement_state state;
  qa_movement_profile profile;
  qa_vec3 view_offset;  /* q2-rerelease */
  bool snap_initial;    /* q2-rerelease */
} qa_movement_input;
typedef qa_movement_input qa_q1_movement_input;
typedef qa_movement_input qa_qw_movement_input;
typedef qa_movement_input qa_q2_movement_input;
typedef qa_movement_input qa_q2_rerelease_movement_input;
typedef qa_movement_input qa_q3_movement_input;

typedef struct qa_movement_contact {
  qa_trace_hit target;
  qa_trace_result trace;
  double substep;
} qa_movement_contact;

typedef struct qa_predictable_movement_event {
  qa_provider_id provider;
  double sequence, event, parameter;
} qa_predictable_movement_event;

/* TS MovementEffect union on kind: "event"|"weapon"|"weapon-selection"|
 * "ammo"|"animation"|"touch". */
typedef enum qa_movement_effect_kind {
  QA_MOVEMENT_EFFECT_EVENT, QA_MOVEMENT_EFFECT_WEAPON,
  QA_MOVEMENT_EFFECT_WEAPON_SELECTION, QA_MOVEMENT_EFFECT_AMMO,
  QA_MOVEMENT_EFFECT_ANIMATION, QA_MOVEMENT_EFFECT_TOUCH
} qa_movement_effect_kind;
typedef struct qa_movement_effect {
  qa_movement_effect_kind kind;
  qa_predictable_movement_event value;      /* event */
  qa_provider_id provider;                  /* weapon/weapon-selection/animation */
  qa_weapon_state weapon_before, weapon_after;
  qa_item_id selection_before, selection_after; /* nullable */
  qa_item_id item;                          /* ammo */
  double ammo_before, ammo_after;
  qa_animation_state animation_before, animation_after;
  qa_trace_hit target;                      /* touch (never "none") */
  double substep;                           /* touch */
} qa_movement_effect;

typedef struct qa_ordered_movement_effect {
  double substep, sequence;
  qa_source_time time;
  qa_movement_effect effect;
} qa_ordered_movement_effect;

/* TS MovementOutcome<TState>: {kind, status:"active", ...result fields,
 * state} | {kind, status:"actor-removed", actor, commandSequence,
 * effects}. The C form carries the TS `kind` string of the state. */
typedef struct qa_movement_result {
  const char *kind;
  const char *status; /* "active" | "actor-removed" */
  qa_actor_id actor;
  double command_sequence;
  qa_bounds bounds;
  qa_vec3 view_angles;
  double view_height;
  qa_trace_hit ground;
  double water_level, water_type, horizontal_speed;
  const qa_movement_contact *contacts; size_t contact_count;
  const qa_ordered_movement_effect *effects; size_t effect_count;
  qa_arsenal_state arsenal;
  qa_actor_animation_state animation;
  qa_movement_state state; /* active only */
  /* Q2RereleaseMovementPresentation (active q2-rerelease results). */
  qa_vec4 screen_blend;
  double render_flags;
  bool jump_sound, step_clip;
  double impact_delta;
} qa_movement_result;
typedef qa_movement_result qa_q1_movement_result;
typedef qa_movement_result qa_qw_movement_result;
typedef qa_movement_result qa_q2_movement_result;
typedef qa_movement_result qa_q2_rerelease_movement_result;
typedef qa_movement_result qa_q3_movement_result;

/* TS MovementContinuation: {kind:"continue",state} | {kind:"actor-removed"}.
 * MovementInputContinuation additionally carries the command. */
typedef struct qa_movement_continuation {
  bool is_continue;
  qa_movement_state state;
  qa_user_command command; /* input continuation only */
} qa_movement_continuation;
typedef qa_movement_continuation qa_movement_input_continuation;

typedef struct qa_weapon_step_input {
  qa_owned_actor actor;
  qa_movement_command command;
  qa_frame_context frame;
  qa_arsenal_state arsenal;
  qa_actor_animation_state animation;
  qa_movement_environment environment;
  bool gauntlet_hit;
} qa_weapon_step_input;

typedef struct qa_weapon_step_result {
  const qa_movement_continuation *continuation; /* optional */
  qa_arsenal_state arsenal;
  qa_actor_animation_state animation;
  const qa_movement_effect *effects; size_t effect_count;
} qa_weapon_step_result;

/* TS LocomotionAnimation: "idle"|"walk"|"run"|"backward"|"crouch"|
 * "jump"|"land"|"swim" (exact strings). */
typedef const char *qa_locomotion_animation;

typedef struct qa_animation_step_input {
  qa_owned_actor actor;
  qa_frame_context frame;
  qa_actor_animation_state animation;
  qa_locomotion_animation locomotion;
  bool backwards, force;
} qa_animation_step_input;

typedef struct qa_animation_step_result {
  qa_actor_animation_state animation;
  const qa_movement_effect *effects; size_t effect_count;
} qa_animation_step_result;

/** TS MovementInputApplication. */
typedef struct qa_movement_posture {
  qa_bounds bounds;
  double view_height;
} qa_movement_posture;
typedef struct qa_movement_input_application {
  void *ctx;
  qa_movement_input_continuation (*begin)(void *ctx, qa_user_command command,
                                          qa_frame_context frame, qa_movement_state state);
  qa_movement_continuation (*end)(void *ctx, qa_movement_state state, bool has_failed,
                                  bool failed, const qa_movement_posture *posture);
} qa_movement_input_application;

/* TS MovementTouchContact: TouchContact with `other` narrowed to a
 * hit and sourceTrace without `ent`. */
typedef struct qa_movement_touch_contact {
  qa_owned_actor self;
  qa_trace_hit other;
  const qa_plane *plane;
  const qa_touch_surface *surface;
  bool has_source_trace;
  qa_trace_result source_trace_trace;
  bool source_trace_inverted;
} qa_movement_touch_contact;

/** TS MovementServices. input_application is optional (has flag). */
typedef struct qa_movement_services {
  void *ctx;
  bool has_input_application;
  qa_movement_input_application input_application;
  qa_scene_queries scene;
  qa_numeric_operations numeric;
  qa_movement_continuation (*touch)(void *ctx, qa_movement_touch_contact contact,
                                    qa_movement_state state);
  qa_weapon_step_result (*weapon_step)(void *ctx, qa_weapon_step_input input,
                                       qa_movement_state state);
  qa_animation_step_result (*animation_step)(void *ctx, qa_animation_step_input input);
} qa_movement_services;

/* TS MovementProvider union: one struct per kind, each with a typed
 * move(). The C form tags with the exact TS kind string and dispatches
 * through one move over the unified input struct. */
typedef struct qa_movement_provider {
  const char *kind;
  qa_provider_id id;
  void *ctx;
  qa_movement_result (*move)(void *ctx, qa_movement_input input,
                             qa_movement_services services);
} qa_movement_provider;

#endif

/*
 * Port of src/contracts/protocol.ts (types only): wire protocol
 * identities, user commands, entity/player states, snapshots, and
 * decoded network events. TS bigint (Q2 rerelease effects) -> uint64_t.
 * GPL-2.0-or-later.
 */
#ifndef QA_CONTRACTS_PROTOCOL_H
#define QA_CONTRACTS_PROTOCOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

/* Protocol identities. TS unions are on `kind` with literal version
 * numbers; the C form is one struct with kind string (exact TS
 * strings: "q1-netquake" v15, "q1-fitzquake" v666, "q1-rmq" v999,
 * "q1-quakeworld" v28, "q1-quakeworld-donor-wide" v29, "q2-classic"
 * v34, "q2-r1q2" v35 revision 1903|1904|1905, "q2-q2pro" v36 revision
 * 1015..1026 (TS Q2ProRevision), "q2-rerelease" v1038, "q2-kex" v2023,
 * "q2-kex-demo" v2022, "q2-private-classic" v4038, "q3" v68). */
typedef struct qa_protocol_identity {
  const char *kind;
  int64_t version;
  int64_t flags;    /* q1-rmq / quakeworld-donor-wide */
  int64_t revision; /* q2-r1q2 / q2-q2pro */
} qa_protocol_identity;
typedef qa_protocol_identity qa_q1_protocol_identity;
typedef qa_protocol_identity qa_qw_protocol_identity;
typedef qa_protocol_identity qa_q2_protocol_identity;
typedef qa_protocol_identity qa_q3_protocol_identity;

/* TS IntegerTriple / AngleWords: readonly [number, number, number]. */
typedef struct qa_integer_triple { double values[3]; } qa_integer_triple;
typedef qa_integer_triple qa_angle_words;

typedef struct qa_q1_user_command {
  /* kind "q1-netquake" */
  double acknowledged_server_time_seconds;
  qa_vec3 view_angles;
  double forward_move, side_move, up_move, buttons, impulse;
} qa_q1_user_command;

typedef struct qa_qw_user_command {
  /* kind "q1-quakeworld" */
  double milliseconds;
  qa_vec3 angles;
  double forward_move, side_move, up_move, buttons, impulse;
} qa_qw_user_command;

typedef struct qa_q2_user_command {
  /* kind "q2-classic" */
  double milliseconds;
  qa_angle_words angle_shorts;
  double forward_move, side_move, up_move, buttons, impulse, light_level;
} qa_q2_user_command;

typedef struct qa_q2_rerelease_user_command {
  /* kind "q2-rerelease" */
  double milliseconds;
  qa_vec3 angles;
  double forward_move, side_move, buttons, server_frame;
} qa_q2_rerelease_user_command;

typedef struct qa_q3_user_command {
  /* kind "q3" */
  double server_time_milliseconds;
  qa_angle_words angle_words;
  double buttons, weapon, forward_move, right_move, up_move;
} qa_q3_user_command;

/* TS UserCommand union on kind. */
typedef enum qa_user_command_kind {
  QA_USER_COMMAND_Q1, QA_USER_COMMAND_QW, QA_USER_COMMAND_Q2,
  QA_USER_COMMAND_Q2_RERELEASE, QA_USER_COMMAND_Q3
} qa_user_command_kind;
typedef struct qa_user_command {
  qa_user_command_kind kind;
  union {
    qa_q1_user_command q1;
    qa_qw_user_command qw;
    qa_q2_user_command q2;
    qa_q2_rerelease_user_command q2_rerelease;
    qa_q3_user_command q3;
  } as;
} qa_user_command;

typedef struct qa_q1_entity_state {
  double number;
  qa_vec3 origin, angles;
  double model_index, frame, color_map, skin, effects;
} qa_q1_entity_state;

typedef struct qa_q1_extended_entity_state {
  qa_q1_entity_state base;
  double alpha, scale, lerp_finish_seconds;
  bool step;
} qa_q1_extended_entity_state;

typedef struct qa_q1_client_data {
  double view_height, ideal_pitch;
  qa_vec3 punch_angles, velocity;
  double items;
  bool on_ground, in_water;
  double weapon_frame, armor, weapon_model, health, ammo;
  double shells, nails, rockets, cells, active_weapon;
} qa_q1_client_data;

typedef struct qa_qw_player_state {
  double number, flags;
  qa_vec3 origin, velocity;
  double model_index, frame, skin, effects, weapon_frame, milliseconds;
  qa_qw_user_command command;
} qa_qw_player_state;

typedef struct qa_q2_movement_state {
  /* kind "q2-classic" */
  double type;
  qa_integer_triple origin_eighths, velocity_eighths;
  double flags, time_eight_milliseconds, gravity;
  qa_angle_words delta_angle_shorts;
} qa_q2_movement_state;

typedef struct qa_q2_rerelease_movement_state {
  /* kind "q2-rerelease" */
  double type;
  qa_vec3 origin, velocity;
  double flags, time_milliseconds, gravity;
  qa_vec3 delta_angles;
  double view_height;
} qa_q2_rerelease_movement_state;

/* TS Q2PlayerView shared fields. */
typedef struct qa_q2_player_view {
  qa_vec3 view_angles, view_offset, kick_angles, gun_angles, gun_offset;
  double gun_index, gun_frame, fov, render_flags;
  qa_double_view stats;
} qa_q2_player_view;

typedef struct qa_q2_player_state {
  qa_q2_player_view view;
  /* kind "q2-classic" */
  qa_q2_movement_state movement;
  qa_vec4 blend;
} qa_q2_player_state;

typedef struct qa_q2_rerelease_player_state {
  qa_q2_player_view view;
  /* kind "q2-rerelease" */
  qa_q2_rerelease_movement_state movement;
  double gun_skin, gun_rate;
  qa_vec4 screen_blend, damage_blend;
  double team_id;
} qa_q2_rerelease_player_state;

typedef struct qa_q2_entity_state {
  double number;
  qa_vec3 origin, angles, old_origin;
  double model_indexes[4];
  double frame, skin, effects, render_effects, solid, sound, event;
} qa_q2_entity_state;

typedef struct qa_q2_rerelease_entity_state {
  qa_q2_entity_state base; /* base.effects unused; see effects below */
  uint64_t effects;        /* TS bigint */
  double alpha, scale, instance_bits, loop_volume, loop_attenuation;
  double owner, old_frame;
} qa_q2_rerelease_entity_state;

/* TS Q3Trajectory kind: "stationary"|"interpolate"|"linear"|
 * "linear-stop"|"sine"|"gravity" (exact strings). */
typedef struct qa_q3_trajectory {
  const char *kind;
  double time_milliseconds, duration_milliseconds;
  qa_vec3 base, delta;
} qa_q3_trajectory;

typedef struct qa_q3_entity_state {
  double number, type, flags;
  qa_q3_trajectory position, angular_position;
  double time_milliseconds, time2_milliseconds;
  qa_vec3 origin, origin2, angles, angles2;
  double other_entity_number, other_entity_number2, ground_entity_number;
  double constant_light, loop_sound, model_index, model_index2;
  double client_number, frame, solid, event, event_parameter;
  double powerups, weapon, legs_animation, torso_animation, generic1;
} qa_q3_entity_state;

typedef struct qa_q3_player_state {
  double command_time_milliseconds, movement_type, bob_cycle;
  double movement_flags, movement_time_milliseconds;
  qa_vec3 origin, velocity;
  double weapon_time_milliseconds, gravity, speed;
  qa_angle_words delta_angle_words;
  double ground_entity_number, legs_timer_milliseconds, legs_animation;
  double torso_timer_milliseconds, torso_animation, movement_direction;
  qa_vec3 grapple_point;
  double flags, event_sequence;
  double events[2];
  double event_parameters[2];
  double external_event, external_event_parameter, external_event_time_milliseconds;
  double client_number, weapon, weapon_state;
  qa_vec3 view_angles;
  double view_height, damage_event, damage_yaw, damage_pitch, damage_count;
  qa_double_view stats, persistent, powerups, ammo;
  double generic1, loop_sound, jump_pad_entity;
  double ping_milliseconds, movement_frame_count, jump_pad_frame, entity_event_sequence;
} qa_q3_player_state;

/* Snapshots. TS Q1Snapshot is a union on kind: "q1-netquake" (protocol
 * version 15, Q1EntityState[]) | "q1-enhanced" (other Q1 protocols,
 * Q1ExtendedEntityState[]); both lists live in one struct. */
typedef struct qa_q1_snapshot {
  const char *kind;
  qa_q1_protocol_identity protocol;
  double server_time_seconds;
  qa_q1_client_data client;
  const qa_q1_entity_state *entities; size_t entity_count;
  const qa_q1_extended_entity_state *extended_entities; size_t extended_entity_count;
} qa_q1_snapshot;

typedef struct qa_qw_snapshot {
  /* kind "q1-quakeworld" */
  qa_qw_protocol_identity protocol;
  double sequence;
  bool has_delta_sequence;
  double delta_sequence;
  const qa_qw_player_state *players; size_t player_count;
  const qa_q1_entity_state *entities; size_t entity_count;
} qa_qw_snapshot;

/* TS Q2Frame shared fields (deltaFrame nullable). */
typedef struct qa_q2_snapshot {
  /* kind "q2-classic" */
  qa_q2_protocol_identity protocol;
  double server_frame;
  bool has_delta_frame;
  double delta_frame;
  double suppressed_count;
  qa_byte_view area_bits;
  qa_q2_player_state player;
  const qa_q2_entity_state *entities; size_t entity_count;
} qa_q2_snapshot;

typedef struct qa_q2_rerelease_snapshot {
  /* kind "q2-rerelease" */
  qa_q2_protocol_identity protocol;
  double server_frame;
  bool has_delta_frame;
  double delta_frame;
  double suppressed_count;
  qa_byte_view area_bits;
  qa_q2_rerelease_player_state player;
  const qa_q2_rerelease_entity_state *entities; size_t entity_count;
} qa_q2_rerelease_snapshot;

typedef struct qa_q3_snapshot {
  /* kind "q3" */
  qa_q3_protocol_identity protocol;
  double flags, ping_milliseconds, server_time_milliseconds;
  qa_byte_view area_mask;
  qa_q3_player_state player;
  const qa_q3_entity_state *entities; size_t entity_count;
  double server_command_count, server_command_sequence;
} qa_q3_snapshot;

/* TS NetworkSnapshot union. */
typedef enum qa_network_snapshot_kind {
  QA_NETWORK_SNAPSHOT_Q1, QA_NETWORK_SNAPSHOT_QW, QA_NETWORK_SNAPSHOT_Q2,
  QA_NETWORK_SNAPSHOT_Q2_RERELEASE, QA_NETWORK_SNAPSHOT_Q3
} qa_network_snapshot_kind;
typedef struct qa_network_snapshot {
  qa_network_snapshot_kind kind;
  union {
    const qa_q1_snapshot *q1;
    const qa_qw_snapshot *qw;
    const qa_q2_snapshot *q2;
    const qa_q2_rerelease_snapshot *q2_rerelease;
    const qa_q3_snapshot *q3;
  } as;
} qa_network_snapshot;

/* TS NetworkEvent union on kind: "print"(level,text) |
 * "center-print"(text) | "command-text"(text) |
 * "config-string"(index,value) | "sound"(entityNumber,channel,
 * soundIndex,origin|null,volume,attenuation,delaySeconds) |
 * "q1-damage"(armor,blood,source) | "q1-particle"(origin,direction,
 * count,color) | "q2-layout"(program) | "q2-inventory"(counts) |
 * "q2-muzzle-flash"(entityNumber,flash,monster) |
 * "q3-server-command"(sequence,text) | "disconnect"(reason). */
typedef enum qa_network_event_kind {
  QA_NETWORK_EVENT_PRINT, QA_NETWORK_EVENT_CENTER_PRINT,
  QA_NETWORK_EVENT_COMMAND_TEXT, QA_NETWORK_EVENT_CONFIG_STRING,
  QA_NETWORK_EVENT_SOUND, QA_NETWORK_EVENT_Q1_DAMAGE,
  QA_NETWORK_EVENT_Q1_PARTICLE, QA_NETWORK_EVENT_Q2_LAYOUT,
  QA_NETWORK_EVENT_Q2_INVENTORY, QA_NETWORK_EVENT_Q2_MUZZLE_FLASH,
  QA_NETWORK_EVENT_Q3_SERVER_COMMAND, QA_NETWORK_EVENT_DISCONNECT
} qa_network_event_kind;
typedef struct qa_network_event {
  qa_network_event_kind kind;
  const char *text;   /* print/center-print/command-text/q3-server-command */
  double level;       /* print */
  double index;       /* config-string */
  const char *value;  /* config-string */
  double entity_number, channel, sound_index;
  const qa_vec3 *origin; /* sound, nullable; also q1-particle by value below */
  qa_vec3 origin_value;
  double volume, attenuation, delay_seconds;
  double armor, blood;    /* q1-damage */
  qa_vec3 source;         /* q1-damage */
  qa_vec3 direction;      /* q1-particle */
  double count, color;    /* q1-particle */
  const char *program;    /* q2-layout */
  qa_double_view counts;  /* q2-inventory */
  double flash;           /* q2-muzzle-flash */
  bool monster;           /* q2-muzzle-flash */
  double sequence;        /* q3-server-command */
  const char *reason;     /* disconnect */
} qa_network_event;

#endif

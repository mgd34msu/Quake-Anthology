/*
 * Port of src/network/q2/state.ts: Q2 wire state shapes. TS Vec3 is a
 * Float32Array(3); the C form is float[3], so stores round to binary32
 * exactly as typed-array stores do. Typed-array fields keep their C
 * element types; plain TS number fields are double. Distinct C names
 * (qa_q2_wire_*) avoid the contracts qa_q2_entity_state family.
 * GPL-2.0-or-later.
 */
#ifndef QA_NETWORK_Q2_STATE_H
#define QA_NETWORK_Q2_STATE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef float qa_q2_wire_vec3[3];

#define QA_Q2_MAX_EDICTS 1024

/* TS PmTypeT. */
/* movement/q2/types.h spells the same TS PmTypeT values; one shared definition. */
#ifndef QA_Q2_PM_TYPE_DEFINED
#define QA_Q2_PM_TYPE_DEFINED
typedef enum qa_q2_pm_type {
  QA_Q2_PM_NORMAL, QA_Q2_PM_SPECTATOR, QA_Q2_PM_DEAD, QA_Q2_PM_GIB,
  QA_Q2_PM_FREEZE, QA_Q2_PM_GRAPPLE = 5, QA_Q2_PM_NOCLIP = 6
} qa_q2_pm_type;
#endif

/* TS PmoveStateT. delta_angle_float is TS deltaAngleEncoding=="float". */
typedef struct qa_q2_wire_pmove {
  qa_q2_wire_vec3 delta_angles_f;
  bool delta_angle_float;
  double pm_type;
  int32_t origin[3];
  int32_t velocity[3];
  double pm_flags;
  double pm_time;
  double gravity;
  int16_t delta_angles[3];
  double viewheight;
  qa_q2_wire_vec3 origin_f;
  qa_q2_wire_vec3 velocity_f;
} qa_q2_wire_pmove;

/* TS UsercmdT. */
typedef struct qa_q2_wire_usercmd {
  double server_frame;
  double msec;
  double buttons;
  int16_t angles[3];
  double forwardmove;
  double sidemove;
  double upmove;
  double impulse;
  double lightlevel;
} qa_q2_wire_usercmd;

#define QA_Q2_RF_BEAM 128
#define QA_Q2_MAX_STATS 32
#define QA_Q2_MAX_STATS_STORAGE 64

/* TS EntityStateT. */
typedef struct qa_q2_wire_entity {
  double number;
  qa_q2_wire_vec3 origin;
  qa_q2_wire_vec3 angles;
  qa_q2_wire_vec3 old_origin;
  double modelindex, modelindex2, modelindex3, modelindex4;
  double frame, skinnum, effects, renderfx, solid, sound, event;
  double alpha, scale, instance_bits, loop_volume, loop_attenuation;
  double owner, old_frame, morefx;
} qa_q2_wire_entity;

/* TS Q2ProPlayerFog. */
typedef struct qa_q2_pro_player_fog {
  double color[3];
  double density, sky_factor, height_density, height_falloff;
  double height_start_color[3];
  double height_end_color[3];
  double height_start_distance, height_end_distance;
} qa_q2_pro_player_fog;

/* TS PlayerStateT. */
typedef struct qa_q2_wire_player {
  double clientnum;
  qa_q2_pro_player_fog q2pro_fog;
  qa_q2_wire_pmove pmove;
  qa_q2_wire_vec3 viewangles;
  qa_q2_wire_vec3 viewoffset;
  qa_q2_wire_vec3 kick_angles;
  qa_q2_wire_vec3 gunangles;
  qa_q2_wire_vec3 gunoffset;
  double gunindex, gunskin, gunframe, gunrate;
  float blend[4];
  float damage_blend[4];
  double fov, rdflags;
  int16_t stats[QA_Q2_MAX_STATS_STORAGE];
  double team_id;
} qa_q2_wire_player;

void qa_q2_wire_pmove_init(qa_q2_wire_pmove *state);
void qa_q2_wire_usercmd_init(qa_q2_wire_usercmd *cmd);
void qa_q2_wire_entity_init(qa_q2_wire_entity *state);
void qa_q2_wire_player_init(qa_q2_wire_player *state);

/* TS ANGLE2SHORT / SHORT2ANGLE. */
int32_t qa_q2_angle2short(double x);
double qa_q2_short2angle(double x);

/* TS vec3(x,y,z) -> Float32Array (each store rounds to binary32). */
void qa_q2_vec3_make(qa_q2_wire_vec3 out, double x, double y, double z);
/* TS VectorCopy(a, b): b[0..2] = a[0..2]. */
void qa_q2_vector_copy(const qa_q2_wire_vec3 a, qa_q2_wire_vec3 b);
/* TS DotProduct over Float32Array values widened to number. */
double qa_q2_dot_product(const qa_q2_wire_vec3 a, const qa_q2_wire_vec3 b);

/* TS readElement: aborts with the exact RangeError when out of range. */
float qa_q2_read_element_f32(const float *array, size_t length, int64_t index);
int16_t qa_q2_read_element_i16(const int16_t *array, size_t length, int64_t index);
int32_t qa_q2_read_element_i32(const int32_t *array, size_t length, int64_t index);
uint8_t qa_q2_read_element_u8(const uint8_t *array, size_t length, int64_t index);
double qa_q2_read_element_f64(const double *array, size_t length, int64_t index);

#endif

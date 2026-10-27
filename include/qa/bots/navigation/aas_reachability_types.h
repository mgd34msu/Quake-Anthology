/* Port of src/bots/navigation/aas-reachability-types.ts. Math.fround
 * staging is preserved by storing settings as float.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_AAS_REACHABILITY_TYPES_H
#define QA_BOTS_NAVIGATION_AAS_REACHABILITY_TYPES_H
#include <stdint.h>
#include "qa/contracts/math.h"
typedef struct qa_aas_movement_settings {
  qa_vec3 gravity_direction;
  float friction, stop_speed, gravity, water_friction, water_gravity;
  float max_velocity, max_walk_velocity, max_crouch_velocity, max_swim_velocity;
  float walk_accelerate, air_accelerate, swim_accelerate;
  float max_step, max_steepness, max_water_jump, max_barrier;
  float jump_velocity, fall_delta5, fall_delta10;
  float water_jump_time, teleport_time, barrier_jump_time, start_crouch_time;
  float start_grapple_time, start_walk_off_ledge_time, start_jump_time;
  float rocket_jump_time, bfg_jump_time, jump_pad_time, air_controlled_jump_pad_time;
  float func_bob_time, start_elevator_time, fall_damage5_time, fall_damage10_time;
  float max_fall_height, max_jump_fall_height;
} qa_aas_movement_settings;
typedef double (*qa_aas_lib_var_value)(void *ctx, const char *name, const char *default_value);
/* Zero-initializes like `new AasMovementSettings()`. */
void qa_aas_movement_settings_init(qa_aas_movement_settings *s);
/* Reads each lib var once, in TS source order; fround per value. */
void qa_init_aas_movement_settings(qa_aas_movement_settings *s, void *ctx, qa_aas_lib_var_value value);
/* DEFAULT_AAS_MOVEMENT_SETTINGS: init with Number(default) values. */
void qa_aas_default_movement_settings(qa_aas_movement_settings *s);
enum {
  QA_AAS_STOP_NONE = 0, QA_AAS_STOP_HIT_GROUND = 1, QA_AAS_STOP_LEAVE_GROUND = 2,
  QA_AAS_STOP_ENTER_WATER = 4, QA_AAS_STOP_ENTER_SLIME = 8, QA_AAS_STOP_ENTER_LAVA = 16,
  QA_AAS_STOP_HIT_GROUND_DAMAGE = 32, QA_AAS_STOP_GAP = 64, QA_AAS_STOP_TOUCH_JUMP_PAD = 128,
  QA_AAS_STOP_TOUCH_TELEPORTER = 256, QA_AAS_STOP_ENTER_AREA = 512, QA_AAS_STOP_HIT_GROUND_AREA = 1024,
  QA_AAS_STOP_HIT_BOUNDING_BOX = 2048, QA_AAS_STOP_TOUCH_CLUSTER_PORTAL = 4096
};
typedef struct qa_aas_client_move {
  qa_vec3 end, velocity; int64_t end_area, frames, stop_event; double time;
} qa_aas_client_move;
#endif

/* Port of src/bots/behavior/q3/movement-state.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_MOVEMENT_STATE_H
#define QA_BOTS_BEHAVIOR_Q3_MOVEMENT_STATE_H
#include "qa/bots/behavior/library/libvars.h"
#include "qa/bots/behavior/library/memory.h"
#include "qa/contracts/math.h"
#include "qa/persistence/value.h"
#include <stdbool.h>
#include <stdint.h>
/* A binary32 vector cell of a source record. Store through qa_bot_fv3. */
typedef struct qa_bot_fvec3 {
  float x, y, z;
} qa_bot_fvec3;
static inline qa_vec3 qa_bot_v3(qa_bot_fvec3 v) { return (qa_vec3){v.x, v.y, v.z}; }
static inline qa_bot_fvec3 qa_bot_fv3(qa_vec3 v) {
  return (qa_bot_fvec3){(float)v.x, (float)v.y, (float)v.z};
}
#define QA_MAX_MOVE_STATES 64
#define QA_MAX_AVOID_REACH 1
#define QA_MAX_AVOID_SPOTS 32
typedef enum qa_bot_move_type {
  QA_BOT_MOVE_WALK = 1,
  QA_BOT_MOVE_CROUCH = 2,
  QA_BOT_MOVE_JUMP = 4,
  QA_BOT_MOVE_GRAPPLE = 8,
  QA_BOT_MOVE_ROCKETJUMP = 16,
  QA_BOT_MOVE_BFGJUMP = 32
} qa_bot_move_type;
typedef enum qa_bot_move_flag {
  QA_BOT_MF_BARRIERJUMP = 1,
  QA_BOT_MF_ONGROUND = 2,
  QA_BOT_MF_SWIMMING = 4,
  QA_BOT_MF_AGAINSTLADDER = 8,
  QA_BOT_MF_WATERJUMP = 16,
  QA_BOT_MF_TELEPORTED = 32,
  QA_BOT_MF_GRAPPLEPULL = 64,
  QA_BOT_MF_ACTIVEGRAPPLE = 128,
  QA_BOT_MF_GRAPPLERESET = 256,
  QA_BOT_MF_WALK = 512
} qa_bot_move_flag;
typedef enum qa_bot_move_result_flag {
  QA_BOT_MRF_MOVEMENTVIEW = 1,
  QA_BOT_MRF_SWIMVIEW = 2,
  QA_BOT_MRF_WAITING = 4,
  QA_BOT_MRF_MOVEMENTVIEWSET = 8,
  QA_BOT_MRF_MOVEMENTWEAPON = 16,
  QA_BOT_MRF_ONTOPOFOBSTACLE = 32,
  QA_BOT_MRF_ONTOPOF_FUNCBOB = 64,
  QA_BOT_MRF_ONTOPOF_ELEVATOR = 128,
  QA_BOT_MRF_BLOCKEDBYAVOIDSPOT = 256
} qa_bot_move_result_flag;
typedef enum qa_bot_move_result_type {
  QA_BOT_MRT_ELEVATORUP = 1,
  QA_BOT_MRT_WAITFORFUNCBOBBING = 2,
  QA_BOT_MRT_BADGRAPPLEPATH = 4,
  QA_BOT_MRT_INSOLIDAREA = 8
} qa_bot_move_result_type;
typedef enum qa_bot_avoid_spot_type {
  QA_AVOID_CLEAR = 0,
  QA_AVOID_ALWAYS = 1,
  QA_AVOID_DONTBLOCK = 2
} qa_bot_avoid_spot_type;
typedef struct qa_bot_init_move {
  qa_vec3 origin, velocity, view_offset;
  int64_t entity_num, client;
  double think_time;
  int64_t presence_type;
  qa_vec3 view_angles;
  int64_t or_move_flags;
} qa_bot_init_move;
typedef struct qa_bot_move_result {
  bool failure;
  int64_t type;
  bool blocked;
  int64_t block_entity, travel_type, flags, weapon;
  qa_vec3 move_direction, ideal_view_angles;
} qa_bot_move_result;
/* Source avoid-spot cell: 20 bytes. */
typedef struct qa_bot_avoid_spot {
  qa_bot_fvec3 origin;
  float radius;
  int32_t type;
} qa_bot_avoid_spot;
/* bot_movestate_t release32: 772 bytes in one BotMemory heap allocation. Fields
 * hold what TS DataView stores. Offsets are asserted in movement_state.c. Write
 * last_reachability through qa_bot_move_state_set_last_reachability (TS setter). */
typedef struct qa_bot_move_record {
  qa_bot_fvec3 origin, velocity, view_offset;
  int32_t entity_num, client;
  float think_time;
  int32_t presence_type;
  qa_bot_fvec3 view_angles;
  int32_t area, last_area, last_goal_area, last_reachability;
  qa_bot_fvec3 last_origin;
  int32_t reach_area, move_flags, jump_reach;
  float grapple_visible_time, last_grapple_distance, reachability_time;
  int32_t avoid_reach;
  float avoid_reach_time;
  int32_t avoid_reach_tries;
  qa_bot_avoid_spot avoid_spots[QA_MAX_AVOID_SPOTS];
  int32_t num_avoid_spots;
} qa_bot_move_record;
/* TS BotMoveState. src borrows the allocation; TS throws on any access after the
 * memory is disposed, and C aborts for that only through qa_bot_move_state_record. */
typedef struct qa_bot_move_state {
  qa_bot_move_record *src;
  qa_bot_memory *memory;
  qa_bot_memory_allocation allocation;
  /* TS walkProgress {edge, phase:"traverse"}; edges compare by graph id. */
  bool has_walk_progress;
  int64_t walk_edge_id;
} qa_bot_move_state;
/* TS constructor: restored NULL allocates a cleared heap record in memory. */
void qa_bot_move_state_init(qa_bot_move_state *state, qa_bot_memory *memory,
                            const qa_bot_memory_allocation *restored);
qa_bot_move_record *qa_bot_move_state_record(const qa_bot_move_state *state);
void qa_bot_move_state_set_last_reachability(qa_bot_move_state *state, int64_t value);
void qa_bot_move_state_reset(qa_bot_move_state *state);
void qa_bot_move_state_free(qa_bot_move_state *state);
int32_t qa_bot_move_state_native_reset_last_avoid_probe(const qa_bot_move_state *state);
/* TS BotMoveStateHost; lib_var is TS libVar and returns the live record. */
typedef struct qa_bot_move_state_host {
  void *ctx;
  double (*time)(void *ctx);
  void (*print)(void *ctx, int severity, const char *text); /* 1|3|4 */
  qa_bot_lib_var *(*lib_var)(void *ctx, const char *name, const char *default_value);
} qa_bot_move_state_host;
/* TS BotMoveVariable fields of the store are live records (NULL before setup). */
typedef struct qa_bot_move_state_store {
  qa_bot_move_state states[QA_MAX_MOVE_STATES + 1];
  bool allocated[QA_MAX_MOVE_STATES + 1];
  qa_bot_move_state_host host;
  qa_bot_memory owned_memory;
  qa_bot_memory *memory;
  qa_bot_lib_var *sv_max_step, *sv_max_barrier, *sv_gravity, *rocket_launcher_index, *bfg_index,
      *grapple_index, *missile_entity_type, *offhand_grapple, *grapple_on_command,
      *grapple_off_command;
} qa_bot_move_state_store;
/* Default state: owned BotMemory, no host. The struct must not move. */
void qa_bot_move_state_store_init(qa_bot_move_state_store *s);
/* TS constructor arguments; memory NULL keeps the owned default. */
void qa_bot_move_state_store_bind(qa_bot_move_state_store *s, qa_bot_move_state_host host,
                                  qa_bot_memory *memory);
void qa_bot_move_state_store_destroy(qa_bot_move_state_store *s);
int64_t qa_bot_move_state_store_allocate(qa_bot_move_state_store *s);
qa_bot_move_state *qa_bot_move_state_store_from_handle(qa_bot_move_state_store *s, int64_t handle);
void qa_bot_move_state_store_free(qa_bot_move_state_store *s, int64_t handle);
void qa_bot_move_state_store_initialize(qa_bot_move_state_store *s, int64_t handle,
                                        qa_bot_init_move input);
void qa_bot_move_state_store_reset(qa_bot_move_state_store *s, int64_t handle);
void qa_bot_move_state_store_reset_avoid_reach(qa_bot_move_state_store *s, int64_t handle);
void qa_bot_move_state_store_reset_last_avoid_reach(qa_bot_move_state_store *s, int64_t handle);
void qa_bot_move_state_store_add_to_avoid_reach(qa_bot_move_state_store *s,
                                                qa_bot_move_state *state, int64_t number,
                                                double avoid_time);
void qa_bot_move_state_store_add_avoid_spot(qa_bot_move_state_store *s, int64_t handle,
                                            qa_vec3 origin, double radius, int64_t type);
int64_t qa_bot_move_state_store_setup(qa_bot_move_state_store *s);
void qa_bot_move_state_store_shutdown(qa_bot_move_state_store *s);
void qa_bot_move_result_init(qa_bot_move_result *r);
/* Owned checkpoint tree. */
qa_save_value *qa_bot_move_state_store_checkpoint(qa_bot_move_state_store *s,
                                                  qa_bot_memory_capture *memory,
                                                  qa_bot_lib_vars *variables);
/* TS edge(client,id) lookup: true when the graph owns the edge. */
typedef bool (*qa_bot_move_edge_fn)(void *ctx, int64_t client, int64_t id);
void qa_bot_move_state_store_restore(qa_bot_move_state_store *s, const qa_save_value *value,
                                     qa_bot_memory_restored *memory, qa_bot_lib_vars *variables,
                                     qa_bot_move_edge_fn edge, void *edge_ctx);
#endif

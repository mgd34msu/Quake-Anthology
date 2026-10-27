/* Port of src/bots/behavior/q3/travel/types.ts (types only). Interfaces
 * become fn+ctx tables. BotMoveVariable / ServerTraceResult are mirrors
 * of movement-state.ts / content q3 world.ts records not yet landed as C
 * headers; NavigationRuntime (navigation/runtime.ts, bots-b unported) is
 * an opaque handle here. bots-a's *_bots_a tables are narrower subsets.
 * GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_Q3_TRAVEL_TYPES_H
#define QA_BOTS_BEHAVIOR_Q3_TRAVEL_TYPES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/behavior/library/actions.h"
#include "qa/bots/behavior/library/weights.h"
#include "qa/bots/behavior/q3/movement_state.h"
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/types.h"
#include "qa/contracts/scene.h"
typedef struct qa_bot_travel_trace { qa_trace_query query; qa_trace_result result; } qa_bot_travel_trace;
typedef struct qa_bot_travel_prediction_result {
  qa_vec3 end, velocity; double frames, stop_event;
  bool has_end_area; double end_area; /* TS number|null */
  const qa_vec3 *trajectory; size_t trajectory_count;
  double seconds; bool grounded; double water_level;
  bool has_trace; qa_bot_travel_trace trace;
  bool has_bounds; qa_bounds bounds; /* TS Bounds|null */
} qa_bot_travel_prediction_result;
/* kind: "elevator"|"bobbing"|"door"|"train"|"static" (exact strings). */
typedef struct qa_bot_travel_model {
  double entity; qa_vec3 origin; qa_bounds bounds; const char *kind;
} qa_bot_travel_model;
typedef struct qa_bot_move_variable { const char *string; double value; } qa_bot_move_variable;
/* solidity: "clear"|"start-solid"|"all-solid" (exact strings). */
typedef struct qa_bot_server_trace_result {
  double fraction; qa_vec3 end; double entity_num;
  const char *solidity; bool has_plane; qa_plane plane;
  double contents, surface_flags;
} qa_bot_server_trace_result;
typedef struct qa_source_bot_travel_host {
  qa_bot_action_buffer *actions; qa_bot_move_state_store *move_states; qa_bot_random random;
  void *ctx;
  /* Pick<BotNavigation,...> plus fuzzy/reachability area queries. */
  qa_bot_navigation_area (*area)(void *ctx, int64_t number);
  int64_t (*point_area)(void *ctx, qa_vec3 origin);
  /* trace_areas / presence_bounds / swimming mirror BotNavigation. */
  int64_t (*trace_areas)(void *ctx, qa_vec3 start, qa_vec3 end, int64_t *areas, qa_vec3 *points, int64_t max_areas);
  qa_bounds (*presence_bounds)(void *ctx, int64_t presence);
  bool (*swimming)(void *ctx, qa_vec3 origin);
  int64_t (*fuzzy_point_reachability_area)(void *ctx, qa_vec3 origin);
  int64_t (*reachability_area)(void *ctx, qa_vec3 origin, int64_t client);
  void *(*runtime)(void *ctx, int64_t client); /* NavigationRuntime handle */
  double (*time)(void *ctx);
  int64_t (*point_contents)(void *ctx, qa_vec3 point);
  /* bounds NULL = TS null. */
  qa_bot_server_trace_result (*trace)(void *ctx, qa_vec3 start, qa_vec3 end, const qa_bounds *bounds, int64_t pass_entity, int64_t mask);
  int64_t (*entity_model_index)(void *ctx, int64_t entity);
  bool (*model_info)(void *ctx, int64_t model, qa_bot_travel_model *out); /* false = TS null */
  int64_t (*next_entity)(void *ctx, int64_t after);
  int64_t (*entity_type)(void *ctx, int64_t entity);
  int64_t (*entity_weapon)(void *ctx, int64_t entity);
  /* mode: "rocket-jump"|"bfg-jump"|"grapple"; false = TS null. */
  bool (*travel_weapon)(void *ctx, int64_t client, const char *mode, int64_t *out);
  qa_bot_travel_prediction_result (*predict)(void *ctx, qa_bot_movement_prediction query);
} qa_source_bot_travel_host;
/* AasAsset reachability record plus its graph edge. */
typedef struct qa_travel_reachability {
  qa_aas_reach_rec reach; qa_navigation_edge graph_edge;
} qa_travel_reachability;
typedef enum qa_bot_movement_variable_name {
  QA_BMV_SV_MAX_STEP, QA_BMV_SV_MAX_BARRIER, QA_BMV_SV_GRAVITY, QA_BMV_ROCKET_LAUNCHER_INDEX,
  QA_BMV_BFG_INDEX, QA_BMV_GRAPPLE_INDEX, QA_BMV_MISSILE_ENTITY_TYPE, QA_BMV_OFFHAND_GRAPPLE,
  QA_BMV_GRAPPLE_ON_COMMAND, QA_BMV_GRAPPLE_OFF_COMMAND, QA_BMV_COUNT
} qa_bot_movement_variable_name;
const char *qa_bot_movement_variable_name_string(qa_bot_movement_variable_name name);
typedef struct qa_bot_travel_jump_speed { bool success; double velocity; } qa_bot_travel_jump_speed;
typedef struct qa_bot_travel_air_control { bool controlled; qa_vec3 direction; double speed; } qa_bot_travel_air_control;
typedef struct qa_bot_travel_context {
  qa_source_bot_travel_host *host; qa_bot_action_buffer *actions;
  void *ctx;
  qa_bot_move_variable (*variable)(void *ctx, qa_bot_movement_variable_name name);
  double (*time)(void *ctx);
  void (*print)(void *ctx, int64_t severity, const char *text); /* severity 1|3|4 */
  int64_t (*area_presence)(void *ctx, int64_t area);
  int64_t (*point_area)(void *ctx, qa_vec3 point);
  int64_t (*point_contents)(void *ctx, qa_vec3 point);
  bool (*reachability)(void *ctx, int64_t number, qa_travel_reachability *out); /* false = TS null */
  bool (*model_info)(void *ctx, int64_t model, qa_bot_travel_model *out);
  bool (*on_mover)(void *ctx, qa_bot_move_state *state, qa_travel_reachability reach);
  bool (*mover_down)(void *ctx, qa_travel_reachability reach);
  int64_t (*weapon)(void *ctx, int64_t client, const char *mode);
  qa_vec3 (*vector_to_angles)(void *ctx, qa_vec3 direction);
  double (*gap_distance)(void *ctx, qa_vec3 origin, qa_vec3 direction, int64_t entity);
  bool (*check_barrier_jump)(void *ctx, qa_bot_move_state *state, qa_vec3 direction, double speed);
  void (*check_blocked)(void *ctx, qa_bot_move_state *state, qa_vec3 direction, bool bottom, qa_bot_move_result *result);
  qa_vec3 (*jump_run_start)(void *ctx, qa_bot_move_state *state, qa_travel_reachability reach);
  qa_bot_travel_jump_speed (*jump_speed)(void *ctx, qa_bot_move_state *state, qa_vec3 start, qa_vec3 end, double initial_vertical_velocity);
  qa_bot_travel_air_control (*air_control)(void *ctx, qa_bot_move_state *state, qa_vec3 goal);
} qa_bot_travel_context;
#endif

/*
 * Port of src/compat/qvm/bot-navigation-syscalls.ts.
 * qa_qvmx_bot_navigation mirrors bots/behavior/q3/navigation.ts
 * SourceBotNavigation flattened to the used surface (bots module
 * unported); qa_qvmx_aas_bsp_entities mirrors bsp-entities.ts.
 * List results are malloc'd by the adapter and freed by the
 * dispatcher. GPL-2.0-or-later.
 */
#ifndef QA_COMPAT_QVM_BOT_NAVIGATION_SYSCALLS_H
#define QA_COMPAT_QVM_BOT_NAVIGATION_SYSCALLS_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/compat/qvm/bot_library_syscalls.h"
#include "qa/compat/qvm/bot_navigation_records.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"

typedef struct qa_qvm_aas_movement_prediction {
  bool success;
  qa_vec3 end;
  double end_area;
  qa_vec3 velocity;
  struct {
    bool start_solid;
    double fraction;
    qa_vec3 end;
    double entity_num, last_area, area, plane;
  } trace;
  double presence, stop_event, end_contents, time, frames;
} qa_qvm_aas_movement_prediction;

typedef struct qa_qvmx_bot_movement_prediction_query {
  double entity_num;
  qa_vec3 origin;
  double presence;
  bool on_ground;
  qa_vec3 velocity, command_move;
  double command_frames, max_frames, frame_time, stop_events, stop_area;
  bool visualize;
} qa_qvmx_bot_movement_prediction_query;

typedef struct qa_qvmx_navigation_node {
  double id;
  qa_bounds bounds;
  qa_vec3 origin;
  bool is_aas;
  double flags;
} qa_qvmx_navigation_node;

typedef struct qa_qvmx_navigation_area {
  double contents, flags, presence_type, cluster, reachable_area_count;
} qa_qvmx_navigation_area;

typedef struct qa_qvmx_area_crossing { double area; qa_vec3 point; } qa_qvmx_area_crossing;
typedef struct qa_qvmx_alternative_route_goal {
  qa_vec3 origin;
  double area, start_travel_time, goal_travel_time, extra_travel_time;
} qa_qvmx_alternative_route_goal;

typedef struct qa_qvmx_predict_route_query {
  double area;
  qa_vec3 origin;
  double goal_area, travel_flags, maximum_areas, maximum_time, stop_event, stop_contents;
  double stop_travel_flags, stop_area;
} qa_qvmx_predict_route_query;
typedef struct qa_qvmx_predict_route_result {
  qa_vec3 end_position;
  double end_area, stop_event, end_contents, end_travel_flags, time;
  bool succeeded;
} qa_qvmx_predict_route_result;

typedef struct qa_qvmx_travel_query {
  double area;
  bool has_origin;
  qa_vec3 origin;
  double goal_area, travel_flags;
} qa_qvmx_travel_query;

typedef struct qa_qvmx_host_prediction_trace {
  qa_vec3 query_start;
  bool start_solid;
  double fraction;
  qa_vec3 end;
  bool hit_is_actor;
  qa_actor_id hit_actor;
  double plane_distance;
  qa_vec3 plane_normal;
} qa_qvmx_host_prediction_trace;

typedef struct qa_qvmx_host_prediction {
  double stop_event, frames, seconds;
  qa_vec3 end, velocity;
  bool has_end_area;
  double end_area;
  bool has_bounds;
  qa_bounds bounds;
  bool has_trace;
  qa_qvmx_host_prediction_trace trace;
} qa_qvmx_host_prediction;

typedef struct qa_qvmx_aas_plane { double distance; qa_vec3 normal; } qa_qvmx_aas_plane;

typedef struct qa_qvmx_bot_navigation {
  void *ctx;
  bool (*node)(void *ctx, double id, qa_qvmx_navigation_node *out);
  bool (*enable_area)(void *ctx, double id, bool enabled);
  bool (*area_enabled)(void *ctx, double id, bool *out_enabled); /* false = not in checkpoint */
  double *(*bbox_areas)(void *ctx, qa_bounds bounds, size_t *out_count); /* malloc'd */
  qa_qvmx_navigation_area (*area)(void *ctx, double area);
  double (*point_area)(void *ctx, qa_vec3 point);
  qa_qvmx_area_crossing *(*trace_areas)(void *ctx, qa_vec3 start, qa_vec3 end, double max,
                                        size_t *out_count); /* malloc'd */
  double (*point_contents)(void *ctx, qa_vec3 point);
  qa_bounds (*presence_bounds)(void *ctx, double presence);
  double (*area_travel_time_to_goal)(void *ctx, qa_qvmx_travel_query query);
  bool (*swimming)(void *ctx, qa_vec3 point);
  void (*move_to_goal)(void *ctx, qa_qvm_bot_move_result_ref result, double state,
                       qa_qvm_bot_goal_ref goal, double travel_flags);
  bool (*move_in_direction)(void *ctx, double state, qa_vec3 direction, float speed, double type);
  double (*reachability_area)(void *ctx, qa_vec3 origin, double test_ground);
  bool (*movement_view_target)(void *ctx, double state, qa_qvm_bot_goal_ref goal, double chain,
                               float time, qa_qvm_bot_movement_target_ref target);
  bool (*predict_visible_position)(void *ctx, qa_vec3 origin, double eye, qa_qvm_bot_goal_ref goal,
                                   double mode, qa_qvm_bot_movement_target_ref target);
  qa_qvmx_alternative_route_goal *(*alternative_route_goals)(
      void *ctx, qa_vec3 start, double start_area, qa_vec3 goal, double goal_area,
      double travel_flags, double maximum_goals, double type, size_t *out_count); /* malloc'd */
  qa_qvmx_predict_route_result (*predict_route)(void *ctx, qa_qvmx_predict_route_query query);
  /* graph surface for trap 577 and movement prediction */
  bool (*graph_is_aas)(void *ctx);
  double (*graph_node_count)(void *ctx);
  double *(*reachable_node_ids)(void *ctx, size_t *out_count); /* malloc'd */
  double *(*cluster_reachability_counts)(void *ctx, size_t *out_count); /* malloc'd */
  bool (*area_settings)(void *ctx, double area, double *out_cluster, double *out_cluster_area);
  qa_qvmx_aas_plane *(*aas_planes)(void *ctx, size_t *out_count); /* malloc'd; NULL when not AAS */
  qa_qvmx_host_prediction (*predict_client_movement)(void *ctx,
                                                      qa_qvmx_bot_movement_prediction_query query);
} qa_qvmx_bot_navigation;

typedef struct qa_qvm_bsp_key_fn {
  void *ctx;
  bool (*matches)(void *ctx, const char *candidate);
} qa_qvm_bsp_key_fn;

typedef struct qa_qvm_bsp_output_io {
  qa_qvm_writable_view view;
  void *ctx;
  void (*clear)(void *ctx, int64_t length);
} qa_qvm_bsp_output_io;

typedef struct qa_qvmx_bsp_vector_result { qa_vec3 value; bool found; } qa_qvmx_bsp_vector_result;
typedef struct qa_qvmx_bsp_float_result { double value; bool found; } qa_qvmx_bsp_float_result;

typedef struct qa_qvmx_aas_bsp_entities {
  void *ctx;
  double (*next_entity)(void *ctx, double entity);
  bool (*value)(void *ctx, double entity, qa_qvm_bsp_key_fn key, qa_qvm_memory_span output,
                double size, qa_qvm_bsp_output_io io);
  qa_qvmx_bsp_vector_result (*vector)(void *ctx, double entity, qa_qvm_bsp_key_fn key);
  qa_qvmx_bsp_float_result (*get_float)(void *ctx, double entity, qa_qvm_bsp_key_fn key);
  qa_qvmx_bsp_float_result (*get_int)(void *ctx, double entity, qa_qvm_bsp_key_fn key);
} qa_qvmx_aas_bsp_entities;

typedef struct qa_qvm_bot_navigation_services {
  qa_qvmx_bot_navigation navigation;
  qa_qvmx_bot_library *library;
  qa_qvmx_aas_bsp_entities bsp_entities;
  void *ctx;
  double (*time)(void *ctx);
  bool (*initialized)(void *ctx);
  bool (*entity_info)(void *ctx, double number, qa_qvm_aas_entity_info *out);
  qa_qvm_aas_movement_prediction (*predict_client_movement)(
      void *ctx, qa_qvmx_bot_movement_prediction_query query);
} qa_qvm_bot_navigation_services;

typedef struct qa_qvm_entity_number_fn {
  void *ctx;
  double (*call)(void *ctx, qa_actor_id actor);
} qa_qvm_entity_number_fn;

qa_qvm_aas_movement_prediction qa_qvm_bot_movement_prediction(
    qa_qvmx_bot_navigation *navigation, qa_qvmx_bot_movement_prediction_query query,
    qa_qvm_entity_number_fn entity_number);

bool qa_qvm_bot_navigation_syscall(const qa_qvm_host_call *call,
                                   qa_qvm_bot_navigation_services *services,
                                   qa_qvm_host_result *out);

#endif

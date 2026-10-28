/* Port of src/bots/navigation/aas-reachability.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_AAS_REACHABILITY_H
#define QA_BOTS_NAVIGATION_AAS_REACHABILITY_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/behavior/q3/navigation_types.h"
#include "qa/bots/behavior/q3/travel/types.h"
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/aas_reachability_types.h"
#include "qa/bots/navigation/types.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"

typedef struct qa_aas_reachability_world {
  qa_aas_asset asset; /* settings points to the owned copy; other arrays borrowed from source */
  const qa_aas_asset *source;
} qa_aas_reachability_world;
int64_t qa_aas_reachability_world_point_area(const qa_aas_reachability_world *world, qa_vec3 point);
/* TS world.setting(area) via aasAt: aborts with the RangeError text. */
qa_aas_setting_rec *qa_aas_reachability_world_setting(qa_aas_reachability_world *world, int64_t area);

typedef struct qa_aas_reachability_debug_entry { char *category; int64_t count; } qa_aas_reachability_debug_entry;
typedef struct qa_aas_reachability_debug_state {
  qa_aas_reachability_debug_entry *entries; size_t count, cap;
} qa_aas_reachability_debug_state;
void qa_aas_reachability_debug_count(qa_aas_reachability_debug_state *state, const char *category);
void qa_aas_reachability_debug_state_destroy(qa_aas_reachability_debug_state *state);

/* 48-byte source link record; accessors mirror the TS DataView fields
 * (i32 at 0/4/8/36, f32 vectors at 12/24, u16 travel time at 40). */
typedef struct qa_aas_linked_reachability {
  uint8_t bytes[48];
  struct qa_aas_linked_reachability *next;
} qa_aas_linked_reachability;
int64_t qa_aas_link_area(const qa_aas_linked_reachability *link);
void qa_aas_link_set_area(qa_aas_linked_reachability *link, int64_t value);
int64_t qa_aas_link_face(const qa_aas_linked_reachability *link);
void qa_aas_link_set_face(qa_aas_linked_reachability *link, int64_t value);
int64_t qa_aas_link_edge(const qa_aas_linked_reachability *link);
void qa_aas_link_set_edge(qa_aas_linked_reachability *link, int64_t value);
qa_vec3 qa_aas_link_start(const qa_aas_linked_reachability *link);
void qa_aas_link_set_start(qa_aas_linked_reachability *link, qa_vec3 value);
qa_vec3 qa_aas_link_end(const qa_aas_linked_reachability *link);
void qa_aas_link_set_end(qa_aas_linked_reachability *link, qa_vec3 value);
int64_t qa_aas_link_travel_type(const qa_aas_linked_reachability *link);
void qa_aas_link_set_travel_type(qa_aas_linked_reachability *link, int64_t value);
int64_t qa_aas_link_travel_time(const qa_aas_linked_reachability *link);
/* Out-of-int32-range times abort with the TS RangeError text. */
void qa_aas_link_set_travel_time(qa_aas_linked_reachability *link, double value);
void qa_aas_link_clear(qa_aas_linked_reachability *link);

struct qa_aas_reachability_spatial;
struct qa_aas_reachability_entities;
typedef double (*qa_aas_reachability_variable_fn)(void *ctx, const char *name, const char *default_value);
typedef void (*qa_aas_reachability_print_fn)(void *ctx, int64_t severity, const char *text);
typedef void (*qa_aas_reachability_debug_line_fn)(void *ctx, qa_vec3 start, qa_vec3 end, double color);
typedef struct qa_aas_reachability_context {
  qa_aas_reachability_world *world;
  struct qa_aas_reachability_spatial *spatial;
  struct qa_aas_reachability_entities *bsp_entities;
  const qa_aas_movement_settings *settings;
  void *variable_ctx; qa_aas_reachability_variable_fn variable;
  bool debug;
  qa_aas_reachability_debug_state debug_state;
  qa_aas_linked_reachability **heads; size_t head_count;
  qa_aas_linked_reachability *free_list;
  int64_t allocated;
  void *print_ctx; qa_aas_reachability_print_fn print;
  void *debug_line_ctx; qa_aas_reachability_debug_line_fn permanent_line;
} qa_aas_reachability_context;
void qa_aas_reachability_context_print(qa_aas_reachability_context *context, int64_t severity, const char *text);
void qa_aas_reachability_context_log(qa_aas_reachability_context *context, const char *text);
void qa_aas_reachability_context_permanent_line(qa_aas_reachability_context *context, qa_vec3 start, qa_vec3 end, double color);
double qa_aas_reachability_context_variable(qa_aas_reachability_context *context, const char *name, const char *initial);
/* NULL at the 65536 cap (after the TS print) or on OOM. */
qa_aas_linked_reachability *qa_aas_reachability_context_allocate(qa_aas_reachability_context *context);
void qa_aas_reachability_context_free_link(qa_aas_reachability_context *context, qa_aas_linked_reachability *link);
bool qa_aas_reachability_context_exists(qa_aas_reachability_context *context, int64_t from, int64_t to);

typedef qa_bot_travel_prediction_result (*qa_aas_predict_client_movement_fn)(void *ctx, qa_bot_movement_prediction query);
typedef struct qa_aas_reachability_options {
  const qa_aas_asset *asset;
  qa_decoded_world geometry;
  qa_scene_queries scene;
  qa_navigation_profile profile;
  int64_t prediction_client;
  void *predict_ctx; qa_aas_predict_client_movement_fn predict_client_movement;
  bool force, debug;
  bool has_settings; qa_aas_movement_settings settings;
  void *variable_ctx; qa_aas_reachability_variable_fn variable; /* NULL = Number(default) */
  void *print_ctx; qa_aas_reachability_print_fn print;           /* NULL = TS noop */
  void *debug_line_ctx; qa_aas_reachability_debug_line_fn debug_line;
} qa_aas_reachability_options;
/* Frees only the replaced settings/reachability arrays of a built result. */
void qa_aas_reachability_result_destroy(qa_aas_asset *asset);
/* TS buildAasReachability. When the input already has reachabilities and
 * force is false, *out_reused is set and *out is left untouched (the TS
 * returns the input asset itself). Otherwise *out is a shallow copy of
 * the input with owned settings/reachability replacements. False only on
 * OOM; a bad prediction client aborts with the TS RangeError text. */
bool qa_build_aas_reachability(qa_aas_asset *out, bool *out_reused, qa_aas_reachability_options options);
#endif

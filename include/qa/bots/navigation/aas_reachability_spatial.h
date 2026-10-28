/* Port of src/bots/navigation/aas-reachability-spatial.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_NAVIGATION_AAS_REACHABILITY_SPATIAL_H
#define QA_BOTS_NAVIGATION_AAS_REACHABILITY_SPATIAL_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/bots/navigation/aas.h"
#include "qa/bots/navigation/aas_reachability.h"
#include "qa/bots/navigation/aas_reachability_types.h"
#include "qa/contracts/math.h"
#include "qa/contracts/scene.h"
#include "qa/formats/q3_map/entities.h"

typedef struct qa_aas_reachability_entities {
  qa_q3_entity *records; size_t count;
} qa_aas_reachability_entities;
/* Parse errors and >=2048 records abort with the TS error text. False only on OOM. */
bool qa_aas_reachability_entities_init(qa_aas_reachability_entities *entities, const char *text);
void qa_aas_reachability_entities_destroy(qa_aas_reachability_entities *entities);
int64_t qa_aas_reachability_entities_next(qa_aas_reachability_entities *entities, int64_t previous);
/* TS value(entity,key,Uint8Array): empty output aborts. */
bool qa_aas_reachability_entities_value(qa_aas_reachability_entities *entities, int64_t entity, const char *key,
                                       uint8_t *output, size_t output_length);
/* Return value is `found`; *out is 0 when not found. */
bool qa_aas_reachability_entities_int(qa_aas_reachability_entities *entities, int64_t entity, const char *key, int64_t *out);
bool qa_aas_reachability_entities_float(qa_aas_reachability_entities *entities, int64_t entity, const char *key, double *out);
bool qa_aas_reachability_entities_vector(qa_aas_reachability_entities *entities, int64_t entity, const char *key, qa_vec3 *out);

typedef struct qa_aas_trace {
  bool start_solid; double fraction; qa_vec3 end;
  int64_t entity_num, last_area, area, plane;
} qa_aas_trace;
typedef struct qa_aas_host_trace {
  bool start_solid; double fraction; qa_vec3 end; double surface_flags;
} qa_aas_host_trace;
typedef struct qa_aas_area_link {
  int64_t area;
  struct qa_aas_area_link *next_area;
} qa_aas_area_link;
void qa_aas_area_link_free(qa_aas_area_link *head);
typedef struct qa_aas_jump_speed { bool success; double velocity; } qa_aas_jump_speed;
typedef struct qa_aas_best_area { int64_t area; qa_vec3 origin; } qa_aas_best_area;
typedef struct qa_aas_jump_pad_info { qa_vec3 start; qa_bounds bounds; qa_vec3 velocity; } qa_aas_jump_pad_info;

typedef struct qa_aas_reachability_spatial {
  qa_aas_reachability_options options;
  qa_aas_reachability_world *world;
  qa_aas_reachability_entities *entities;
  const qa_aas_movement_settings *settings;
} qa_aas_reachability_spatial;
void qa_aas_reachability_spatial_init(qa_aas_reachability_spatial *spatial, qa_aas_reachability_options options,
                                      qa_aas_reachability_world *world, qa_aas_reachability_entities *entities,
                                      const qa_aas_movement_settings *settings);
double qa_aas_spatial_point_contents(qa_aas_reachability_spatial *spatial, qa_vec3 point);
/* has_bounds false = TS null bounds (point shape). */
qa_aas_host_trace qa_aas_spatial_trace(qa_aas_reachability_spatial *spatial, qa_vec3 start, qa_vec3 end,
                                       bool has_bounds, qa_bounds bounds, int64_t pass_entity, double mask);
/* Out-of-range model aborts with the TS RangeError text. */
void qa_aas_spatial_model_bounds(qa_aas_reachability_spatial *spatial, int64_t model, qa_vec3 angles,
                                 qa_bounds *out_bounds, qa_vec3 *out_origin);
/* Missing AAS end area aborts with the TS Error text. */
qa_aas_client_move qa_aas_spatial_predict_client_movement(qa_aas_reachability_spatial *spatial, qa_bot_movement_prediction request);
qa_aas_jump_speed qa_aas_spatial_horizontal_velocity_for_jump(qa_aas_reachability_spatial *spatial, double z_velocity, qa_vec3 start, qa_vec3 end);
double qa_aas_spatial_rocket_jump_z_velocity(qa_aas_reachability_spatial *spatial, qa_vec3 origin);
/* Caller frees the returned array. */
qa_aas_area_crossing *qa_aas_spatial_trace_areas(qa_aas_reachability_spatial *spatial, qa_vec3 start, qa_vec3 end,
                                                 int64_t maximum, size_t *out_count);
qa_aas_trace qa_aas_spatial_trace_client_bbox(qa_aas_reachability_spatial *spatial, qa_vec3 start, qa_vec3 end,
                                              int64_t presence, int64_t pass_entity);
/* Caller frees with qa_aas_area_link_free. Missing presence bounds abort. */
qa_aas_area_link *qa_aas_spatial_link_client_bounds(qa_aas_reachability_spatial *spatial, int64_t entity, qa_bounds bounds, int64_t presence);
qa_aas_best_area qa_aas_spatial_best_reachable_area(qa_aas_reachability_spatial *spatial, qa_vec3 origin, qa_bounds bounds);
bool qa_aas_spatial_drop_to_floor(qa_aas_reachability_spatial *spatial, qa_vec3 origin, qa_bounds bounds, qa_vec3 *out_origin);
bool qa_aas_spatial_point_inside_face(qa_aas_reachability_spatial *spatial, int64_t number, qa_vec3 point, double epsilon);
/* False = TS null. */
bool qa_aas_spatial_get_jump_pad_info(qa_aas_reachability_spatial *spatial, int64_t entity, qa_aas_jump_pad_info *out);
#endif

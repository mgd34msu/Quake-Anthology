/* Port of src/app/bootstrap/simulation/q3/guest-spatial.ts over landed
 * scene queries/records. adjust_area_portal_state is the guest-world
 * hook (world struct above fills it). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_SPATIAL_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_SPATIAL_H
#include "qa/app/bootstrap/simulation/q3/guest_records.h"
#include "qa/app/bootstrap/simulation/q3/guest_world.h"
#include "qa/compat/qvm/server_game_syscalls.h"
typedef struct qa_app_q3_guest_spatial_dependencies {
  qa_shared_scene_queries *scene;
  qa_app_q3_guest_records *records;
  qa_app_q3_guest_world *world; /* area portal pair adjust */
  void *ctx;
  void (*collision)(void *ctx, qa_owned_actor actor, qa_actor_collision collision);
} qa_app_q3_guest_spatial_dependencies;
typedef struct qa_app_q3_guest_spatial qa_app_q3_guest_spatial;
qa_app_q3_guest_spatial *qa_app_q3_guest_spatial_create(qa_app_q3_guest_spatial_dependencies dependencies);
void qa_app_q3_guest_spatial_destroy(qa_app_q3_guest_spatial *spatial);
qa_qvm_server_spatial_operations qa_app_q3_guest_spatial_operations(qa_app_q3_guest_spatial *spatial);
qa_qvm_trace_record qa_app_q3_guest_spatial_trace(qa_app_q3_guest_spatial *spatial, const qa_qvm_server_trace_query *input);
int32_t qa_app_q3_guest_spatial_point_contents(qa_app_q3_guest_spatial *spatial, qa_vec3 point, int32_t pass_entity_num);
size_t qa_app_q3_guest_spatial_area_entities(qa_app_q3_guest_spatial *spatial, qa_bounds bounds, int32_t maximum, int32_t *out, size_t capacity);
bool qa_app_q3_guest_spatial_entity_contact(qa_app_q3_guest_spatial *spatial, qa_bounds bounds, int32_t slot, bool capsule);
void qa_app_q3_guest_spatial_set_brush_model(qa_app_q3_guest_spatial *spatial, int32_t slot, const char *name);
void qa_app_q3_guest_spatial_adjust_area_portal_state(qa_app_q3_guest_spatial *spatial, int32_t slot, bool open);
bool qa_app_q3_guest_spatial_in_pvs(qa_app_q3_guest_spatial *spatial, qa_vec3 first, qa_vec3 second, bool ignore_portals);
bool qa_app_q3_guest_spatial_areas_connected(qa_app_q3_guest_spatial *spatial, int32_t first, int32_t second);
/* TS modelBounds(index): scene.modelBounds(index). */
qa_bounds qa_app_q3_guest_spatial_model_bounds(qa_app_q3_guest_spatial *spatial, double index);
#endif

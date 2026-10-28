/* Port of src/app/bootstrap/simulation/q3/guest-world.ts over landed
 * qa_shared_scene_queries. Capture returns an owned qa_save_value.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_GUEST_WORLD_H
#include "qa/app/bootstrap/simulation/q3/types.h"
#include "qa/persistence/value.h"
#include "qa/world/collision/index.h"
typedef struct qa_app_q3_guest_world qa_app_q3_guest_world;
qa_app_q3_guest_world *qa_app_q3_guest_world_create(qa_shared_scene_queries *scene);
void qa_app_q3_guest_world_destroy(qa_app_q3_guest_world *world);
qa_shared_scene_queries *qa_app_q3_guest_world_scene(qa_app_q3_guest_world *world);
void qa_app_q3_guest_world_adjust_area_portal_state(qa_app_q3_guest_world *world, double first, double second, bool open);
qa_save_value *qa_app_q3_guest_world_capture_portal_checkpoint(qa_app_q3_guest_world *world);
void qa_app_q3_guest_world_close(qa_app_q3_guest_world *world);
void qa_app_q3_guest_world_restore_portal_checkpoint(qa_app_q3_guest_world *world, const qa_save_value *value);
#endif

/* Port of src/compat/qc/movement-host.ts. GPL-2.0-or-later.
 * Movement state lives in caller-owned storage (monster movement +
 * services). Touch callback is a fn+ctx struct. */
#ifndef QA_COMPAT_QC_MOVEMENT_HOST_H
#define QA_COMPAT_QC_MOVEMENT_HOST_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/compat/qc/world_host.h"
#include "qa/movement/q1/monsters.h"
typedef struct qa_compat_qc_movement_services {
  qa_scene_queries scene; qa_random_source random; void *ctx;
  void (*touch_triggers)(void *ctx, qa_owned_actor actor);
  bool (*actor_reference)(void *ctx, int64_t reference, qa_actor_id *out); /* nullable */
} qa_compat_qc_movement_services;
typedef struct qa_compat_qc_movement_state_compat {
  qa_compat_qc_world_host *world; qa_compat_qc_movement_services services;
  qa_q1_monster_movement movement; qa_numeric_operations numeric;
} qa_compat_qc_movement_state_compat;
typedef struct qa_compat_qc_movement_bindings { qa_compat_qc_host_binding entries[4]; size_t count; } qa_compat_qc_movement_bindings;
qa_compat_qc_movement_bindings qa_compat_qc_create_movement_bindings(qa_compat_qc_world_host *world, qa_compat_qc_movement_services services, qa_compat_qc_movement_state_compat *storage);
typedef struct qa_compat_qc_touch_state {
  qa_compat_qc_world_host *world; qa_compat_qc_machine *vm; void *ctx;
  double (*server_time)(void *ctx); int64_t solid, touch;
} qa_compat_qc_touch_state;
typedef struct qa_compat_qc_touch_callback { qa_compat_qc_touch_state *state; } qa_compat_qc_touch_callback;
qa_compat_qc_touch_callback qa_compat_qc_create_touch_callback(qa_compat_qc_world_host *world, qa_compat_qc_machine *vm, void *ctx, double (*server_time)(void *ctx), qa_compat_qc_touch_state *storage);
void qa_compat_qc_touch_callback_call(qa_compat_qc_touch_callback cb, qa_touch_contact contact);
#endif

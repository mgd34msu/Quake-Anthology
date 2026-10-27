/* Port of src/compat/qc/pusher-host.ts. GPL-2.0-or-later.
 * Returned services' ctx is caller-owned state; physical() result
 * is stored in that state. foreign read/write are fn+ctx. */
#ifndef QA_COMPAT_QC_PUSHER_HOST_H
#define QA_COMPAT_QC_PUSHER_HOST_H
#include <stdbool.h>
#include "qa/compat/qc/world_host.h"
#include "qa/movement/q1/types.h"
typedef struct qa_compat_qc_pusher_projection {
  void *ctx;
  bool (*read)(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out);
  void (*write)(void *ctx, qa_q1_physics_entity entity);
  void (*link)(void *ctx, qa_owned_actor actor, bool touch);
  void (*blocked)(void *ctx, qa_owned_actor pusher, qa_actor_id obstacle);
} qa_compat_qc_pusher_projection;
typedef struct qa_compat_qc_pusher_state {
  qa_compat_qc_world_host *world; qa_compat_qc_machine *vm; void *ctx;
  qa_q1_pusher_services (*physical)(void *ctx, qa_compat_qc_pusher_projection projection);
  bool (*foreign_read)(void *ctx, qa_actor_id actor, qa_q1_physics_entity *out);
  void (*foreign_write)(void *ctx, qa_q1_physics_entity entity);
  void (*touch_triggers)(void *ctx, qa_owned_actor actor);
  double (*server_time)(void *ctx);
  void (*invoke)(void *ctx, qa_owned_actor actor, const char *callback, const qa_actor_id *other); /* nullable */
  qa_q1_pusher_services inner;
} qa_compat_qc_pusher_state;
qa_q1_pusher_services qa_compat_qc_create_pusher_services(qa_compat_qc_world_host *world, qa_compat_qc_machine *vm, qa_compat_qc_pusher_state *storage);
#endif

/* Port of src/content/q3/base/world-adapter.ts. GPL-2.0-or-later.
 * The landed game/state entity-shared mirror has no bounds-override
 * or collision-model index storage: link() skips clearBoundsOverrides
 * and passes model 0 for inline shapes; captureLink folds the linked
 * bounds into the mirror absmin/absmax/linkcount fields. */
#ifndef QA_CONTENT_Q3_BASE_WORLD_ADAPTER_H
#define QA_CONTENT_Q3_BASE_WORLD_ADAPTER_H
#include <stdbool.h>
#include <stddef.h>
#include "qa/content/q3/base/game/state.h"
#include "qa/content/q3/base/records.h"
#include "qa/content/q3/base/world.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/world/actors/body.h"
#include "qa/world/collision/index.h"

/* TS Q3WorldAdapterHost. */
typedef struct qa_q3_base_world_adapter_host {
  qa_shared_scene_queries *queries;
  qa_shared_body_table *bodies;
  void *ctx;
  void (*collision)(void *ctx, qa_owned_actor actor, qa_actor_collision collision);
  bool (*curves)(void *ctx);
  bool (*player_curve_clip)(void *ctx);
} qa_q3_base_world_adapter_host;

/* TS Q3WorldAdapter (implements ServerWorld + ActorSpatialQueries). */
typedef struct qa_q3_base_world_adapter {
  qa_q3_base_world_adapter_host host;
  qa_q3_base_entity_records *records;
  qa_actor_id *scratch; size_t scratch_capacity;
  double *entity_scratch; size_t entity_scratch_capacity;
} qa_q3_base_world_adapter;
void qa_q3_base_world_adapter_init(qa_q3_base_world_adapter *adapter,
    qa_q3_base_world_adapter_host host, qa_q3_base_entity_records *records);
void qa_q3_base_world_adapter_destroy(qa_q3_base_world_adapter *adapter);

qa_q3_base_server_trace_result qa_q3_base_world_adapter_trace(qa_q3_base_world_adapter *adapter,
    qa_q3_base_server_trace_query input);
qa_q3_base_actor_trace_result qa_q3_base_world_adapter_trace_actor(qa_q3_base_world_adapter *adapter,
    qa_q3_base_actor_trace_query input);
/* Borrowed scratch arrays, invalidated by the next area call. */
const qa_actor_id *qa_q3_base_world_adapter_area_actors(qa_q3_base_world_adapter *adapter,
    qa_bounds bounds, double maximum, size_t *out_count);
const double *qa_q3_base_world_adapter_area_entities(qa_q3_base_world_adapter *adapter,
    qa_bounds bounds, bool has_maximum, double maximum, size_t *out_count);
double qa_q3_base_world_adapter_point_contents(qa_q3_base_world_adapter *adapter,
    qa_vec3 point, double pass_entity_num);
/* False is TS undefined. */
bool qa_q3_base_world_adapter_link_state(qa_q3_base_world_adapter *adapter, double number,
    qa_q3_base_link_state *out);
void qa_q3_base_world_adapter_link(qa_q3_base_world_adapter *adapter, qa_q3_game_entity *entity);
void qa_q3_base_world_adapter_unlink(qa_q3_base_world_adapter *adapter, double number);
/* TS unlinkActor's returned closure (NULL struct when TS null):
 * call any number of times, then destroy. */
typedef struct qa_q3_base_relink_closure qa_q3_base_relink_closure;
qa_q3_base_relink_closure *qa_q3_base_world_adapter_unlink_actor(qa_q3_base_world_adapter *adapter,
    qa_actor_id actor);
void qa_q3_base_relink_closure_call(qa_q3_base_relink_closure *closure);
void qa_q3_base_relink_closure_destroy(qa_q3_base_relink_closure *closure);
bool qa_q3_base_world_adapter_entity_contact(qa_q3_base_world_adapter *adapter,
    qa_bounds bounds, double number, bool capsule);
bool qa_q3_base_world_adapter_contact_actor(qa_q3_base_world_adapter *adapter,
    qa_bounds bounds, qa_actor_id actor, bool capsule);
/* Landed world.h fn tables over this adapter. */
qa_q3_base_server_world qa_q3_base_world_adapter_server_world(qa_q3_base_world_adapter *adapter);
qa_q3_base_actor_spatial_queries qa_q3_base_world_adapter_spatial_queries(qa_q3_base_world_adapter *adapter);
#endif

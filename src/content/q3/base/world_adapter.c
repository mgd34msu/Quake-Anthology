/* Port of src/content/q3/base/world-adapter.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/world_adapter.h"

#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"
#include "qa/world/collision/body.h"

void qa_q3_base_world_adapter_init(qa_q3_base_world_adapter *adapter,
    qa_q3_base_world_adapter_host host, qa_q3_base_entity_records *records) {
  adapter->host = host;
  adapter->records = records;
  adapter->scratch = NULL;
  adapter->scratch_capacity = 0;
  adapter->entity_scratch = NULL;
  adapter->entity_scratch_capacity = 0;
}
void qa_q3_base_world_adapter_destroy(qa_q3_base_world_adapter *adapter) {
  free(adapter->scratch);
  free(adapter->entity_scratch);
  adapter->scratch = NULL;
  adapter->entity_scratch = NULL;
}

/* TS private actor(number): false is TS null. */
static bool actor_for_number(qa_q3_base_world_adapter *adapter, double number, qa_actor_id *out) {
  if (number == 1022 || number == 1023 || number < 0) return false;
  qa_q3_game_entity *entity = qa_q3_base_entity_records_get(adapter->records, number);
  if (entity == NULL || !qa_q3_game_entity_inuse(entity)) return false;
  *out = qa_q3_game_entity_actor(entity).id;
  return true;
}
static qa_trace_query build_query(qa_q3_base_world_adapter *adapter, qa_q3_base_actor_trace_query input) {
  qa_trace_query query;
  memset(&query, 0, sizeof query);
  query.start = input.start;
  query.end = input.end;
  query.target.is_model = false;
  query.shape.kind = input.shape.kind == QA_Q3_BASE_SHAPE_POINT ? "point"
      : input.shape.kind == QA_Q3_BASE_SHAPE_CAPSULE ? "capsule" : "box";
  query.shape.bounds.min = input.shape.mins;
  query.shape.bounds.max = input.shape.maxs;
  query.pass_actor = input.pass_actor;
  query.numeric = qa_q3_binary32_profile;
  query.policy.kind = "q3";
  query.policy.contents_mask = input.mask;
  query.policy.curves = adapter->host.curves(adapter->host.ctx);
  query.policy.player_curve_clip = adapter->host.player_curve_clip(adapter->host.ctx);
  return query;
}

qa_q3_base_actor_trace_result qa_q3_base_world_adapter_trace_actor(qa_q3_base_world_adapter *adapter,
    qa_q3_base_actor_trace_query input) {
  qa_trace_result result = qa_shared_trace(adapter->host.queries, build_query(adapter, input));
  if (strcmp(result.kind, "q3") != 0) qa_range_error("Shared scene did not adapt a Q3 trace policy");
  qa_q3_base_actor_trace_result out;
  memset(&out, 0, sizeof out);
  out.fraction = result.fraction;
  out.end = result.end;
  out.solidity = result.all_solid ? QA_Q3_BASE_SOLID_ALL_SOLID
      : result.start_solid ? QA_Q3_BASE_SOLID_START_SOLID : QA_Q3_BASE_SOLID_CLEAR;
  out.contact_is_plane = result.contact.has_plane;
  out.contact_plane = result.contact.plane;
  out.contents = result.contents;
  out.surface_flags = result.surface_flags;
  out.hit_kind = result.hit.kind == QA_TRACE_HIT_ACTOR ? QA_Q3_BASE_HIT_ACTOR
      : result.hit.kind == QA_TRACE_HIT_WORLD ? QA_Q3_BASE_HIT_WORLD : QA_Q3_BASE_HIT_NONE;
  out.hit_actor = result.hit.actor;
  return out;
}

qa_q3_base_server_trace_result qa_q3_base_world_adapter_trace(qa_q3_base_world_adapter *adapter,
    qa_q3_base_server_trace_query input) {
  qa_actor_id pass;
  bool has_pass = actor_for_number(adapter, input.pass_entity_num, &pass);
  qa_q3_base_actor_trace_query actor_input = {input.start, input.end, input.shape, input.mask,
      has_pass ? &pass : NULL};
  qa_q3_base_actor_trace_result result = qa_q3_base_world_adapter_trace_actor(adapter, actor_input);
  double entity_num = result.hit_kind == QA_Q3_BASE_HIT_NONE ? 1023 : 1022;
  if (result.hit_kind == QA_Q3_BASE_HIT_ACTOR) {
    qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(adapter->records, true, result.hit_actor);
    if (entity != NULL) entity_num = entity->slot;
  }
  qa_q3_base_server_trace_result out;
  memset(&out, 0, sizeof out);
  out.fraction = result.fraction;
  out.end = result.end;
  out.entity_num = entity_num;
  out.solidity = result.solidity;
  out.contact_is_plane = result.contact_is_plane;
  out.contact_plane = result.contact_plane;
  out.contents = result.contents;
  out.surface_flags = result.surface_flags;
  return out;
}

const qa_actor_id *qa_q3_base_world_adapter_area_actors(qa_q3_base_world_adapter *adapter,
    qa_bounds bounds, double maximum, size_t *out_count) {
  size_t count = 0;
  const qa_spatial_actor *actors = qa_shared_query_actors(adapter->host.queries, bounds, "both", &count);
  size_t take = count;
  if (maximum >= 0 && take > (size_t)maximum) take = (size_t)maximum;
  if (take > adapter->scratch_capacity) {
    qa_actor_id *grown = realloc(adapter->scratch, take * sizeof *grown);
    if (!grown && take) qa_range_error("out of memory");
    adapter->scratch = grown;
    adapter->scratch_capacity = take;
  }
  for (size_t i = 0; i < take; i++) adapter->scratch[i] = actors[i].body.actor;
  *out_count = take;
  return adapter->scratch;
}
const double *qa_q3_base_world_adapter_area_entities(qa_q3_base_world_adapter *adapter,
    qa_bounds bounds, bool has_maximum, double maximum, size_t *out_count) {
  size_t count = 0;
  const qa_actor_id *actors = qa_q3_base_world_adapter_area_actors(adapter, bounds,
      has_maximum ? maximum : 1024, &count);
  if (count > adapter->entity_scratch_capacity) {
    double *grown = realloc(adapter->entity_scratch, count * sizeof *grown);
    if (!grown && count) qa_range_error("out of memory");
    adapter->entity_scratch = grown;
    adapter->entity_scratch_capacity = count;
  }
  size_t out = 0;
  for (size_t i = 0; i < count; i++) {
    qa_q3_game_entity *entity = qa_q3_base_entity_records_by_actor(adapter->records, true, actors[i]);
    if (entity != NULL) adapter->entity_scratch[out++] = entity->slot;
  }
  *out_count = out;
  return adapter->entity_scratch;
}

double qa_q3_base_world_adapter_point_contents(qa_q3_base_world_adapter *adapter,
    qa_vec3 point, double pass_entity_num) {
  qa_actor_id pass;
  bool has_pass = actor_for_number(adapter, pass_entity_num, &pass);
  qa_q3_base_actor_trace_query input;
  memset(&input, 0, sizeof input);
  input.start = point;
  input.end = point;
  input.shape.kind = QA_Q3_BASE_SHAPE_POINT;
  input.pass_actor = has_pass ? &pass : NULL;
  input.mask = -1;
  qa_trace_query base = build_query(adapter, input);
  qa_point_contents_query query = {point, base.target, base.policy, base.numeric, base.pass_actor};
  qa_point_contents_result result = qa_shared_point_contents(adapter->host.queries, query);
  if (strcmp(result.kind, "q3") != 0) qa_range_error("Shared scene did not adapt Q3 point contents");
  return result.contents;
}

bool qa_q3_base_world_adapter_link_state(qa_q3_base_world_adapter *adapter, double number,
    qa_q3_base_link_state *out) {
  qa_actor_id actor;
  if (!actor_for_number(adapter, number, &actor)) return false;
  qa_linked_body_result linked = qa_shared_body_table_linked(adapter->host.bodies, actor);
  if (!linked.has_value) return false;
  out->linked = true;
  out->linkcount = linked.value.link_count;
  out->absbounds = linked.value.absolute_bounds;
  return true;
}

/* Mirror captureLink: fold the current link into the r fields. */
static void capture_link(qa_q3_base_world_adapter *adapter, qa_q3_game_entity *entity) {
  qa_linked_body_result linked = qa_shared_body_table_linked(adapter->host.bodies,
      qa_q3_game_entity_actor(entity).id);
  if (!linked.has_value) return;
  entity->r.linked = true;
  entity->r.linkcount = linked.value.link_count;
  entity->r.absmin = linked.value.absolute_bounds.min;
  entity->r.absmax = linked.value.absolute_bounds.max;
}

static double solid_byte(double value) {
  double truncated = trunc(value);
  return truncated < 1 ? 1 : truncated > 255 ? 255 : truncated;
}
void qa_q3_base_world_adapter_link(qa_q3_base_world_adapter *adapter, qa_q3_game_entity *entity) {
  bool inline_model = entity->r.model_kind == 0; /* shared MODEL_INLINE */
  if (inline_model) entity->s.solid = 0xffffff;
  else if (((int32_t)entity->r.contents & (1 | 0x2000000)) == 0) entity->s.solid = 0;
  else {
    int32_t packed = ((int32_t)solid_byte((float)(entity->r.maxs.z + 32)) << 16) |
        ((int32_t)solid_byte(-entity->r.mins.z) << 8) | (int32_t)solid_byte(entity->r.maxs.x);
    entity->s.solid = packed;
  }
  qa_actor_collision collision;
  memset(&collision, 0, sizeof collision);
  collision.family = "q3";
  collision.shape_kind = inline_model ? "model" : entity->r.model_kind == 2 ? "capsule" : "box";
  collision.shape_model = 0; /* mirror has no collision-model index */
  collision.contents = entity->r.contents;
  qa_actor_id owner;
  bool has_owner = actor_for_number(adapter, entity->r.owner_num, &owner);
  collision.owner = has_owner ? &owner : NULL;
  collision.role = entity->r.contents == 0x40000000 ? "trigger" : "solid";
  qa_owned_actor actor = qa_q3_game_entity_actor(entity);
  adapter->host.collision(adapter->host.ctx, actor, collision);
  qa_shared_body_table_link(adapter->host.bodies, actor, &entity->r.current_origin);
  capture_link(adapter, entity);
}

void qa_q3_base_world_adapter_unlink(qa_q3_base_world_adapter *adapter, double number) {
  qa_q3_game_entity *entity = qa_q3_base_entity_records_get(adapter->records, number);
  if (entity != NULL && qa_q3_game_entity_inuse(entity)) {
    capture_link(adapter, entity);
    qa_shared_body_table_unlink(adapter->host.bodies, qa_q3_game_entity_actor(entity));
  }
}

struct qa_q3_base_relink_closure {
  qa_q3_base_world_adapter *adapter;
  qa_owned_actor owner;
  qa_q3_game_entity *native;
};
qa_q3_base_relink_closure *qa_q3_base_world_adapter_unlink_actor(qa_q3_base_world_adapter *adapter,
    qa_actor_id actor) {
  const qa_q3_base_record_host *host = qa_q3_base_entity_records_host(adapter->records);
  qa_owned_actor owner;
  if (!qa_session_actor_registry_resolve_owned(host->actors, actor, &owner)) return NULL;
  if (!qa_shared_body_table_linked(adapter->host.bodies, actor).has_value) return NULL;
  qa_q3_game_entity *native = qa_q3_base_entity_records_native_by_actor(adapter->records, true, actor);
  if (native != NULL) capture_link(adapter, native);
  qa_shared_body_table_unlink(adapter->host.bodies, owner);
  qa_q3_base_relink_closure *closure = malloc(sizeof *closure);
  if (!closure) qa_range_error("out of memory");
  closure->adapter = adapter;
  closure->owner = owner;
  closure->native = native;
  return closure;
}
void qa_q3_base_relink_closure_call(qa_q3_base_relink_closure *closure) {
  qa_q3_base_world_adapter *adapter = closure->adapter;
  const qa_q3_base_record_host *host = qa_q3_base_entity_records_host(adapter->records);
  if (!qa_session_actor_registry_is_live(host->actors, closure->owner.id) ||
      !qa_shared_body_table_read(adapter->host.bodies, closure->owner.id).has_value) return;
  if (closure->native != NULL &&
      qa_q3_base_entity_records_native_by_actor(adapter->records, true, closure->owner.id) == closure->native)
    qa_q3_base_world_adapter_link(adapter, closure->native);
  else
    qa_shared_body_table_link(adapter->host.bodies, closure->owner, NULL);
}
void qa_q3_base_relink_closure_destroy(qa_q3_base_relink_closure *closure) { free(closure); }

bool qa_q3_base_world_adapter_entity_contact(qa_q3_base_world_adapter *adapter,
    qa_bounds bounds, double number, bool capsule) {
  qa_q3_game_entity *entity = qa_q3_base_entity_records_get(adapter->records, number);
  if (entity == NULL || !qa_q3_game_entity_inuse(entity)) return false;
  return qa_q3_base_world_adapter_contact_actor(adapter, bounds, qa_q3_game_entity_actor(entity).id, capsule);
}
bool qa_q3_base_world_adapter_contact_actor(qa_q3_base_world_adapter *adapter,
    qa_bounds bounds, qa_actor_id actor, bool capsule) {
  qa_spatial_actor spatial;
  if (!qa_spatial_index_get(adapter->host.queries->spatial, actor, &spatial)) return false;
  qa_body_state_result body = qa_shared_body_table_read(adapter->host.bodies, actor);
  if (!body.has_value) return false;
  qa_q3_base_actor_trace_query input;
  memset(&input, 0, sizeof input);
  input.shape.kind = capsule ? QA_Q3_BASE_SHAPE_CAPSULE : QA_Q3_BASE_SHAPE_BOX;
  input.shape.mins = bounds.min;
  input.shape.maxs = bounds.max;
  input.mask = -1;
  qa_trace_query query = build_query(adapter, input);
  if (strcmp(spatial.collision.shape_kind, "model") == 0) {
    query.target.is_model = true;
    query.target.model = spatial.collision.shape_model;
    query.target.origin = body.value.origin;
    query.target.angles = body.value.angles;
    return qa_shared_geometry_trace(adapter->host.queries, query).start_solid;
  }
  qa_spatial_actor probe = spatial;
  probe.body.state = body.value;
  return qa_trace_actor_body(query, probe).start_solid;
}

/* Landed world.h tables. */
static bool table_entity_contact(void *ctx, qa_bounds bounds, double entity_num, bool capsule) {
  return qa_q3_base_world_adapter_entity_contact(ctx, bounds, entity_num, capsule);
}
static qa_q3_base_server_trace_result table_trace(void *ctx, qa_q3_base_server_trace_query query) {
  return qa_q3_base_world_adapter_trace(ctx, query);
}
static const double *table_area_entities(void *ctx, qa_bounds bounds, bool has_maximum, double maximum, size_t *out_count) {
  return qa_q3_base_world_adapter_area_entities(ctx, bounds, has_maximum, maximum, out_count);
}
static double table_point_contents(void *ctx, qa_vec3 point, double pass_entity_num) {
  return qa_q3_base_world_adapter_point_contents(ctx, point, pass_entity_num);
}
static bool table_link_state(void *ctx, double number, qa_q3_base_link_state *out) {
  return qa_q3_base_world_adapter_link_state(ctx, number, out);
}
static void table_link(void *ctx, qa_q3_game_entity *entity) { qa_q3_base_world_adapter_link(ctx, entity); }
static void table_unlink(void *ctx, double number) { qa_q3_base_world_adapter_unlink(ctx, number); }
qa_q3_base_server_world qa_q3_base_world_adapter_server_world(qa_q3_base_world_adapter *adapter) {
  qa_q3_base_server_world world = {adapter, table_entity_contact, table_trace, table_area_entities,
      table_point_contents, table_link_state, table_link, table_unlink};
  return world;
}
static const qa_actor_id *table_area_actors(void *ctx, qa_bounds bounds, double maximum, size_t *out_count) {
  return qa_q3_base_world_adapter_area_actors(ctx, bounds, maximum, out_count);
}
static qa_q3_base_actor_trace_result table_trace_actor(void *ctx, qa_q3_base_actor_trace_query query) {
  return qa_q3_base_world_adapter_trace_actor(ctx, query);
}
static bool table_contact_actor(void *ctx, qa_bounds bounds, qa_actor_id actor, bool capsule) {
  return qa_q3_base_world_adapter_contact_actor(ctx, bounds, actor, capsule);
}
qa_q3_base_actor_spatial_queries qa_q3_base_world_adapter_spatial_queries(qa_q3_base_world_adapter *adapter) {
  qa_q3_base_actor_spatial_queries queries = {adapter, table_area_actors, table_trace_actor, table_contact_actor};
  return queries;
}

/* Port of src/compat/qvm/bot-navigation-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/bot_navigation_syscalls.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/bots/navigation/aas.h"
#include "qa/core/numeric.h"
#include "qa/foundation/js.h"
#include "qa/foundation/support.h"

static qa_navigation_runtime *host_runtime(qa_source_bot_navigation_bots_a *navigation) { return navigation->host.runtime; }
static const qa_aas_asset *aas_asset(qa_source_bot_navigation_bots_a *navigation) {
  return host_runtime(navigation)->graph->aas_asset;
}

qa_qvm_aas_movement_prediction qa_qvm_bot_movement_prediction(qa_source_bot_navigation_bots_a *navigation,
                                                              qa_bot_movement_prediction query,
                                                              qa_qvm_entity_number_fn entity_number) {
  double frame_time = query.frame_time <= 0 ? (double)qa_float32(0.1) : query.frame_time;
  query.frame_time = frame_time;
  qa_source_bot_navigation_host_bots_a *host = &navigation->host;
  qa_bot_travel_prediction_result result = host->predict_client_movement(host->ctx, query);
  qa_qvm_aas_movement_prediction prediction;
  memset(&prediction, 0, sizeof prediction);
  int32_t stop_event = qa_js_to_int32(result.stop_event);
  if (result.has_trace && result.stop_event != 0 && (stop_event & (4 | 8 | 16)) == 0) {
    const qa_trace_result *collision = &result.trace.result;
    const qa_aas_asset *asset = aas_asset(navigation);
    const qa_plane *plane = &collision->source_plane.plane;
    double plane_index = -1;
    if (asset != NULL)
      for (size_t i = 0; i < asset->plane_count; i++) {
        const qa_aas_plane_rec *candidate = &asset->planes[i];
        if (candidate->distance == plane->distance && candidate->normal.x == plane->normal.x &&
            candidate->normal.y == plane->normal.y && candidate->normal.z == plane->normal.z) {
          plane_index = (double)i;
          break;
        }
      }
    size_t crossing_count = 0;
    qa_bot_area_crossing *crossings = qa_source_bot_navigation_trace_areas(
        navigation, result.trace.query.start, collision->end, (int64_t)host_runtime(navigation)->graph->node_count + 1,
        &crossing_count);
    prediction.trace.start_solid = collision->start_solid;
    prediction.trace.fraction = collision->fraction;
    prediction.trace.end = collision->end;
    prediction.trace.entity_num =
        collision->hit.kind == QA_TRACE_HIT_ACTOR ? entity_number.call(entity_number.ctx, collision->hit.actor) : 0;
    prediction.trace.last_area = crossing_count > 0
                                     ? (double)crossings[crossing_count - 1].area
                                     : (double)qa_source_bot_navigation_point_area(navigation, result.trace.query.start);
    free(crossings);
    prediction.trace.area =
        collision->fraction == 1 ? 0 : (double)qa_source_bot_navigation_point_area(navigation, collision->end);
    prediction.trace.plane = qa_js_max(0, plane_index);
  }
  qa_bounds crouched = qa_source_bot_navigation_presence_bounds(navigation, 4);
  prediction.presence = !result.has_bounds ? (double)query.presence
                        : result.bounds.max.z - result.bounds.min.z <= crouched.max.z - crouched.min.z ? 4
                                                                                                      : 2;
  prediction.success = result.stop_event != 0 || result.frames >= qa_js_max(0, (double)query.max_frames);
  prediction.end = result.end;
  prediction.end_area = result.has_end_area ? result.end_area : (double)qa_source_bot_navigation_point_area(navigation, result.end);
  prediction.velocity = result.velocity;
  prediction.stop_event = result.stop_event;
  prediction.end_contents = (stop_event & (4 | 8 | 16)) != 0 ? (double)host->point_contents(host->ctx, result.end) : 0;
  prediction.time = result.stop_event == 0 ? result.seconds : qa_js_max(0, result.seconds - frame_time);
  prediction.frames = result.stop_event == 0 ? result.frames : qa_js_max(0, result.frames - 1);
  return prediction;
}

typedef struct dispatch {
  const qa_qvm_host_call *call;
  const qa_qvm_game_memory *memory;
} dispatch;

static int32_t wint(const dispatch *d, size_t index) { return qa_qvm_host_call_word(d->call, index); }
static float wfloat(const dispatch *d, size_t index) { return qa_qvm_host_call_word_f32(d->call, index * 4); }

/* TS () => memory.pointer(integer(index)). */
typedef struct pointer_ctx { const dispatch *d; size_t index; } pointer_ctx;
static qa_qvmx_view pointer_view(void *ctx) {
  pointer_ctx *pointer = ctx;
  qa_qvm_game_region span;
  qa_qvmx_view view = {NULL, 0};
  if (qa_qvm_game_memory_pointer(pointer->d->memory, wint(pointer->d, pointer->index), &span)) {
    view.bytes = span.bytes;
    view.length = span.length;
  }
  return view;
}
/* TS fieldView(bytes, offset) = memory.regionView(bytes, offset, 4). */
static qa_qvmx_view field_view(void *ctx, qa_qvmx_view bytes, double offset) {
  const dispatch *d = ctx;
  qa_qvm_writable_view view;
  const qa_qvm_game_region region = {bytes.bytes, bytes.length, false};
  qa_qvm_game_memory_region_view(d->memory, &region, (int64_t)offset, 4, &view);
  qa_qvmx_view result = {view.bytes + view.start, (size_t)view.length};
  return result;
}
static qa_qvm_bot_pointer pointer_of(pointer_ctx *storage, const dispatch *d, size_t index) {
  storage->d = d;
  storage->index = index;
  qa_qvm_bot_pointer pointer = {storage, pointer_view};
  return pointer;
}
static qa_vec3 point(const dispatch *d, size_t index) {
  pointer_ctx storage;
  return qa_qvm_bot_movement_vector(pointer_of(&storage, d, index));
}
/* TS goal(index) = readQvmBotGoalReference(...); the C operations take the goal value. */
static qa_bot_goal goal(const dispatch *d, size_t index) {
  pointer_ctx storage;
  qa_qvm_bot_goal_ref ref = qa_qvm_read_bot_goal_reference(pointer_of(&storage, d, index));
  qa_qvmx_bot_goal record = qa_qvm_bot_goal_ref_read(&ref);
  qa_bot_goal value;
  memset(&value, 0, sizeof value);
  value.origin = record.origin;
  value.area = (int64_t)record.area;
  value.mins = record.mins;
  value.maxs = record.maxs;
  value.entity = (int64_t)record.entity;
  value.number = (int64_t)record.number;
  value.flags = (int64_t)record.flags;
  value.item_info = (int64_t)record.item_info;
  return value;
}
static void write_vector(qa_qvm_writable_view *view, int64_t offset, qa_vec3 value) {
  qa_qvm_writable_view_set_float32(view, offset, value.x, true);
  qa_qvm_writable_view_set_float32(view, offset + 4, value.y, true);
  qa_qvm_writable_view_set_float32(view, offset + 8, value.z, true);
}
static int64_t node_id(qa_source_bot_navigation_bots_a *navigation, int64_t area) {
  return aas_asset(navigation) != NULL ? area : area - 1;
}
/* TS runtime.checkpoint().enabled.find(entry => entry.id === id)?.enabled. */
static bool enabled_entry(const qa_navigation_runtime *runtime, int64_t id, bool *out) {
  for (size_t i = 0; i < runtime->enabled_count; i++)
    if (runtime->enabled[i].id == id) {
      *out = runtime->enabled[i].enabled;
      return true;
    }
  return false;
}

/* TS key(index)(candidate): byte comparison against the guest key string. */
typedef struct key_ctx { const dispatch *d; size_t index; } key_ctx;
static bool key_matches(void *ctx, const char *candidate) {
  key_ctx *key = ctx;
  qa_qvm_game_region span;
  if (!qa_qvm_game_memory_pointer(key->d->memory, wint(key->d, key->index), &span)) qa_range_error("QVM BSP epair key is null");
  size_t length = strlen(candidate);
  for (size_t i = 0; i <= length; i++) {
    if (i >= span.length) qa_range_error("QVM BSP epair key exceeds allocation");
    uint8_t byte = span.bytes[i];
    if (byte != (i == length ? 0 : (uint8_t)candidate[i])) return false;
  }
  return true;
}
/* TS { view: memory.regionView(output), clear: length => memory.regionFill(output, 0, length, 0) }. */
typedef struct output_ctx { const dispatch *d; qa_qvm_game_region output; } output_ctx;
static void output_set_byte(void *ctx, int64_t index, uint8_t value) {
  output_ctx *output = ctx;
  qa_qvm_writable_view view;
  qa_qvm_game_memory_region_view(output->d->memory, &output->output, 0, -1, &view);
  qa_qvm_writable_view_set_uint8(&view, index, value);
}
static void output_clear(void *ctx, int64_t length) {
  output_ctx *output = ctx;
  qa_qvm_game_memory_region_fill(output->d->memory, &output->output, 0, length, 0);
}

/* A target the operation may store: staged with a sentinel, written through the view when set. */
static qa_vec3 target_sentinel(void) {
  qa_vec3 value;
  memset(&value, 0xa5, sizeof value);
  return value;
}
static void store_target(const dispatch *d, size_t index, const qa_vec3 *value) {
  qa_vec3 sentinel = target_sentinel();
  if (memcmp(value, &sentinel, sizeof sentinel) == 0) return;
  pointer_ctx storage;
  qa_qvm_bot_field_view view = {(void *)d, field_view};
  qa_qvm_bot_movement_target_ref target = qa_qvm_bot_movement_target(pointer_of(&storage, d, index), view);
  qa_qvm_bot_movement_target_set(&target, *value);
}

bool qa_qvm_bot_navigation_syscall(const qa_qvm_host_call *call, qa_qvm_bot_navigation_services *services,
                                   qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") != 0) return false;
  const qa_qvm_game_memory guest_memory = qa_qvm_host_call_game_memory(call);
  const qa_qvm_game_memory *memory = &guest_memory;
  dispatch storage = {call, memory};
  const dispatch *d = &storage;
  qa_source_bot_navigation_bots_a *navigation = services->navigation;
  qa_bot_move_state_store *states = &services->library->move_states;
  qa_qvm_writable_view view;
  *out = 0;
  switch (call->code) {
    case 300: {
      qa_navigation_runtime *runtime = host_runtime(navigation);
      int32_t area = wint(d, 1);
      const qa_navigation_node *node = qa_navigation_runtime_node(runtime, node_id(navigation, area));
      if (area <= 0 || node == NULL) return true;
      if (wint(d, 2) >= 0) {
        *out = qa_navigation_runtime_enable_area(runtime, node->id, wint(d, 2) != 0) ? 1 : 0;
        return true;
      }
      bool enabled = false;
      if (!enabled_entry(runtime, node->id, &enabled)) enabled = !(node->source.kind == QA_NS_AAS && (node->flags & 8) != 0);
      *out = enabled ? 1 : 0;
      return true;
    }
    case 301: {
      qa_bounds bounds;
      bounds.min = point(d, 1);
      bounds.max = point(d, 2);
      size_t count = 0;
      int64_t *areas = qa_source_bot_navigation_bbox_areas(navigation, bounds, &count);
      int32_t maximum = wint(d, 4);
      size_t kept = maximum <= 0 ? 0 : count < (size_t)maximum ? count : (size_t)maximum;
      qa_cleanup cleanup;
      qa_cleanup_push(&cleanup, free, areas);
      for (size_t i = 0; i < kept; i++) {
        qa_qvm_game_memory_view(memory, wint(d, 3), 4, (int64_t)i * 4, &view);
        qa_qvm_writable_view_set_int32(&view, 0, qa_js_to_int32((double)areas[i]), true);
      }
      qa_cleanup_pop(&cleanup);
      free(areas);
      *out = (qa_qvm_host_result)kept;
      return true;
    }
    case 302: {
      qa_navigation_runtime *runtime = host_runtime(navigation);
      int32_t area = wint(d, 1);
      const qa_navigation_node *node = qa_navigation_runtime_node(runtime, node_id(navigation, area));
      if (wint(d, 2) == 0 || node == NULL || area <= 0) return true;
      qa_bot_navigation_area info = qa_source_bot_navigation_area(navigation, area);
      qa_qvm_game_memory_view(memory, wint(d, 2), 52, 0, &view);
      bool enabled = false;
      int32_t flags = qa_js_to_int32((double)info.flags);
      if (enabled_entry(runtime, node->id, &enabled)) flags = enabled ? flags & ~8 : flags | 8;
      qa_qvm_writable_view_set_int32(&view, 0, qa_js_to_int32((double)info.contents), true);
      qa_qvm_writable_view_set_int32(&view, 4, flags, true);
      qa_qvm_writable_view_set_int32(&view, 8, qa_js_to_int32((double)info.presence_type), true);
      qa_qvm_writable_view_set_int32(&view, 12, qa_js_to_int32((double)info.cluster), true);
      const qa_aas_asset *asset = runtime->graph->aas_asset;
      write_vector(&view, 16, node->bounds.min);
      write_vector(&view, 28, node->bounds.max);
      qa_vec3 center = node->origin;
      if (asset != NULL && area < (int64_t)asset->area_count) center = asset->areas[area].center;
      write_vector(&view, 40, center);
      *out = 52;
      return true;
    }
    case 303: {
      qa_qvm_aas_entity_info info;
      if (!services->entity_info(services->ctx, wint(d, 1), &info)) {
        const qa_qvm_game_region span = qa_qvm_game_memory_span(memory, wint(d, 2), 140, 0);
        qa_qvm_game_memory_region_fill(memory, &span, 0, 140, 0);
      } else {
        qa_qvm_game_memory_view(memory, wint(d, 2), 140, 0, &view);
        qa_qvmx_view target = {view.bytes + view.start, (size_t)view.length};
        qa_qvm_write_aas_entity_info(target, &info);
      }
      return true;
    }
    case 304: *out = services->initialized(services->ctx) ? 1 : 0; return true;
    case 305: {
      int32_t presence = wint(d, 1);
      if (presence != 2 && presence != 4)
        services->library->options.print(services->library->options.ctx, 4, "AAS_PresenceTypeBoundingBox: unknown presence type\n");
      qa_bounds bounds = qa_source_bot_navigation_presence_bounds(navigation, presence == 2 ? 2 : 4);
      qa_qvm_game_memory_view(memory, wint(d, 2), 12, 0, &view);
      write_vector(&view, 0, bounds.min);
      qa_qvm_game_memory_view(memory, wint(d, 3), 12, 0, &view);
      write_vector(&view, 0, bounds.max);
      return true;
    }
    case 306: *out = (int32_t)qa_float32_to_bits(services->time(services->ctx)); return true;
    case 307: *out = qa_js_to_int32((double)qa_source_bot_navigation_point_area(navigation, point(d, 1))); return true;
    case 308: {
      qa_qvm_game_memory_view(memory, wint(d, 3), 4, 0, &view);
      qa_qvm_writable_view_set_int32(&view, 0, 0, true);
      qa_vec3 start = point(d, 1), end = point(d, 2);
      int32_t maximum = wint(d, 5);
      size_t count = 0;
      qa_bot_area_crossing *crossings = qa_source_bot_navigation_trace_areas(navigation, start, end, maximum < 0 ? 0 : maximum, &count);
      qa_cleanup cleanup;
      qa_cleanup_push(&cleanup, free, crossings);
      for (size_t i = 0; i < count; i++) {
        qa_qvm_game_memory_view(memory, wint(d, 3), 4, (int64_t)i * 4, &view);
        qa_qvm_writable_view_set_int32(&view, 0, qa_js_to_int32((double)crossings[i].area), true);
        if (wint(d, 4) != 0) {
          qa_qvm_game_memory_view(memory, wint(d, 4), 12, (int64_t)i * 12, &view);
          write_vector(&view, 0, crossings[i].point);
        }
      }
      qa_cleanup_pop(&cleanup);
      free(crossings);
      *out = (qa_qvm_host_result)count;
      return true;
    }
    case 309:
      *out = qa_js_to_int32((double)navigation->host.point_contents(navigation->host.ctx, point(d, 1)));
      return true;
    case 310: *out = qa_js_to_int32((double)qa_aas_bsp_entities_next_entity(services->bsp_entities, wint(d, 1))); return true;
    case 311: {
      qa_qvm_game_region output;
      if (!qa_qvm_game_memory_pointer(memory, wint(d, 3), &output)) qa_range_error("QVM BSP epair output is null");
      key_ctx key_storage = {d, 2};
      qa_aas_bsp_key key = {key_matches, &key_storage};
      output_ctx output_storage = {d, output};
      qa_aas_bsp_writes writes = {&output_storage, output_set_byte, output_clear};
      *out = qa_aas_bsp_entities_value_ex(services->bsp_entities, wint(d, 1), NULL, &key, output.bytes,
                                          output.length, wint(d, 4), &writes)
                 ? 1
                 : 0;
      return true;
    }
    case 312: {
      qa_qvm_game_memory_view(memory, wint(d, 3), 12, 0, &view);
      qa_vec3 zero = {0, 0, 0};
      write_vector(&view, 0, zero);
      key_ctx key_storage = {d, 2};
      qa_aas_bsp_key key = {key_matches, &key_storage};
      qa_aas_bsp_vector_result result = qa_aas_bsp_entities_vector_key(services->bsp_entities, wint(d, 1), &key);
      write_vector(&view, 0, result.value);
      *out = result.found ? 1 : 0;
      return true;
    }
    case 313:
    case 314: {
      qa_qvm_game_memory_view(memory, wint(d, 3), 4, 0, &view);
      qa_qvm_writable_view_set_int32(&view, 0, 0, true);
      key_ctx key_storage = {d, 2};
      qa_aas_bsp_key key = {key_matches, &key_storage};
      qa_aas_bsp_number_result result = call->code == 313
                                            ? qa_aas_bsp_entities_float_key(services->bsp_entities, wint(d, 1), &key)
                                            : qa_aas_bsp_entities_int_key(services->bsp_entities, wint(d, 1), &key);
      if (call->code == 313) qa_qvm_writable_view_set_float32(&view, 0, result.value, true);
      else qa_qvm_writable_view_set_int32(&view, 0, qa_js_to_int32(result.value), true);
      *out = result.found ? 1 : 0;
      return true;
    }
    case 315:
      *out = qa_js_to_int32((double)qa_source_bot_navigation_area(navigation, wint(d, 1)).reachable_area_count);
      return true;
    case 316: {
      if (!services->initialized(services->ctx)) return true;
      qa_area_travel_time_query query;
      memset(&query, 0, sizeof query);
      query.area = wint(d, 1);
      query.has_origin = wint(d, 2) != 0;
      if (query.has_origin) query.origin = point(d, 2);
      query.goal_area = wint(d, 3);
      query.travel_flags = wint(d, 4);
      *out = qa_js_to_int32(qa_source_bot_navigation_area_travel_time_to_goal(navigation, query));
      return true;
    }
    case 317: *out = qa_source_bot_navigation_swimming(navigation, point(d, 1)) ? 1 : 0; return true;
    case 318: {
      int32_t presence = wint(d, 4);
      if (presence != 2 && presence != 4) {
        /* AAS_PresenceTypeBoundingBox only prints; no prediction runs, so the caller reads a cleared result. */
        services->library->options.print(services->library->options.ctx, 4, "AAS_PresenceTypeBoundingBox: unknown presence type\n");
        const qa_qvm_game_region span = qa_qvm_game_memory_span(memory, wint(d, 1), 84, 0);
        qa_qvm_game_memory_region_fill(memory, &span, 0, 84, 0);
        return true;
      }
      qa_bot_movement_prediction query;
      memset(&query, 0, sizeof query);
      query.entity_num = wint(d, 2);
      query.origin = point(d, 3);
      query.presence = presence;
      query.on_ground = wint(d, 5) != 0;
      query.velocity = point(d, 6);
      query.command_move = point(d, 7);
      query.command_frames = wint(d, 8);
      query.max_frames = wint(d, 9);
      query.frame_time = wfloat(d, 10);
      query.stop_events = wint(d, 11);
      query.stop_area = wint(d, 12);
      query.visualize = wint(d, 13) != 0;
      qa_qvm_aas_movement_prediction result = services->predict_client_movement(services->ctx, query);
      qa_qvm_game_memory_view(memory, wint(d, 1), 84, 0, &view);
      write_vector(&view, 0, result.end);
      qa_qvm_writable_view_set_int32(&view, 12, qa_js_to_int32(result.end_area), true);
      write_vector(&view, 16, result.velocity);
      qa_qvm_writable_view_set_int32(&view, 28, result.trace.start_solid ? 1 : 0, true);
      qa_qvm_writable_view_set_float32(&view, 32, result.trace.fraction, true);
      write_vector(&view, 36, result.trace.end);
      qa_qvm_writable_view_set_int32(&view, 48, qa_js_to_int32(result.trace.entity_num), true);
      qa_qvm_writable_view_set_int32(&view, 52, qa_js_to_int32(result.trace.last_area), true);
      qa_qvm_writable_view_set_int32(&view, 56, qa_js_to_int32(result.trace.area), true);
      qa_qvm_writable_view_set_int32(&view, 60, qa_js_to_int32(result.trace.plane), true);
      qa_qvm_writable_view_set_int32(&view, 64, qa_js_to_int32(result.presence), true);
      qa_qvm_writable_view_set_int32(&view, 68, qa_js_to_int32(result.stop_event), true);
      if (call->has_abi_profile && strcmp(call->abi_profile, "q3-1.16n-base") == 0)
        qa_qvm_writable_view_set_float32(&view, 72, result.end_contents, true);
      else qa_qvm_writable_view_set_int32(&view, 72, qa_js_to_int32(result.end_contents), true);
      qa_qvm_writable_view_set_float32(&view, 76, result.time, true);
      qa_qvm_writable_view_set_int32(&view, 80, qa_js_to_int32(result.frames), true);
      *out = result.success ? 1 : 0;
      return true;
    }
    case 548: qa_bot_move_state_store_reset(states, wint(d, 1)); return true;
    case 549: {
      pointer_ctx result_storage;
      qa_qvm_bot_field_view fields = {(void *)d, field_view};
      qa_qvm_bot_move_result_ref ref = qa_qvm_bot_move_result_reference(pointer_of(&result_storage, d, 1), fields);
      qa_qvm_bot_move_result_set_failure(&ref, false);
      qa_qvm_bot_move_result_set_type(&ref, 0);
      qa_qvm_bot_move_result_set_blocked(&ref, false);
      qa_qvm_bot_move_result_set_block_entity(&ref, 0);
      qa_qvm_bot_move_result_set_travel_type(&ref, 0);
      qa_qvm_bot_move_result_set_flags(&ref, 0);
      if (wint(d, 3) == 0) {
        qa_qvm_bot_move_result_set_failure(&ref, true);
        return true;
      }
      qa_qvmx_bot_move_result staged = qa_qvm_bot_move_result_read(&ref);
      qa_bot_move_result before;
      memset(&before, 0, sizeof before);
      before.failure = staged.failure;
      before.type = (int64_t)staged.type;
      before.blocked = staged.blocked;
      before.block_entity = (int64_t)staged.block_entity;
      before.travel_type = (int64_t)staged.travel_type;
      before.flags = (int64_t)staged.flags;
      before.weapon = (int64_t)staged.weapon;
      before.move_direction = staged.move_direction;
      before.ideal_view_angles = staged.ideal_view_angles;
      qa_bot_move_result result = before;
      int32_t handle = wint(d, 2);
      qa_bot_goal target = goal(d, 3);
      qa_source_bot_navigation_move_to_goal(navigation, &result, handle, target, wint(d, 4));
      if (result.failure != before.failure) qa_qvm_bot_move_result_set_failure(&ref, result.failure);
      if (result.type != before.type) qa_qvm_bot_move_result_set_type(&ref, (double)result.type);
      if (result.blocked != before.blocked) qa_qvm_bot_move_result_set_blocked(&ref, result.blocked);
      if (result.block_entity != before.block_entity) qa_qvm_bot_move_result_set_block_entity(&ref, (double)result.block_entity);
      if (result.travel_type != before.travel_type) qa_qvm_bot_move_result_set_travel_type(&ref, (double)result.travel_type);
      if (result.flags != before.flags) qa_qvm_bot_move_result_set_flags(&ref, (double)result.flags);
      if (result.weapon != before.weapon) qa_qvm_bot_move_result_set_weapon(&ref, (double)result.weapon);
      if (memcmp(&result.move_direction, &before.move_direction, sizeof result.move_direction) != 0)
        qa_qvm_bot_move_result_set_move_direction(&ref, result.move_direction);
      if (memcmp(&result.ideal_view_angles, &before.ideal_view_angles, sizeof result.ideal_view_angles) != 0)
        qa_qvm_bot_move_result_set_ideal_view_angles(&ref, result.ideal_view_angles);
      return true;
    }
    case 550: {
      int32_t handle = wint(d, 1);
      qa_vec3 direction = point(d, 2);
      *out = qa_source_bot_navigation_move_in_direction(navigation, handle, direction, wfloat(d, 3), wint(d, 4)) ? 1 : 0;
      return true;
    }
    case 551: qa_bot_move_state_store_reset_avoid_reach(states, wint(d, 1)); return true;
    case 552: qa_bot_move_state_store_reset_last_avoid_reach(states, wint(d, 1)); return true;
    case 553: {
      qa_vec3 origin = point(d, 1);
      *out = qa_js_to_int32((double)qa_source_bot_navigation_reachability_area(navigation, origin, wint(d, 2)));
      return true;
    }
    case 554: {
      if (wint(d, 2) == 0) return true;
      int32_t handle = wint(d, 1);
      qa_bot_goal target_goal = goal(d, 2);
      qa_vec3 target = target_sentinel();
      bool found = qa_source_bot_navigation_movement_view_target(navigation, handle, target_goal, wint(d, 3), wfloat(d, 4), &target);
      store_target(d, 5, &target);
      *out = found ? 1 : 0;
      return true;
    }
    case 555: *out = qa_qvm_host_word((double)qa_bot_move_state_store_allocate(states)); return true;
    case 556: qa_bot_move_state_store_free(states, wint(d, 1)); return true;
    case 557: {
      pointer_ctx move_storage;
      qa_qvm_bot_init_move_ref ref = qa_qvm_bot_init_move_reference(pointer_of(&move_storage, d, 2));
      int32_t handle = wint(d, 1);
      qa_qvmx_bot_init_move read = qa_qvm_bot_init_move_read(&ref);
      qa_bot_init_move input;
      memset(&input, 0, sizeof input);
      input.origin = read.origin;
      input.velocity = read.velocity;
      input.view_offset = read.view_offset;
      input.entity_num = (int64_t)read.entity_num;
      input.client = (int64_t)read.client;
      input.think_time = read.think_time;
      input.presence_type = (int64_t)read.presence_type;
      input.view_angles = read.view_angles;
      input.or_move_flags = (int64_t)read.or_move_flags;
      qa_bot_move_state_store_initialize(states, handle, input);
      return true;
    }
    case 572: {
      if (wint(d, 3) == 0) return true;
      qa_vec3 origin = point(d, 1);
      int32_t area = wint(d, 2);
      qa_bot_goal target_goal = goal(d, 3);
      qa_vec3 target = target_sentinel();
      bool found = qa_source_bot_navigation_predict_visible_position(navigation, origin, area, target_goal, wint(d, 4), &target);
      store_target(d, 5, &target);
      *out = found ? 1 : 0;
      return true;
    }
    case 574: {
      int32_t handle = wint(d, 1);
      qa_vec3 origin = point(d, 2);
      qa_bot_move_state_store_add_avoid_spot(states, handle, origin, wfloat(d, 3), wint(d, 4));
      return true;
    }
    case 575: {
      qa_alternative_route_query query;
      memset(&query, 0, sizeof query);
      query.start = point(d, 1);
      query.start_area = wint(d, 2);
      query.goal = point(d, 3);
      query.goal_area = wint(d, 4);
      query.travel_flags = wint(d, 5);
      query.maximum_goals = wint(d, 7);
      query.type = wint(d, 8);
      size_t count = 0;
      qa_alternative_goal *goals = qa_source_bot_navigation_alternative_route_goals(navigation, query, &count);
      qa_cleanup cleanup;
      qa_cleanup_push(&cleanup, free, goals);
      for (size_t i = 0; i < count; i++) {
        qa_qvm_game_memory_view(memory, wint(d, 6), 22, (int64_t)i * 24, &view);
        write_vector(&view, 0, goals[i].origin);
        qa_qvm_writable_view_set_int32(&view, 12, qa_js_to_int32((double)goals[i].area), true);
        qa_qvm_writable_view_set_uint16(&view, 16, qa_js_to_uint16(goals[i].start_travel_time), true);
        qa_qvm_writable_view_set_uint16(&view, 18, qa_js_to_uint16(goals[i].goal_travel_time), true);
        qa_qvm_writable_view_set_uint16(&view, 20, qa_js_to_uint16(goals[i].extra_travel_time), true);
      }
      qa_cleanup_pop(&cleanup);
      free(goals);
      *out = (qa_qvm_host_result)count;
      return true;
    }
    case 576: {
      qa_predict_route_query query;
      memset(&query, 0, sizeof query);
      query.route.area = wint(d, 2);
      query.route.origin = point(d, 3);
      query.route.goal_area = wint(d, 4);
      query.route.travel_flags = wint(d, 5);
      query.maximum_areas = wint(d, 6);
      query.maximum_time = wint(d, 7);
      query.stop_event = wint(d, 8);
      query.stop_contents = wint(d, 9);
      query.stop_travel_flags = wint(d, 10);
      query.stop_area = wint(d, 11);
      qa_predicted_route result = qa_source_bot_navigation_predict_route(navigation, query);
      qa_qvm_game_memory_view(memory, wint(d, 1), 36, 0, &view);
      write_vector(&view, 0, result.end_position);
      qa_qvm_writable_view_set_int32(&view, 12, qa_js_to_int32((double)result.end_area), true);
      qa_qvm_writable_view_set_int32(&view, 16, qa_js_to_int32((double)result.stop_event), true);
      qa_qvm_writable_view_set_int32(&view, 20, qa_js_to_int32((double)result.end_contents), true);
      qa_qvm_writable_view_set_int32(&view, 24, qa_js_to_int32((double)result.end_travel_flags), true);
      qa_qvm_writable_view_set_int32(&view, 32, qa_js_to_int32(result.time), true);
      *out = result.succeeded ? 1 : 0;
      return true;
    }
    case 577: {
      if (!services->initialized(services->ctx)) return true;
      qa_navigation_runtime *runtime = host_runtime(navigation);
      const qa_aas_asset *asset = runtime->graph->aas_asset;
      if (asset == NULL) {
        const qa_navigation_graph *graph = runtime->graph;
        double reachable = 0, index = -1;
        int64_t wanted = 0;
        bool has_point = wint(d, 1) != 0;
        int64_t area = 0;
        if (has_point) {
          area = qa_source_bot_navigation_point_area(navigation, point(d, 1));
          wanted = node_id(navigation, area);
        }
        for (size_t i = 0; i < graph->node_count; i++) {
          size_t outgoing = 0;
          (void)qa_navigation_runtime_outgoing(runtime, graph->nodes[i].id, &outgoing);
          if (outgoing == 0) continue;
          if (index < 0 && has_point && graph->nodes[i].id == wanted) index = reachable;
          reachable++;
        }
        if (!has_point) *out = qa_js_to_int32(reachable);
        else *out = area == 0 ? 0 : qa_js_to_int32(qa_js_max(0, index));
        return true;
      }
      if (wint(d, 1) == 0) {
        int32_t sum = 0;
        for (size_t i = 0; i < asset->cluster_count; i++)
          sum = qa_js_to_int32((double)sum + (double)asset->clusters[i].reachability_area_count);
        *out = sum;
        return true;
      }
      int64_t area = qa_source_bot_navigation_point_area(navigation, point(d, 1));
      if (area == 0 || qa_source_bot_navigation_area(navigation, area).reachable_area_count == 0) return true;
      if (area < 0 || area >= (int64_t)asset->setting_count) qa_range_error("QVM reachability area exceeds AAS allocation");
      const qa_aas_setting_rec *settings = &asset->settings[area];
      /* The source indexes with the portal's frontcluster here, which is not a defined area index, so a portal area yields none. */
      if (settings->cluster < 0) return true;
      int32_t sum = 0;
      for (size_t i = 0; i < asset->cluster_count && (int64_t)i < settings->cluster; i++)
        sum = qa_js_to_int32((double)sum + (double)asset->clusters[i].reachability_area_count);
      *out = qa_js_to_int32((double)sum + (double)settings->cluster_area);
      return true;
    }
    default:
      return false;
  }
}

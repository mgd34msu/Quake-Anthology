/* Port of src/compat/qvm/bot-navigation-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/bot_navigation_syscalls.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

qa_qvm_aas_movement_prediction qa_qvm_bot_movement_prediction(
    qa_qvmx_bot_navigation *navigation, qa_qvmx_bot_movement_prediction_query query,
    qa_qvm_entity_number_fn entity_number) {
  double frame_time = query.frame_time <= 0 ? qa_float32(0.1) : query.frame_time;
  query.frame_time = frame_time;
  qa_qvmx_host_prediction result = navigation->predict_client_movement(navigation->ctx, query);
  qa_qvm_aas_movement_prediction prediction;
  memset(&prediction, 0, sizeof prediction);
  if (result.has_trace && result.stop_event != 0 && ((int64_t)result.stop_event & (4 | 8 | 16)) == 0) {
    const qa_qvmx_host_prediction_trace *collision = &result.trace;
    size_t plane_count = 0;
    qa_qvmx_aas_plane *planes = navigation->aas_planes(navigation->ctx, &plane_count);
    double plane_index = -1;
    if (planes != NULL) {
      for (size_t i = 0; i < plane_count; i++) {
        if (planes[i].distance == collision->plane_distance &&
            planes[i].normal.x == collision->plane_normal.x &&
            planes[i].normal.y == collision->plane_normal.y &&
            planes[i].normal.z == collision->plane_normal.z) {
          plane_index = (double)i;
          break;
        }
      }
    }
    free(planes);
    size_t crossing_count = 0;
    qa_qvmx_area_crossing *crossings = navigation->trace_areas(
        navigation->ctx, collision->query_start, collision->end,
        navigation->graph_node_count(navigation->ctx) + 1, &crossing_count);
    prediction.trace.start_solid = collision->start_solid;
    prediction.trace.fraction = collision->fraction;
    prediction.trace.end = collision->end;
    prediction.trace.entity_num =
        collision->hit_is_actor ? entity_number.call(entity_number.ctx, collision->hit_actor) : 0;
    prediction.trace.last_area = crossing_count > 0
                                     ? crossings[crossing_count - 1].area
                                     : navigation->point_area(navigation->ctx, collision->query_start);
    prediction.trace.area = collision->fraction == 1
                                ? 0
                                : navigation->point_area(navigation->ctx, collision->end);
    prediction.trace.plane = plane_index > 0 ? plane_index : 0;
    free(crossings);
  }
  qa_bounds crouched = navigation->presence_bounds(navigation->ctx, 4);
  prediction.presence = !result.has_bounds ? query.presence
                        : result.bounds.max.z - result.bounds.min.z <= crouched.max.z - crouched.min.z
                            ? 4
                            : 2;
  double max_frames = query.max_frames > 0 ? query.max_frames : 0;
  prediction.success = result.stop_event != 0 || result.frames >= max_frames;
  prediction.end = result.end;
  prediction.end_area = result.has_end_area
                            ? result.end_area
                            : navigation->point_area(navigation->ctx, result.end);
  prediction.velocity = result.velocity;
  prediction.stop_event = result.stop_event;
  prediction.end_contents = ((int64_t)result.stop_event & (4 | 8 | 16)) != 0
                                ? navigation->point_contents(navigation->ctx, result.end)
                                : 0;
  prediction.time = result.stop_event == 0
                        ? result.seconds
                        : (result.seconds - frame_time > 0 ? result.seconds - frame_time : 0);
  prediction.frames =
      result.stop_event == 0 ? result.frames : (result.frames - 1 > 0 ? result.frames - 1 : 0);
  return prediction;
}

typedef struct dispatch {
  const qa_qvm_host_call *call;
  qa_qvm_bot_navigation_services *services;
} dispatch;

static int32_t wint(dispatch *d, size_t index) { return qa_qvm_host_call_word(d->call, index); }
static float wfloat(dispatch *d, size_t index) { return qa_qvm_host_call_word_f32(d->call, index * 4); }

typedef struct pointer_ctx { dispatch *d; size_t index; } pointer_ctx;
static qa_qvmx_view pointer_view(void *ctx) {
  pointer_ctx *pointer = ctx;
  qa_qvm_memory_span span;
  qa_qvmx_view view = {NULL, 0};
  if (qa_qvm_memory_pointer(pointer->d->call->guest, wint(pointer->d, pointer->index), &span)) {
    view.bytes = pointer->d->call->guest->bytes + span.byte_offset;
    view.length = (size_t)span.byte_length;
  }
  return view;
}

static qa_qvmx_view field_view(void *ctx, qa_qvmx_view bytes, double offset) {
  dispatch *d = ctx;
  int64_t absolute = (bytes.bytes - d->call->guest->bytes) + (int64_t)offset;
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(d->call->guest, absolute, 4, &view);
  qa_qvmx_view result = {view.bytes + view.start, (size_t)view.length};
  return result;
}

static qa_vec3 point_at(dispatch *d, size_t index, pointer_ctx *storage) {
  storage->d = d;
  storage->index = index;
  qa_qvm_bot_pointer pointer = {storage, pointer_view};
  return qa_qvm_bot_movement_vector(pointer);
}

static void write_vector(qa_qvm_writable_view *view, int64_t offset, qa_vec3 value) {
  qa_qvm_writable_view_set_float32(view, offset, value.x, true);
  qa_qvm_writable_view_set_float32(view, offset + 4, value.y, true);
  qa_qvm_writable_view_set_float32(view, offset + 8, value.z, true);
}

typedef struct key_ctx { dispatch *d; size_t index; } key_ctx;
static bool key_matches(void *ctx, const char *candidate) {
  key_ctx *key = ctx;
  qa_qvm_memory_span span;
  if (!qa_qvm_memory_pointer(key->d->call->guest, wint(key->d, key->index), &span))
    qa_range_error("QVM BSP epair key is null");
  size_t length = strlen(candidate);
  for (size_t i = 0; i <= length; i++) {
    if ((int64_t)i >= span.byte_length) qa_range_error("QVM BSP epair key exceeds allocation");
    uint8_t byte = key->d->call->guest->bytes[span.byte_offset + (int64_t)i];
    if (byte != (uint8_t)(i == length ? 0 : candidate[i])) return false;
  }
  return true;
}

typedef struct clear_ctx { dispatch *d; int64_t offset; } clear_ctx;
static void output_clear(void *ctx, int64_t length) {
  clear_ctx *clear = ctx;
  qa_qvm_memory_fill_bytes(clear->d->call->guest, clear->offset, length, 0);
}

bool qa_qvm_bot_navigation_syscall(const qa_qvm_host_call *call,
                                   qa_qvm_bot_navigation_services *services,
                                   qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "qagame") != 0) return false;
  dispatch d = {call, services};
  qa_qvm_memory *memory = call->guest;
  qa_qvmx_bot_navigation *navigation = &services->navigation;
  qa_qvmx_bot_library *library = services->library;
  pointer_ctx p1, p2, p3, p6, p7;
  qa_qvm_writable_view view;
  switch (call->code) {
    case 300: {
      int32_t area = wint(&d, 1);
      double node_id = navigation->graph_is_aas(navigation->ctx) ? area : area - 1;
      qa_qvmx_navigation_node node;
      bool has_node = navigation->node(navigation->ctx, node_id, &node);
      if (area <= 0 || !has_node) { *out = 0; return true; }
      if (wint(&d, 2) >= 0) {
        *out = navigation->enable_area(navigation->ctx, node.id, wint(&d, 2) != 0) ? 1 : 0;
        return true;
      }
      bool enabled = false;
      bool found = navigation->area_enabled(navigation->ctx, node.id, &enabled);
      *out = (found ? enabled : !(node.is_aas && ((int64_t)node.flags & 8) != 0)) ? 1 : 0;
      return true;
    }
    case 301: {
      qa_bounds bounds = {point_at(&d, 1, &p1), point_at(&d, 2, &p2)};
      size_t count = 0;
      double *areas = navigation->bbox_areas(navigation->ctx, bounds, &count);
      int64_t max = wint(&d, 4);
      if (max < 0) max = 0;
      size_t kept = count < (size_t)max ? count : (size_t)max;
      for (size_t i = 0; i < kept; i++) {
        qa_qvm_memory_view(memory, wint(&d, 3), 4, (int64_t)i * 4, &view);
        qa_qvm_writable_view_set_int32(&view, 0, qa_int32(areas[i]), true);
      }
      free(areas);
      *out = (qa_qvm_host_result)kept; return true;
    }
    case 302: {
      int32_t area = wint(&d, 1);
      double node_id = navigation->graph_is_aas(navigation->ctx) ? area : area - 1;
      qa_qvmx_navigation_node node;
      bool has_node = navigation->node(navigation->ctx, node_id, &node);
      if (wint(&d, 2) == 0 || !has_node || area <= 0) { *out = 0; return true; }
      qa_qvmx_navigation_area info = navigation->area(navigation->ctx, area);
      qa_qvm_memory_view(memory, wint(&d, 2), 52, 0, &view);
      bool enabled = false;
      bool found = navigation->area_enabled(navigation->ctx, node.id, &enabled);
      double flags = !found ? info.flags
                            : (double)(enabled ? qa_int32(info.flags) & ~8 : qa_int32(info.flags) | 8);
      qa_qvm_writable_view_set_int32(&view, 0, qa_int32(info.contents), true);
      qa_qvm_writable_view_set_int32(&view, 4, qa_int32(flags), true);
      qa_qvm_writable_view_set_int32(&view, 8, qa_int32(info.presence_type), true);
      qa_qvm_writable_view_set_int32(&view, 12, qa_int32(info.cluster), true);
      write_vector(&view, 16, node.bounds.min);
      write_vector(&view, 28, node.bounds.max);
      write_vector(&view, 40, node.origin);
      *out = 52; return true;
    }
    case 303: {
      qa_qvm_aas_entity_info info;
      if (!services->entity_info(services->ctx, wint(&d, 1), &info)) {
        qa_qvm_memory_span span = qa_qvm_memory_get_span(memory, wint(&d, 2), 140, 0);
        qa_qvm_memory_fill_bytes(memory, span.byte_offset, 140, 0);
      } else {
        qa_qvm_writable_view storage;
        qa_qvm_memory_view(memory, wint(&d, 2), 140, 0, &storage);
        qa_qvmx_view target = {storage.bytes + storage.start, (size_t)storage.length};
        qa_qvm_write_aas_entity_info(target, &info);
      }
      *out = 0; return true;
    }
    case 304: *out = services->initialized(services->ctx) ? 1 : 0; return true;
    case 305: {
      int32_t presence = wint(&d, 1);
      if (presence != 2 && presence != 4)
        library->print(library->ctx, 4, "AAS_PresenceTypeBoundingBox: unknown presence type\n");
      qa_bounds bounds = navigation->presence_bounds(navigation->ctx, presence == 2 ? 2 : 4);
      qa_qvm_memory_view(memory, wint(&d, 2), 12, 0, &view);
      write_vector(&view, 0, bounds.min);
      qa_qvm_memory_view(memory, wint(&d, 3), 12, 0, &view);
      write_vector(&view, 0, bounds.max);
      *out = 0; return true;
    }
    case 306: *out = (int32_t)qa_float32_to_bits(services->time(services->ctx)); return true;
    case 307:
      *out = qa_int32(navigation->point_area(navigation->ctx, point_at(&d, 1, &p1))); return true;
    case 308: {
      qa_qvm_memory_view(memory, wint(&d, 3), 4, 0, &view);
      qa_qvm_writable_view_set_int32(&view, 0, 0, true);
      size_t count = 0;
      qa_qvmx_area_crossing *crossings = navigation->trace_areas(
          navigation->ctx, point_at(&d, 1, &p1), point_at(&d, 2, &p2), wint(&d, 5), &count);
      for (size_t i = 0; i < count; i++) {
        qa_qvm_memory_view(memory, wint(&d, 3), 4, (int64_t)i * 4, &view);
        qa_qvm_writable_view_set_int32(&view, 0, qa_int32(crossings[i].area), true);
        if (wint(&d, 4) != 0) {
          qa_qvm_memory_view(memory, wint(&d, 4), 12, (int64_t)i * 12, &view);
          write_vector(&view, 0, crossings[i].point);
        }
      }
      free(crossings);
      *out = (qa_qvm_host_result)count; return true;
    }
    case 309:
      *out = qa_int32(navigation->point_contents(navigation->ctx, point_at(&d, 1, &p1))); return true;
    case 310:
      *out = qa_int32(services->bsp_entities.next_entity(services->bsp_entities.ctx, wint(&d, 1)));
      return true;
    case 311: {
      qa_qvm_memory_span output;
      if (!qa_qvm_memory_pointer(memory, wint(&d, 3), &output))
        qa_range_error("QVM BSP epair output is null");
      key_ctx key = {&d, 2};
      qa_qvm_bsp_key_fn key_fn = {&key, key_matches};
      qa_qvm_writable_view output_view;
      qa_qvm_memory_data_view(memory, output.byte_offset, output.byte_length, &output_view);
      clear_ctx clear = {&d, output.byte_offset};
      qa_qvm_bsp_output_io io = {output_view, &clear, output_clear};
      *out = services->bsp_entities.value(services->bsp_entities.ctx, wint(&d, 1), key_fn, output,
                                          wint(&d, 4), io) ? 1 : 0;
      return true;
    }
    case 312: {
      qa_qvm_memory_view(memory, wint(&d, 3), 12, 0, &view);
      qa_vec3 zero = {0, 0, 0};
      write_vector(&view, 0, zero);
      key_ctx key = {&d, 2};
      qa_qvm_bsp_key_fn key_fn = {&key, key_matches};
      qa_qvmx_bsp_vector_result result =
          services->bsp_entities.vector(services->bsp_entities.ctx, wint(&d, 1), key_fn);
      write_vector(&view, 0, result.value);
      *out = result.found ? 1 : 0; return true;
    }
    case 313:
    case 314: {
      qa_qvm_memory_view(memory, wint(&d, 3), 4, 0, &view);
      qa_qvm_writable_view_set_int32(&view, 0, 0, true);
      key_ctx key = {&d, 2};
      qa_qvm_bsp_key_fn key_fn = {&key, key_matches};
      qa_qvmx_bsp_float_result result = call->code == 313
          ? services->bsp_entities.get_float(services->bsp_entities.ctx, wint(&d, 1), key_fn)
          : services->bsp_entities.get_int(services->bsp_entities.ctx, wint(&d, 1), key_fn);
      if (call->code == 313) qa_qvm_writable_view_set_float32(&view, 0, qa_float32(result.value), true);
      else qa_qvm_writable_view_set_int32(&view, 0, qa_int32(result.value), true);
      *out = result.found ? 1 : 0; return true;
    }
    case 315:
      *out = qa_int32(navigation->area(navigation->ctx, wint(&d, 1)).reachable_area_count);
      return true;
    case 316: {
      if (!services->initialized(services->ctx)) { *out = 0; return true; }
      qa_qvmx_travel_query query;
      query.area = wint(&d, 1);
      query.has_origin = wint(&d, 2) != 0;
      query.origin = query.has_origin ? point_at(&d, 2, &p2) : (qa_vec3){0, 0, 0};
      query.goal_area = wint(&d, 3);
      query.travel_flags = wint(&d, 4);
      *out = qa_int32(navigation->area_travel_time_to_goal(navigation->ctx, query));
      return true;
    }
    case 317:
      *out = navigation->swimming(navigation->ctx, point_at(&d, 1, &p1)) ? 1 : 0; return true;
    case 318: {
      int32_t presence = wint(&d, 4);
      if (presence != 2 && presence != 4)
        qa_range_error("QVM movement prediction requires a supported presence type");
      qa_qvmx_bot_movement_prediction_query query;
      query.entity_num = wint(&d, 2);
      query.origin = point_at(&d, 3, &p3);
      query.presence = presence;
      query.on_ground = wint(&d, 5) != 0;
      query.velocity = point_at(&d, 6, &p6);
      query.command_move = point_at(&d, 7, &p7);
      query.command_frames = wint(&d, 8);
      query.max_frames = wint(&d, 9);
      query.frame_time = wfloat(&d, 10);
      query.stop_events = wint(&d, 11);
      query.stop_area = wint(&d, 12);
      query.visualize = wint(&d, 13) != 0;
      qa_qvm_aas_movement_prediction result =
          services->predict_client_movement(services->ctx, query);
      qa_qvm_memory_view(memory, wint(&d, 1), 84, 0, &view);
      write_vector(&view, 0, result.end);
      qa_qvm_writable_view_set_int32(&view, 12, qa_int32(result.end_area), true);
      write_vector(&view, 16, result.velocity);
      qa_qvm_writable_view_set_int32(&view, 28, result.trace.start_solid ? 1 : 0, true);
      qa_qvm_writable_view_set_float32(&view, 32, qa_float32(result.trace.fraction), true);
      write_vector(&view, 36, result.trace.end);
      qa_qvm_writable_view_set_int32(&view, 48, qa_int32(result.trace.entity_num), true);
      qa_qvm_writable_view_set_int32(&view, 52, qa_int32(result.trace.last_area), true);
      qa_qvm_writable_view_set_int32(&view, 56, qa_int32(result.trace.area), true);
      qa_qvm_writable_view_set_int32(&view, 60, qa_int32(result.trace.plane), true);
      qa_qvm_writable_view_set_int32(&view, 64, qa_int32(result.presence), true);
      qa_qvm_writable_view_set_int32(&view, 68, qa_int32(result.stop_event), true);
      if (call->has_abi_profile && strcmp(call->abi_profile, "q3-1.16n-base") == 0)
        qa_qvm_writable_view_set_float32(&view, 72, qa_float32(result.end_contents), true);
      else
        qa_qvm_writable_view_set_int32(&view, 72, qa_int32(result.end_contents), true);
      qa_qvm_writable_view_set_float32(&view, 76, qa_float32(result.time), true);
      qa_qvm_writable_view_set_int32(&view, 80, qa_int32(result.frames), true);
      *out = result.success ? 1 : 0; return true;
    }
    case 548: library->move_states_reset(library->ctx, wint(&d, 1)); *out = 0; return true;
    case 549: {
      pointer_ctx result_pointer = {&d, 1};
      pointer_ctx goal_pointer = {&d, 3};
      qa_qvm_bot_pointer rp = {&result_pointer, pointer_view};
      qa_qvm_bot_pointer gp = {&goal_pointer, pointer_view};
      qa_qvm_bot_field_view fv = {&d, field_view};
      qa_qvm_bot_move_result_ref result = qa_qvm_bot_move_result_reference(rp, fv);
      qa_qvm_bot_move_result_set_failure(&result, false);
      qa_qvm_bot_move_result_set_type(&result, 0);
      qa_qvm_bot_move_result_set_blocked(&result, false);
      qa_qvm_bot_move_result_set_block_entity(&result, 0);
      qa_qvm_bot_move_result_set_travel_type(&result, 0);
      qa_qvm_bot_move_result_set_flags(&result, 0);
      if (wint(&d, 3) == 0) qa_qvm_bot_move_result_set_failure(&result, true);
      else {
        qa_qvm_bot_goal_ref goal = qa_qvm_read_bot_goal_reference(gp);
        navigation->move_to_goal(navigation->ctx, result, wint(&d, 2), goal, wint(&d, 4));
      }
      *out = 0; return true;
    }
    case 550:
      *out = navigation->move_in_direction(navigation->ctx, wint(&d, 1), point_at(&d, 2, &p2),
                                            wfloat(&d, 3), wint(&d, 4)) ? 1 : 0;
      return true;
    case 551: library->move_states_reset_avoid_reach(library->ctx, wint(&d, 1)); *out = 0; return true;
    case 552: library->move_states_reset_last_avoid_reach(library->ctx, wint(&d, 1)); *out = 0; return true;
    case 553:
      *out = qa_int32(navigation->reachability_area(navigation->ctx, point_at(&d, 1, &p1), wint(&d, 2)));
      return true;
    case 554: {
      if (wint(&d, 2) == 0) { *out = 0; return true; }
      pointer_ctx goal_pointer = {&d, 2};
      pointer_ctx target_pointer = {&d, 5};
      qa_qvm_bot_pointer gp = {&goal_pointer, pointer_view};
      qa_qvm_bot_pointer tp = {&target_pointer, pointer_view};
      qa_qvm_bot_field_view fv = {&d, field_view};
      qa_qvm_bot_goal_ref goal = qa_qvm_read_bot_goal_reference(gp);
      qa_qvm_bot_movement_target_ref target = qa_qvm_bot_movement_target(tp, fv);
      *out = navigation->movement_view_target(navigation->ctx, wint(&d, 1), goal, wint(&d, 3),
                                              wfloat(&d, 4), target) ? 1 : 0;
      return true;
    }
    case 555: *out = (qa_qvm_host_result)library->move_states_allocate(library->ctx); return true;
    case 556: library->move_states_free(library->ctx, wint(&d, 1)); *out = 0; return true;
    case 557: {
      pointer_ctx move_pointer = {&d, 2};
      qa_qvm_bot_pointer mp = {&move_pointer, pointer_view};
      qa_qvm_bot_init_move_ref ref = qa_qvm_bot_init_move_reference(mp);
      library->move_states_initialize(library->ctx, wint(&d, 1), qa_qvm_bot_init_move_read(&ref));
      *out = 0; return true;
    }
    case 572: {
      if (wint(&d, 3) == 0) { *out = 0; return true; }
      pointer_ctx goal_pointer = {&d, 3};
      pointer_ctx target_pointer = {&d, 5};
      qa_qvm_bot_pointer gp = {&goal_pointer, pointer_view};
      qa_qvm_bot_pointer tp = {&target_pointer, pointer_view};
      qa_qvm_bot_field_view fv = {&d, field_view};
      qa_qvm_bot_goal_ref goal = qa_qvm_read_bot_goal_reference(gp);
      qa_qvm_bot_movement_target_ref target = qa_qvm_bot_movement_target(tp, fv);
      *out = navigation->predict_visible_position(navigation->ctx, point_at(&d, 1, &p1), wint(&d, 2),
                                                  goal, wint(&d, 4), target) ? 1 : 0;
      return true;
    }
    case 574:
      library->move_states_add_avoid_spot(library->ctx, wint(&d, 1), point_at(&d, 2, &p2),
                                          wfloat(&d, 3), wint(&d, 4));
      *out = 0; return true;
    case 575: {
      size_t count = 0;
      qa_qvmx_alternative_route_goal *goals = navigation->alternative_route_goals(
          navigation->ctx, point_at(&d, 1, &p1), wint(&d, 2), point_at(&d, 3, &p3), wint(&d, 4),
          wint(&d, 5), wint(&d, 7), wint(&d, 8), &count);
      for (size_t i = 0; i < count; i++) {
        qa_qvm_memory_view(memory, wint(&d, 6), 22, (int64_t)i * 24, &view);
        write_vector(&view, 0, goals[i].origin);
        qa_qvm_writable_view_set_int32(&view, 12, qa_int32(goals[i].area), true);
        qa_qvm_writable_view_set_uint16(&view, 16, (uint16_t)qa_int32(goals[i].start_travel_time), true);
        qa_qvm_writable_view_set_uint16(&view, 18, (uint16_t)qa_int32(goals[i].goal_travel_time), true);
        qa_qvm_writable_view_set_uint16(&view, 20, (uint16_t)qa_int32(goals[i].extra_travel_time), true);
      }
      free(goals);
      *out = (qa_qvm_host_result)count; return true;
    }
    case 576: {
      qa_qvmx_predict_route_query query;
      query.area = wint(&d, 2);
      query.origin = point_at(&d, 3, &p3);
      query.goal_area = wint(&d, 4);
      query.travel_flags = wint(&d, 5);
      query.maximum_areas = wint(&d, 6);
      query.maximum_time = wint(&d, 7);
      query.stop_event = wint(&d, 8);
      query.stop_contents = wint(&d, 9);
      query.stop_travel_flags = wint(&d, 10);
      query.stop_area = wint(&d, 11);
      qa_qvmx_predict_route_result result = navigation->predict_route(navigation->ctx, query);
      qa_qvm_memory_view(memory, wint(&d, 1), 36, 0, &view);
      write_vector(&view, 0, result.end_position);
      qa_qvm_writable_view_set_int32(&view, 12, qa_int32(result.end_area), true);
      qa_qvm_writable_view_set_int32(&view, 16, qa_int32(result.stop_event), true);
      qa_qvm_writable_view_set_int32(&view, 20, qa_int32(result.end_contents), true);
      qa_qvm_writable_view_set_int32(&view, 24, qa_int32(result.end_travel_flags), true);
      qa_qvm_writable_view_set_int32(&view, 32, qa_int32(result.time), true);
      *out = result.succeeded ? 1 : 0; return true;
    }
    case 577: {
      if (!services->initialized(services->ctx)) { *out = 0; return true; }
      if (!navigation->graph_is_aas(navigation->ctx)) {
        size_t count = 0;
        double *reachable = navigation->reachable_node_ids(navigation->ctx, &count);
        if (wint(&d, 1) == 0) { free(reachable); *out = (qa_qvm_host_result)count; return true; }
        double area = navigation->point_area(navigation->ctx, point_at(&d, 1, &p1));
        double node_id = area - 1;
        double index = -1;
        for (size_t i = 0; i < count; i++)
          if (reachable[i] == node_id) index = (double)i;
        free(reachable);
        *out = area == 0 ? 0 : qa_int32(index > 0 ? index : 0);
        return true;
      }
      size_t cluster_count = 0;
      double *clusters = navigation->cluster_reachability_counts(navigation->ctx, &cluster_count);
      if (wint(&d, 1) == 0) {
        int32_t sum = 0;
        for (size_t i = 0; i < cluster_count; i++) sum = (int32_t)((uint32_t)sum + (uint32_t)qa_int32(clusters[i]));
        free(clusters);
        *out = sum; return true;
      }
      double area = navigation->point_area(navigation->ctx, point_at(&d, 1, &p1));
      if (area == 0 || navigation->area(navigation->ctx, area).reachable_area_count == 0) {
        free(clusters); *out = 0; return true;
      }
      double cluster = 0, cluster_area = 0;
      if (!navigation->area_settings(navigation->ctx, area, &cluster, &cluster_area)) {
        free(clusters);
        qa_range_error("QVM reachability area exceeds AAS allocation");
      }
      if (cluster < 0) {
        free(clusters);
        qa_range_error("AAS_PointReachabilityAreaIndex source reuses frontcluster as a negative portal index");
      }
      int32_t sum = 0;
      for (size_t i = 0; i < cluster_count && (double)i < cluster; i++)
        sum = (int32_t)((uint32_t)sum + (uint32_t)qa_int32(clusters[i]));
      free(clusters);
      *out = (int32_t)((uint32_t)sum + (uint32_t)qa_int32(cluster_area));
      return true;
    }
    default: return false;
  }
}

/* Port of src/compat/qvm/client-collision-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_collision_syscalls.h"

#include <stdlib.h>
#include <string.h>

#include "qa/compat/qvm/trace_record.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

static qa_vec3 read_vector(qa_qvm_memory *memory, int32_t word) {
  qa_qvm_writable_view view;
  qa_qvm_memory_view(memory, word, 12, 0, &view);
  qa_vec3 v = {qa_qvm_writable_view_get_float32(&view, 0, true),
               qa_qvm_writable_view_get_float32(&view, 4, true),
               qa_qvm_writable_view_get_float32(&view, 8, true)};
  return v;
}

static qa_vec3 bounds_vector(qa_qvm_memory *memory, int32_t word) {
  qa_vec3 zero = {0, 0, 0};
  return word == 0 ? zero : read_vector(memory, word);
}

bool qa_qvm_client_collision_syscall(const qa_qvm_host_call *call,
                                     qa_qvm_client_collision_services services,
                                     qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "cgame") != 0) return false;
  qa_qvm_memory *memory = call->guest;
  int32_t trap = call->code;
  if (trap == 18) {
    char *name = qa_qvm_memory_read_string(memory, qa_qvm_host_call_word(call, 1));
    if (name == NULL) qa_range_error("out of memory");
    services.load_map(services.ctx, name);
    free(name);
    *out = 0;
    return true;
  }
  const int32_t traps[] = {19, 20, 22, 23, 24, 25, 26, 82, 83, 84};
  bool listed = false;
  for (size_t i = 0; i < sizeof traps / sizeof traps[0]; i++) listed = listed || traps[i] == trap;
  if (!listed) return false;
  qa_qvm_client_clip_models *models = services.models(services.ctx);
  switch (trap) {
    case 19: *out = qa_int32(qa_source_clip_model_count(models)); return true;
    case 20:
      *out = qa_int32(qa_source_clip_inline_model(models, qa_qvm_host_call_word(call, 1)));
      return true;
    case 22:
    case 82:
      *out = qa_int32(qa_source_clip_temp_box_model(
          models, read_vector(memory, qa_qvm_host_call_word(call, 1)),
          read_vector(memory, qa_qvm_host_call_word(call, 2)), trap == 82));
      return true;
    case 23: {
      int32_t point = qa_qvm_host_call_word(call, 1);
      int32_t handle = qa_qvm_host_call_word(call, 2);
      *out = qa_collision_world_has_nodes(models->world)
                 ? qa_int32(qa_source_clip_point_contents(models, read_vector(memory, point), handle))
                 : 0;
      return true;
    }
    case 24: {
      int32_t point = qa_qvm_host_call_word(call, 1);
      int32_t handle = qa_qvm_host_call_word(call, 2);
      int32_t origin = qa_qvm_host_call_word(call, 3);
      int32_t angles = qa_qvm_host_call_word(call, 4);
      qa_vec3 zero = {0, 0, 0};
      *out = qa_int32(qa_source_clip_transformed_point_contents(
          models, read_vector(memory, point), handle, read_vector(memory, origin),
          handle == QA_SOURCE_BOX_MODEL_HANDLE ? zero : read_vector(memory, angles)));
      return true;
    }
    case 25:
    case 26:
    case 83:
    case 84: {
      qa_qvm_writable_view output;
      qa_qvm_memory_view(memory, qa_qvm_host_call_word(call, 1), QA_QVM_TRACE_BYTES, 0, &output);
      int32_t handle = qa_qvm_host_call_word(call, 6);
      int32_t mask = qa_qvm_host_call_word(call, 7);
      if (trap == 25 || trap == 83) {
        qa_source_trace_result unloaded;
        if (qa_source_clip_trace_without_nodes(models, handle, &unloaded)) {
          qa_qvm_trace_record record = {unloaded, 0};
          qa_qvm_write_trace_view(&output, &record);
          *out = 0;
          return true;
        }
      }
      qa_vec3 start = read_vector(memory, qa_qvm_host_call_word(call, 2));
      qa_vec3 end = read_vector(memory, qa_qvm_host_call_word(call, 3));
      qa_vec3 mins = bounds_vector(memory, qa_qvm_host_call_word(call, 4));
      qa_vec3 maxs = bounds_vector(memory, qa_qvm_host_call_word(call, 5));
      qa_temporary_trace_query query;
      memset(&query, 0, sizeof query);
      query.start = start;
      query.end = end;
      query.shape.kind = trap == 83 || trap == 84 ? "capsule" : "box";
      query.shape.mins = mins;
      query.shape.maxs = maxs;
      query.mask = mask;
      qa_source_trace_result result;
      if (trap == 26 || trap == 84) {
        int32_t origin = qa_qvm_host_call_word(call, 8);
        int32_t angles = qa_qvm_host_call_word(call, 9);
        qa_vec3 zero = {0, 0, 0};
        result = qa_source_clip_transformed_trace(
            models, query, handle, read_vector(memory, origin),
            handle == QA_SOURCE_BOX_MODEL_HANDLE ? zero : read_vector(memory, angles));
      } else {
        result = qa_source_clip_trace(models, query, handle);
      }
      qa_qvm_trace_record record = {result, 0};
      qa_qvm_write_trace_view(&output, &record);
      *out = 0;
      return true;
    }
    default: return false;
  }
}

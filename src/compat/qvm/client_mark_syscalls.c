/* Port of src/compat/qvm/client-mark-syscalls.ts. GPL-2.0-or-later. */
#include "qa/compat/qvm/client_mark_syscalls.h"

#include <math.h>
#include <string.h>

#include "qa/compat/qvm/abi.h"
#include "qa/core/numeric.h"
#include "qa/foundation/support.h"

typedef struct mark_ctx {
  qa_qvm_memory *memory;
  bool has_input, has_projection, has_points, has_fragments;
  qa_qvm_memory_span input, projection, points, fragments;
} mark_ctx;

static qa_qvm_writable_view span(mark_ctx *ctx, bool present, qa_qvm_memory_span bytes,
                                 double offset, int64_t length, const char *name) {
  if (!present) qa_range_error("QVM mark %s requires a nonnull pointer", name);
  if (!(isfinite(offset) && offset == trunc(offset) && offset >= -9007199254740991.0 &&
        offset <= 9007199254740991.0) ||
      offset < 0 || offset + (double)length > (double)bytes.byte_length)
    qa_range_error("QVM mark %s exceeds allocation", name);
  qa_qvm_writable_view view;
  qa_qvm_memory_data_view(ctx->memory, bytes.byte_offset + (int64_t)offset, length, &view);
  return view;
}

static qa_vec3 read_vector_view(const qa_qvm_writable_view *view) {
  qa_vec3 v = {qa_qvm_writable_view_get_float32(view, 0, true),
               qa_qvm_writable_view_get_float32(view, 4, true),
               qa_qvm_writable_view_get_float32(view, 8, true)};
  return v;
}

static qa_vec3 query_read_projection(void *ctx) {
  mark_ctx *mark = ctx;
  qa_qvm_writable_view view = span(mark, mark->has_projection, mark->projection, 0, 12, "projection");
  return read_vector_view(&view);
}
static qa_vec3 query_read_point(void *ctx, double index) {
  mark_ctx *mark = ctx;
  qa_qvm_writable_view view = span(mark, mark->has_input, mark->input, index * 12, 12, "input points");
  return read_vector_view(&view);
}
static void query_write_fragment(void *ctx, double index, qa_q3_pres_mark_fragment fragment) {
  mark_ctx *mark = ctx;
  qa_qvm_writable_view first = span(mark, mark->has_fragments, mark->fragments, index * 8, 4, "fragments");
  qa_qvm_writable_view_set_int32(&first, 0, qa_int32(fragment.first_point), true);
  qa_qvm_writable_view second =
      span(mark, mark->has_fragments, mark->fragments, index * 8 + 4, 4, "fragments");
  qa_qvm_writable_view_set_int32(&second, 0, qa_int32(fragment.point_count), true);
}
static void query_write_points(void *ctx, double first_point, const qa_vec3 *values, size_t count) {
  mark_ctx *mark = ctx;
  qa_qvm_writable_view output =
      span(mark, mark->has_points, mark->points, first_point * 12, (int64_t)count * 12, "output points");
  for (size_t i = 0; i < count; i++) {
    qa_qvm_writable_view_set_float32(&output, (int64_t)i * 12, qa_float32(values[i].x), true);
    qa_qvm_writable_view_set_float32(&output, (int64_t)i * 12 + 4, qa_float32(values[i].y), true);
    qa_qvm_writable_view_set_float32(&output, (int64_t)i * 12 + 8, qa_float32(values[i].z), true);
  }
}

static double bsp_mark_fragments_record(void *ctx, qa_q3_pres_source_mark_projection query) {
  return qa_q3_pres_bsp_mark_projector_mark_fragments_record(ctx, query);
}

qa_qvm_mark_projector qa_qvm_bsp_mark_projector(qa_q3_pres_bsp_mark_projector *projector) {
  qa_qvm_mark_projector out = {projector, bsp_mark_fragments_record};
  return out;
}

/* The active renderer owns both the BSP and its prepared mark grids. */
bool qa_qvm_client_mark_syscall(const qa_qvm_host_call *call, qa_qvm_mark_projector projector,
                                qa_qvm_host_result *out) {
  if (strcmp(call->kind, "engine") != 0 || strcmp(call->role, "cgame") != 0 ||
      call->code != QA_QVM_CGAME_IMPORT_CG_CM_MARKFRAGMENTS)
    return false;
  qa_qvm_memory *memory = call->guest;
  int32_t point_count = qa_qvm_host_call_word(call, 1);
  int32_t input_word = qa_qvm_host_call_word(call, 2);
  int32_t projection_word = qa_qvm_host_call_word(call, 3);
  int32_t max_points = qa_qvm_host_call_word(call, 4);
  int32_t points_word = qa_qvm_host_call_word(call, 5);
  int32_t max_fragments = qa_qvm_host_call_word(call, 6);
  int32_t fragments_word = qa_qvm_host_call_word(call, 7);
  mark_ctx ctx;
  ctx.memory = memory;
  ctx.has_input = qa_qvm_memory_pointer(memory, input_word, &ctx.input);
  ctx.has_projection = qa_qvm_memory_pointer(memory, projection_word, &ctx.projection);
  ctx.has_points = qa_qvm_memory_pointer(memory, points_word, &ctx.points);
  ctx.has_fragments = qa_qvm_memory_pointer(memory, fragments_word, &ctx.fragments);
  qa_q3_pres_source_mark_projection query;
  query.point_count = point_count;
  query.max_points = max_points;
  query.max_fragments = max_fragments;
  query.ctx = &ctx;
  query.read_projection = query_read_projection;
  query.read_point = query_read_point;
  query.write_fragment = query_write_fragment;
  query.write_points = query_write_points;
  *out = qa_qvm_host_word(projector.mark_fragments_record(projector.ctx, query));
  return true;
}

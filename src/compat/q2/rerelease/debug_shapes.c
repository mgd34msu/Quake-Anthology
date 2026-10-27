/* Port of src/compat/q2/rerelease/debug-shapes.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/debug_shapes.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

double qa_compat_q2_rerelease_debug_lifetime(double lifetime_seconds) {
  return lifetime_seconds >= 0 ? lifetime_seconds * 1000 : 1000;
}
struct qa_compat_q2_rerelease_debug_shape_imports {
  qa_compat_q2_memory *memory; void *emit_ctx; void (*emit)(void *, qa_compat_q2_rerelease_debug_shapes_event);
};
qa_compat_q2_rerelease_debug_shape_imports *qa_compat_q2_rerelease_debug_shape_imports_create(
    qa_compat_q2_memory *memory, void *emit_ctx, void (*emit)(void *, qa_compat_q2_rerelease_debug_shapes_event)) {
  qa_compat_q2_rerelease_debug_shape_imports *d = calloc(1, sizeof *d); if (!d) return NULL;
  d->memory = memory; d->emit_ctx = emit_ctx; d->emit = emit; return d;
}
void qa_compat_q2_rerelease_debug_shape_imports_destroy(qa_compat_q2_rerelease_debug_shape_imports *d) { free(d); }
static double shape_float(const qa_guest_call_value *args, size_t count, size_t index) {
  const qa_guest_call_value *v = qa_compat_q2_guest_argument(args, count, index);
  if (v->kind != QA_GUEST_VALUE_FLOAT32 || !isfinite(v->number_value)) qa_range_error("Q2 debug shape requires a finite source float");
  return v->number_value;
}
bool qa_compat_q2_rerelease_debug_shape_imports_invoke(qa_compat_q2_rerelease_debug_shape_imports *d,
                                                       qa_compat_q2_rerelease_import_call call, qa_guest_call_result *out) {
  if (strcmp(call.api, "game") != 0) return false;
  const qa_guest_call_value *args = call.arguments; size_t count = call.argument_count;
  qa_compat_q2_memory *memory = d->memory;
  /* Shape construction with the same argument order as TS. */
  qa_debug_shape shape; memset(&shape, 0, sizeof shape);
  size_t color_index = 0, lifetime_index = 0;
  #define VEC(i) qa_compat_q2_mem_read_f32_vector(memory, qa_compat_q2_guest_required_pointer(args, count, (i)))
  /* shape float arguments validated like the TS float() helper */
  if (strcmp(call.name, "Draw_Line") == 0) { shape.kind = QA_DEBUG_SHAPE_LINE; shape.start = VEC(0); shape.end = VEC(1); color_index = 2; lifetime_index = 3; }
  else if (strcmp(call.name, "Draw_Point") == 0) { shape.kind = QA_DEBUG_SHAPE_POINT; shape.origin = VEC(0); shape.size = shape_float(args, count, 1); color_index = 2; lifetime_index = 3; }
  else if (strcmp(call.name, "Draw_Circle") == 0 || strcmp(call.name, "Draw_Sphere") == 0) {
    shape.kind = strcmp(call.name, "Draw_Circle") == 0 ? QA_DEBUG_SHAPE_CIRCLE : QA_DEBUG_SHAPE_SPHERE;
    shape.origin = VEC(0); shape.radius = shape_float(args, count, 1); color_index = 2; lifetime_index = 3; }
  else if (strcmp(call.name, "Draw_Bounds") == 0) { shape.kind = QA_DEBUG_SHAPE_BOUNDS; shape.min = VEC(0); shape.max = VEC(1); color_index = 2; lifetime_index = 3; }
  else if (strcmp(call.name, "Draw_Cylinder") == 0) { shape.kind = QA_DEBUG_SHAPE_CYLINDER; shape.origin = VEC(0); shape.half_height = shape_float(args, count, 1); shape.radius = shape_float(args, count, 2); color_index = 3; lifetime_index = 4; }
  else if (strcmp(call.name, "Draw_Ray") == 0) { shape.kind = QA_DEBUG_SHAPE_RAY; shape.origin = VEC(0); shape.direction = VEC(1); shape.length = shape_float(args, count, 2); shape.size = shape_float(args, count, 3); color_index = 4; lifetime_index = 5; }
  else if (strcmp(call.name, "Draw_Arrow") == 0) {
    shape.kind = QA_DEBUG_SHAPE_ARROW; shape.start = VEC(0); shape.end = VEC(1); shape.size = shape_float(args, count, 2);
    qa_guest_address cap = qa_compat_q2_guest_required_pointer(args, count, 4);
    shape.cap_color = (qa_vec4){qa_compat_q2_mem_read_u8(memory, cap) / 255.0,
      qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, cap, 1)) / 255.0,
      qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, cap, 2)) / 255.0,
      qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, cap, 3)) / 255.0};
    color_index = 3; lifetime_index = 5; }
  else return false;
  qa_guest_address color_addr = qa_compat_q2_guest_required_pointer(args, count, color_index);
  qa_vec4 color = {qa_compat_q2_mem_read_u8(memory, color_addr) / 255.0,
    qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, color_addr, 1)) / 255.0,
    qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, color_addr, 2)) / 255.0,
    qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, color_addr, 3)) / 255.0};
  double lifetime = shape_float(args, count, lifetime_index);
  bool depth = qa_compat_q2_guest_integer(args, count, lifetime_index + 1) != 0;
  size_t needed = qa_debug_shape_lines(shape, color, depth, NULL, 0);
  qa_debug_line *lines = malloc((needed ? needed : 1) * sizeof *lines);
  qa_debug_shape_lines(shape, color, depth, lines, needed);
  d->emit(d->emit_ctx, (qa_compat_q2_rerelease_debug_shapes_event){lines, needed, qa_compat_q2_rerelease_debug_lifetime(lifetime)});
  if (out) *out = qa_compat_q2_call_void();
  return true;
}

/* Port of src/compat/q2/rerelease/world-text.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/world_text.h"
#include <stdlib.h>
#include <string.h>

qa_compat_q2_world_text_input qa_compat_q2_q2_world_text(qa_compat_q2_world_text_input input) { return input; }
struct qa_compat_q2_rerelease_world_text_imports {
  qa_compat_q2_memory *memory; void *emit_ctx; void (*emit)(void *, qa_compat_q2_rerelease_world_text_event);
  char text_scratch[128];
};
qa_compat_q2_rerelease_world_text_imports *qa_compat_q2_rerelease_world_text_imports_create(
    qa_compat_q2_memory *memory, void *emit_ctx, void (*emit)(void *, qa_compat_q2_rerelease_world_text_event)) {
  qa_compat_q2_rerelease_world_text_imports *w = calloc(1, sizeof *w); if (!w) return NULL;
  w->memory = memory; w->emit_ctx = emit_ctx; w->emit = emit; return w;
}
void qa_compat_q2_rerelease_world_text_imports_destroy(qa_compat_q2_rerelease_world_text_imports *w) { free(w); }
bool qa_compat_q2_rerelease_world_text_imports_invoke(qa_compat_q2_rerelease_world_text_imports *w, qa_compat_q2_rerelease_import_call call, qa_guest_call_result *out) {
  bool fixed = strcmp(call.name, "Draw_StaticWorldText") == 0;
  if (strcmp(call.api, "game") != 0 || (strcmp(call.name, "Draw_OrientedWorldText") != 0 && !fixed)) return false;
  const qa_guest_call_value *args = call.arguments; size_t count = call.argument_count; size_t offset = fixed ? 1 : 0;
  qa_compat_q2_memory *memory = w->memory;
  qa_guest_address string_addr = qa_compat_q2_guest_required_pointer(args, count, 1 + offset);
  qa_guest_address color_addr = qa_compat_q2_guest_required_pointer(args, count, 2 + offset);
  size_t len = 0;
  for (size_t i = 0; i < 127; i++) {
    uint8_t byte = qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, string_addr, (int64_t)i));
    if (byte == 0) break;
    w->text_scratch[len++] = (char)byte;
  }
  w->text_scratch[len] = 0;
  double floats[2];
  for (int i = 0; i < 2; i++) {
    const qa_guest_call_value *v = qa_compat_q2_guest_argument(args, count, 3 + offset + (size_t)i);
    if (v->kind != QA_GUEST_VALUE_FLOAT32) qa_range_error("Q2 world text requires a source float");
    floats[i] = v->number_value;
  }
  qa_compat_q2_world_text_input input; memset(&input, 0, sizeof input);
  input.origin = qa_compat_q2_mem_read_f32_vector(memory, qa_compat_q2_guest_required_pointer(args, count, 0));
  input.has_angles = fixed;
  if (fixed) input.angles = qa_compat_q2_mem_read_f32_vector(memory, qa_compat_q2_guest_required_pointer(args, count, 1));
  input.text = w->text_scratch;
  input.color = (qa_vec4){qa_compat_q2_mem_read_u8(memory, color_addr) / 255.0,
    qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, color_addr, 1)) / 255.0,
    qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, color_addr, 2)) / 255.0,
    qa_compat_q2_mem_read_u8(memory, qa_compat_q2_mem_offset(memory, color_addr, 3)) / 255.0};
  input.size = floats[0];
  input.depth_test = qa_compat_q2_guest_integer(args, count, 5 + offset) != 0;
  w->emit(w->emit_ctx, (qa_compat_q2_rerelease_world_text_event){qa_compat_q2_q2_world_text(input), floats[1]});
  if (out) *out = qa_compat_q2_call_void();
  return true;
}

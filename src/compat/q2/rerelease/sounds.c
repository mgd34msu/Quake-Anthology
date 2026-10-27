/* Port of src/compat/q2/rerelease/sounds.ts. GPL-2.0-or-later. */
#include "qa/compat/q2/rerelease/sounds.h"
#include <stdlib.h>
#include <string.h>

struct qa_compat_q2_rerelease_sound_imports {
  qa_compat_q2_memory *memory; void *emit_ctx; void (*emit)(void *, qa_compat_q2_rerelease_sound_event);
  void *slot_ctx; double (*source_slot)(void *, qa_guest_address);
};
qa_compat_q2_rerelease_sound_imports *qa_compat_q2_rerelease_sound_imports_create(
    qa_compat_q2_memory *memory, void *emit_ctx, void (*emit)(void *, qa_compat_q2_rerelease_sound_event),
    void *slot_ctx, double (*source_slot)(void *, qa_guest_address)) {
  qa_compat_q2_rerelease_sound_imports *s = calloc(1, sizeof *s); if (!s) return NULL;
  s->memory = memory; s->emit_ctx = emit_ctx; s->emit = emit; s->slot_ctx = slot_ctx; s->source_slot = source_slot; return s;
}
void qa_compat_q2_rerelease_sound_imports_destroy(qa_compat_q2_rerelease_sound_imports *s) { free(s); }
qa_compat_q2_memory *qa_compat_q2_rerelease_sound_imports_get_memory(const qa_compat_q2_rerelease_sound_imports *s) { return s->memory; }
bool qa_compat_q2_rerelease_sound_imports_invoke(qa_compat_q2_rerelease_sound_imports *s, qa_compat_q2_rerelease_import_call call, qa_guest_call_result *out) {
  bool is_sound = strcmp(call.name, "sound") == 0, is_positioned = strcmp(call.name, "positioned_sound") == 0, is_local = strcmp(call.name, "local_sound") == 0;
  if (strcmp(call.api, "game") != 0 || (!is_sound && !is_positioned && !is_local)) return false;
  const qa_guest_call_value *args = call.arguments; size_t count = call.argument_count;
  size_t offset = is_sound ? 0 : is_positioned ? 1 : 2;
  qa_guest_address entity; bool has_entity = qa_compat_q2_guest_pointer(args, count, offset, &entity);
  qa_guest_address origin; bool has_origin = false;
  if (is_positioned) { origin = qa_compat_q2_guest_required_pointer(args, count, 0); has_origin = true; }
  else if (is_local) has_origin = qa_compat_q2_guest_pointer(args, count, 1, &origin);
  qa_compat_q2_rerelease_sound_event event; memset(&event, 0, sizeof event);
  event.has_entity_slot = has_entity;
  if (has_entity) event.entity_slot = s->source_slot(s->slot_ctx, entity);
  event.has_origin = has_origin;
  if (has_origin) event.origin = qa_compat_q2_mem_read_f32_vector(s->memory, origin);
  event.channel = (double)qa_compat_q2_guest_integer(args, count, offset + 1);
  event.sound_index = (double)qa_compat_q2_guest_integer(args, count, offset + 2);
  double floats[3];
  for (int i = 0; i < 3; i++) {
    const qa_guest_call_value *v = qa_compat_q2_guest_argument(args, count, offset + 3 + (size_t)i);
    if (v->kind != QA_GUEST_VALUE_FLOAT32) qa_range_error("Q2 sound requires a source float");
    floats[i] = v->number_value;
  }
  event.volume = floats[0]; event.attenuation = floats[1]; event.time_offset = floats[2];
  if (is_local) {
    event.audience_is_client = true;
    event.client_slot = s->source_slot(s->slot_ctx, qa_compat_q2_guest_required_pointer(args, count, 0));
    event.dupe_key = (double)qa_compat_q2_guest_integer(args, count, 8);
  }
  s->emit(s->emit_ctx, event);
  if (out) *out = qa_compat_q2_call_void();
  return true;
}

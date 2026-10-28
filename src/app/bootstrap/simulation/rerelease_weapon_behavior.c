/* Port of src/app/bootstrap/simulation/rerelease-weapon-behavior.ts (retire + source shell). GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/rerelease_weapon_behavior.h"
#include <stdlib.h>
#include <string.h>
#include "qa/foundation/support.h"
void qa_app_sim_retire_rerelease_weapon_actor(qa_actor_id actor, qa_app_sim_rerelease_retire_state state,
    void (*dispose)(void *ctx, double slot, qa_app_sim_rerelease_binding_entry binding), void *dispose_ctx) {
  for (size_t i = 0; i < *state.retired_count; i++)
    if (qa_same_actor(state.retired[i].actor, actor)) {
      memmove(&state.retired[i], &state.retired[i + 1], (*state.retired_count - i - 1) * sizeof state.retired[0]);
      (*state.retired_count)--; break;
    }
  double slot = 0; bool has_slot = false; size_t slot_at = 0;
  for (size_t i = 0; i < *state.slot_count; i++)
    if (qa_same_actor(state.slots[i].actor, actor)) { has_slot = true; slot = state.slots[i].slot; slot_at = i; break; }
  qa_app_sim_rerelease_binding_entry binding; bool has_binding = false; size_t binding_at = 0;
  if (has_slot) for (size_t i = 0; i < *state.binding_count; i++)
    if (state.bindings[i].slot == slot) { has_binding = true; binding = state.bindings[i]; binding_at = i; break; }
  if (!has_slot || !has_binding || !qa_same_actor(binding.actor, actor)) return;
  memmove(&state.slots[slot_at], &state.slots[slot_at + 1], (*state.slot_count - slot_at - 1) * sizeof state.slots[0]);
  (*state.slot_count)--;
  memmove(&state.bindings[binding_at], &state.bindings[binding_at + 1], (*state.binding_count - binding_at - 1) * sizeof state.bindings[0]);
  (*state.binding_count)--;
  dispose(dispose_ctx, slot, binding);
}
struct qa_app_sim_rerelease_weapon_behavior_source {
  qa_weapon_behavior_definition definition;
  qa_native_weapon_behavior_declaration declaration;
  qa_app_sim_rerelease_weapon_host host;
};
static qa_weapon_behavior_instance_result rr_attach(void *ctx, qa_weapon_behavior_launch launch) {
  qa_app_sim_rerelease_weapon_behavior_source *s = ctx;
  return s->host.attach(s->host.ctx, launch);
}
qa_app_sim_rerelease_weapon_behavior_source *qa_app_sim_rerelease_weapon_behavior_source_create(
    qa_weapon_behavior_definition definition, qa_native_weapon_behavior_declaration declaration,
    qa_app_sim_rerelease_weapon_host host) {
  qa_app_sim_rerelease_weapon_behavior_source *s = calloc(1, sizeof *s);
  if (!s) qa_range_error("Out of memory");
  s->definition = definition; s->declaration = declaration; s->host = host;
  return s;
}
void qa_app_sim_rerelease_weapon_behavior_source_destroy(qa_app_sim_rerelease_weapon_behavior_source *source) { free(source); }
qa_native_weapon_behavior_declaration qa_app_sim_rerelease_weapon_behavior_source_declaration(const qa_app_sim_rerelease_weapon_behavior_source *source) { return source->declaration; }
qa_weapon_behavior_definition qa_app_sim_rerelease_weapon_behavior_source_definition(const qa_app_sim_rerelease_weapon_behavior_source *source) { return source->definition; }
qa_weapon_behavior_source qa_app_sim_rerelease_weapon_behavior_source_as_source(qa_app_sim_rerelease_weapon_behavior_source *source) {
  qa_weapon_behavior_source out; memset(&out, 0, sizeof out);
  out.ctx = source; out.definition = source->definition; out.attach = rr_attach;
  return out;
}
qa_app_sim_rerelease_weapon_behavior_checkpoint qa_app_sim_rerelease_weapon_behavior_source_checkpoint(
    qa_app_sim_rerelease_weapon_behavior_source *source, void (*next_frame)(void *ctx), void *frame_ctx) {
  return source->host.checkpoint(source->host.ctx, next_frame, frame_ctx);
}
void qa_app_sim_rerelease_weapon_behavior_source_restore(qa_app_sim_rerelease_weapon_behavior_source *source,
    qa_app_sim_rerelease_weapon_behavior_checkpoint checkpoint, void (*next_frame)(void *ctx), void *frame_ctx) {
  source->host.restore(source->host.ctx, checkpoint, next_frame, frame_ctx);
}
void qa_app_sim_rerelease_weapon_behavior_source_close(qa_app_sim_rerelease_weapon_behavior_source *source) {
  if (source->host.close) source->host.close(source->host.ctx);
}

/* Port of src/content/q3/base/game/targets.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/targets.h"
#include "qa/foundation/support.h"
void qa_q3_target_location_state_init(qa_q3_target_location_state *s) { s->location = NULL; s->last_update = 0; }
void qa_q3_target_runtime_init(qa_q3_target_runtime *r, void *ctx, qa_q3_entity_pool *entities) {
  r->ctx = ctx; r->entities = entities; qa_q3_target_location_state_init(&r->location);
}
static void unimplemented(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)ctx; (void)e; (void)v; qa_range_error("Q3 target spawn handler requires TargetRuntime host wiring (unported)");
}
static const char *const NAMES[] = {"target_give", "target_remove_powerups", "target_delay", "target_speaker", "target_print",
  "target_laser", "target_score", "target_teleporter", "target_relay", "target_kill", "target_position", "target_location", "target_push"};
size_t qa_q3_target_spawn_handlers(qa_q3_target_runtime *runtime, qa_q3_spawn_handler_entry *out, size_t out_cap) {
  size_t n = sizeof NAMES / sizeof *NAMES;
  for (size_t i = 0; i < n && i < out_cap; i++) { out[i].classname = NAMES[i]; out[i].fn = unimplemented; out[i].ctx = runtime->ctx; }
  return n;
}
void qa_q3_bind_target_save_callbacks(qa_q3_target_runtime *runtime) { (void)runtime; }

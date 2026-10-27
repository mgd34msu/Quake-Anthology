/* Port of src/content/q3/base/game/mover-spawn.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/mover_spawn.h"
#include "qa/foundation/support.h"
void qa_q3_mover_spawn_runtime_init(qa_q3_mover_spawn_runtime *runtime, qa_q3_mover_spawn_host host) { runtime->host = host; }
static void unimplemented(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)ctx; (void)e; (void)v; qa_range_error("Q3 mover spawn handler requires MoverRuntime host wiring (unported)");
}
static const char *const NAMES[] = {"func_static", "func_rotating", "func_bobbing", "func_pendulum", "func_door", "func_plat", "func_button", "func_train"};
size_t qa_q3_create_mover_spawn_handlers(qa_q3_mover_spawn_host host, qa_q3_spawn_handler_entry *out, size_t out_cap) {
  size_t n = sizeof NAMES / sizeof *NAMES;
  for (size_t i = 0; i < n && i < out_cap; i++) { out[i].classname = NAMES[i]; out[i].fn = unimplemented; out[i].ctx = host.ctx; }
  return n;
}

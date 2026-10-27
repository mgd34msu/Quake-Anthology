/* Port of src/content/q3/base/game/misc-spawn.ts. GPL-2.0-or-later. */
#include "qa/content/q3/base/game/misc_spawn.h"
#include "qa/foundation/support.h"
static void unimplemented(void *ctx, qa_q3_game_entity *e, const qa_q3_spawn_variables *v) {
  (void)ctx; (void)e; (void)v; qa_range_error("Q3 misc spawn handler requires MiscSpawnHost wiring (unported)");
}
static const char *const NAMES[] = {"misc_teleporter_dest", "misc_model", "misc_portal_surface", "misc_portal_camera"};
size_t qa_q3_misc_spawn_handlers(qa_q3_misc_spawn_host host, qa_q3_spawn_handler_entry *out, size_t out_cap) {
  size_t n = sizeof NAMES / sizeof *NAMES;
  for (size_t i = 0; i < n && i < out_cap; i++) { out[i].classname = NAMES[i]; out[i].fn = unimplemented; out[i].ctx = host.ctx; }
  return n;
}

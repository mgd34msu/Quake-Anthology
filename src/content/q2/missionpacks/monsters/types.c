/* Port of src/content/q2/missionpacks/monsters/types.ts.
 * GPL-2.0-or-later. */
#include "qa/content/q2/missionpacks/monsters/types.h"

#include <stddef.h>

const char *qa_q2mpm_pack_name(qa_q2mpm_pack pack) {
  return pack == QA_Q2MPM_PACK_XATRIX ? "xatrix" : "rogue";
}

static qa_q2mpm_beam_sink beam_sink;
void qa_q2mpm_set_beam_sink(qa_q2mpm_beam_sink sink) { beam_sink = sink; }
void qa_q2mpm_emit_beam(qa_q2mpm_beam_event event) {
  if (!beam_sink.emit) qa_q2f_error("Q2 mission-pack beam event has no registered presentation sink");
  beam_sink.emit(beam_sink.ctx, event);
}

static qa_q2mpm_callback_resolvers resolvers;
void qa_q2mpm_set_callback_resolvers(qa_q2mpm_callback_resolvers value) { resolvers = value; }
qa_q2f_use qa_q2mpm_use_resolve(const char *name) {
  if (!resolvers.use_resolve) qa_q2f_error("Q2 mission-pack use callback %s has no registered resolver", name);
  return resolvers.use_resolve(resolvers.ctx, name);
}
bool qa_q2mpm_touch_resolve(const char *name, qa_q2f_touch *out) {
  if (!resolvers.touch_resolve) qa_q2f_error("Q2 mission-pack touch callback %s has no registered resolver", name);
  *out = resolvers.touch_resolve(resolvers.ctx, name);
  return out->fn != NULL;
}

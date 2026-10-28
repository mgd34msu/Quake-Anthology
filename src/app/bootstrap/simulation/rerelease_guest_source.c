/* Port of src/app/bootstrap/simulation/rerelease-guest-source.ts. GPL-2.0-or-later. */
#include "qa/app/bootstrap/simulation/rerelease_guest_source.h"
#include <stdlib.h>
#include "qa/foundation/support.h"
struct qa_app_sim_rerelease_guest_source {
  qa_app_sim_prepared_rerelease_guest prepared;
  qa_app_sim_rerelease_guest_source_options options;
};
qa_app_sim_prepared_rerelease_guest qa_app_sim_prepare_rerelease_guest_with_native_primary(
    qa_execution_module module, const void *primary, qa_app_sim_rerelease_guest_prepare_host host) {
  return host.prepare(host.ctx, module, primary);
}
qa_app_sim_prepared_rerelease_guest qa_app_sim_prepare_rerelease_guest(
    qa_execution_module module, qa_app_sim_rerelease_guest_prepare_host host) {
  return qa_app_sim_prepare_rerelease_guest_with_native_primary(module, NULL, host);
}
qa_app_sim_rerelease_guest_source *qa_app_sim_rerelease_guest_source_create(
    qa_app_sim_prepared_rerelease_guest prepared, qa_app_sim_rerelease_guest_source_options options) {
  qa_app_sim_rerelease_guest_source *s = calloc(1, sizeof *s);
  if (!s) qa_range_error("Out of memory");
  s->prepared = prepared; s->options = options;
  return s;
}
void qa_app_sim_rerelease_guest_source_destroy(qa_app_sim_rerelease_guest_source *source) { free(source); }
qa_app_sim_prepared_rerelease_guest qa_app_sim_rerelease_guest_source_prepared(qa_app_sim_rerelease_guest_source *source) { return source->prepared; }
void qa_app_sim_rerelease_guest_source_load_loading(qa_app_sim_rerelease_guest_source *source,
    void (*next_frame)(void *frame_ctx), void *frame_ctx) {
  source->options.load(source->options.ctx, source, next_frame, frame_ctx);
}
qa_app_sim_rerelease_guest_world *qa_app_sim_rerelease_guest_source_create_map(qa_app_sim_rerelease_guest_source *source) {
  return source->options.create_map(source->options.ctx, source);
}

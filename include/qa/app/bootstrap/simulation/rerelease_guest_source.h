/* Port of src/app/bootstrap/simulation/rerelease-guest-source.ts.
 * Native module loading/execution is the compat guest runtime's;
 * preparation, export validation and checkpoint admission run
 * through the host bundle. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SOURCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/rerelease_guest_world.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
typedef struct qa_app_sim_prepared_rerelease_guest {
  qa_execution_module module;
  const void *image; /* native module image (compat opaque) */
  bool has_primary;
  const void *primary_profile;
  const char *primary_requested_path;
  const char *primary_digest;
} qa_app_sim_prepared_rerelease_guest;
typedef struct qa_app_sim_rerelease_guest_prepare_host {
  void *ctx;
  /* PreparedClassicGuest construction + native primary validation. */
  qa_app_sim_prepared_rerelease_guest (*prepare)(void *ctx, qa_execution_module module, const void *primary);
} qa_app_sim_rerelease_guest_prepare_host;
/* primary may be NULL (TS primary?: NativePrimaryProfile). */
qa_app_sim_prepared_rerelease_guest qa_app_sim_prepare_rerelease_guest_with_native_primary(
    qa_execution_module module, const void *primary, qa_app_sim_rerelease_guest_prepare_host host);
qa_app_sim_prepared_rerelease_guest qa_app_sim_prepare_rerelease_guest(
    qa_execution_module module, qa_app_sim_rerelease_guest_prepare_host host);
typedef struct qa_app_sim_rerelease_guest_source qa_app_sim_rerelease_guest_source;
typedef struct qa_app_sim_rerelease_guest_source_options {
  const void *simulation; /* SharedSimulation (runtime slice) */
  struct qa_app_sim_rerelease_guest_services *services;
  void *ctx;
  /* guest loading + per-map world construction */
  void (*load)(void *ctx, qa_app_sim_rerelease_guest_source *source,
      void (*next_frame)(void *frame_ctx), void *frame_ctx);
  qa_app_sim_rerelease_guest_world *(*create_map)(void *ctx, qa_app_sim_rerelease_guest_source *source);
} qa_app_sim_rerelease_guest_source_options;
qa_app_sim_rerelease_guest_source *qa_app_sim_rerelease_guest_source_create(
    qa_app_sim_prepared_rerelease_guest prepared, qa_app_sim_rerelease_guest_source_options options);
void qa_app_sim_rerelease_guest_source_destroy(qa_app_sim_rerelease_guest_source *source);
qa_app_sim_prepared_rerelease_guest qa_app_sim_rerelease_guest_source_prepared(qa_app_sim_rerelease_guest_source *source);
void qa_app_sim_rerelease_guest_source_load_loading(qa_app_sim_rerelease_guest_source *source,
    void (*next_frame)(void *frame_ctx), void *frame_ctx);
qa_app_sim_rerelease_guest_world *qa_app_sim_rerelease_guest_source_create_map(qa_app_sim_rerelease_guest_source *source);
#endif

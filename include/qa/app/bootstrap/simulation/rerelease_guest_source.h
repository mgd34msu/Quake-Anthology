/* Port of src/app/bootstrap/simulation/rerelease-guest-source.ts.
 * Owns only the guest address space and ABI lifetime; supplied engine
 * authorities own the world. The guest process is the shared
 * qa_app_sim_native_guest (classic_guest_source.h). GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SOURCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_RERELEASE_GUEST_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/classic_guest_source.h"
#include "qa/compat/q2/rerelease/host.h"

typedef struct qa_app_sim_prepared_rerelease_guest {
  const char *edition; /* "rerelease" */
  qa_resolved_execution_module execution; /* artifact borrowed */
  uint8_t *bytes; size_t length;           /* owned */
  bool has_primary;
  qa_compat_q2_native_primary_declaration primary;
  qa_compat_q2_native_compatibility_result *compatibility; /* owned, NULL without a declaration */
} qa_app_sim_prepared_rerelease_guest;
void qa_app_sim_prepare_rerelease_guest(const qa_resolved_execution_module *execution, qa_content_mounted *mounts, qa_app_sim_prepared_rerelease_guest *out);
void qa_app_sim_prepared_rerelease_guest_free(qa_app_sim_prepared_rerelease_guest *prepared);

/* TS RereleaseGuestSourceOptions: the host options minus runner,
 * getGameApi, getCgameApi, services, pickups and worldProfile (which the
 * source fills), plus pickups admission, services(memory) and the clock. */
typedef struct qa_app_sim_rerelease_guest_source_options {
  qa_compat_q2_rerelease_q2_host_options host;
  const qa_original_pickup_admission *pickups; /* optional */
  void *services_ctx;
  qa_compat_q2_rerelease_core_services (*services)(void *ctx, qa_compat_q2_memory *memory);
  /* Required clock capabilities (ctx shared by the three). */
  void *clock_ctx;
  bool (*now_milliseconds)(void *ctx, double *out);
  bool (*performance_counter)(void *ctx, int64_t *out);
  int64_t performance_frequency;
} qa_app_sim_rerelease_guest_source_options;

typedef struct qa_app_sim_rerelease_guest_source qa_app_sim_rerelease_guest_source;
qa_app_sim_rerelease_guest_source *qa_app_sim_rerelease_guest_source_create(const qa_app_sim_prepared_rerelease_guest *prepared,
                                                                            const qa_app_sim_rerelease_guest_source_options *options);
void qa_app_sim_rerelease_guest_source_destroy(qa_app_sim_rerelease_guest_source *source);
qa_compat_q2_rerelease_q2_guest_host *qa_app_sim_rerelease_guest_source_host(qa_app_sim_rerelease_guest_source *source);
qa_compat_q2_memory *qa_app_sim_rerelease_guest_source_memory(qa_app_sim_rerelease_guest_source *source);
qa_app_sim_native_guest *qa_app_sim_rerelease_guest_source_guest(qa_app_sim_rerelease_guest_source *source);
qa_windows_guest_runtime *qa_app_sim_rerelease_guest_source_runtime(qa_app_sim_rerelease_guest_source *source);
qa_guest_address qa_app_sim_rerelease_guest_source_image_base(qa_app_sim_rerelease_guest_source *source);
qa_guest_address qa_app_sim_rerelease_guest_source_entry(qa_app_sim_rerelease_guest_source *source, const char *name);
void qa_app_sim_rerelease_guest_source_init(qa_app_sim_rerelease_guest_source *source);
void qa_app_sim_rerelease_guest_source_init_loading(qa_app_sim_rerelease_guest_source *source, void (*next_frame)(void *), void *next_ctx);
void qa_app_sim_rerelease_guest_source_close(qa_app_sim_rerelease_guest_source *source);
#endif

/* Port of src/app/bootstrap/simulation/q2-native-world.ts. Prepared
 * guests are the sibling slices' opaque sources; the PE ABI decides
 * the edition through the caller-supplied prepare callbacks.
 * GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q2_NATIVE_WORLD_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q2_NATIVE_WORLD_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
typedef struct qa_app_sim_prepared_q2_native_guest {
  const char *edition; /* "classic"|"rerelease" */
  bool has_primary;
  const void *primary_profile;       /* NativePrimaryProfile */
  const char *primary_requested_path;
  const char *primary_digest;
  qa_execution_module execution;
  const void *source; /* PreparedClassicGuest|PreparedRereleaseGuest */
} qa_app_sim_prepared_q2_native_guest;
typedef struct qa_app_sim_q2_guest_prepare_host {
  void *ctx;
  /* mounts.read(artifact): malloc'd bytes or NULL. */
  uint8_t *(*read_artifact)(void *ctx, const qa_resolved_resource_reference *artifact, size_t *out_length);
  /* parsePe(bytes).abi (guest/pe headers cannot mix with
   * contracts/execution.h in one TU, so parsing is caller-side). */
  void (*pe_abi)(void *ctx, const uint8_t *bytes, size_t length, qa_native_abi *out);
  qa_app_sim_prepared_q2_native_guest (*prepare_classic)(void *ctx, qa_execution_module execution);
  qa_app_sim_prepared_q2_native_guest (*prepare_rerelease)(void *ctx, qa_execution_module execution);
  const void *(*builtin_primary)(void *ctx, const char *digest, const char *edition);
} qa_app_sim_q2_guest_prepare_host;
/* Synchronous form of the TS async prepare (PORTING.md). */
qa_app_sim_prepared_q2_native_guest qa_app_sim_prepare_q2_guest(qa_execution_module execution, qa_app_sim_q2_guest_prepare_host host);
/* revision is malloc'd into scratch when a primary exists (caller
 * copies before the next call). */
qa_module_identity qa_app_sim_native_module_identity(qa_app_sim_prepared_q2_native_guest prepared, char *scratch, size_t scratch_size);
/* WeakMap cache becomes a small identity cache in the .c file. */
const void *qa_app_sim_prepared_native_primary(qa_app_sim_prepared_q2_native_guest *prepared, qa_app_sim_q2_guest_prepare_host host);
#endif

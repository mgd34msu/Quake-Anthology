/* Port of src/app/bootstrap/simulation/classic-guest-source.ts.
 *
 * TS constructs the guest address space, CPU, GuestCallRunner and
 * WindowsGuestRuntime inline in ClassicGuestSource.create (and again in
 * RereleaseGuestSource.create). In C that composition is
 * qa_app_sim_native_guest below, shared by both sources. The compat Q2
 * hosts take the guest memory and runner through the
 * qa_compat_q2_memory / qa_compat_q2_runner tables (classic/layout.h);
 * qa_app_sim_native_guest fills those tables over the real
 * SparseGuestMemory, GuestCallbackTable and GuestCallRunner, converting
 * between the contracts and guest value spellings.
 *
 * Async *Loading variants run synchronously with a next-frame callback
 * (docs/PORTING.md). TS AggregateError cleanup chains have no C
 * counterpart: the first fatal error aborts. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_SOURCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_CLASSIC_GUEST_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/compat/q2/classic/host.h"
#include "qa/compat/q2/compatibility.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"
#include "qa/guest/abi/runner.h"
#include "qa/guest/core/callbacks.h"
#include "qa/guest/core/memory.h"
#include "qa/guest/pe/image.h"
#include "qa/guest/runtime/windows/runtime.h"
#include "qa/guest/x64/cpu.h"
#include "qa/guest/x86/cpu.h"

/* --- the guest process both sources build --- */
typedef struct qa_app_sim_native_guest_options {
  qa_module_identity module;
  int pointer_bytes;           /* 4 classic, 8 rerelease */
  bool managed;                /* SparseGuestMemory.createManaged + managed processor */
  const uint8_t *bytes; size_t length;
  bool has_base; uint64_t base; /* mapPeImage base */
  uint32_t x87_control_word;
  uint64_t stack_top;          /* bytes above the stack allocation's base */
  const char *stack_label, *return_label;
  /* GuestCallRunner variadicLayouts; NULL when the ABI has none. */
  void *variadic_ctx;
  size_t (*variadic_layouts)(void *ctx, qa_callback_id id, const qa_guest_call_value *fixed, size_t fixed_count,
                             qa_guest_value_layout *out, size_t capacity);
  qa_windows_capabilities capabilities;
} qa_app_sim_native_guest_options;

typedef struct qa_app_sim_native_guest qa_app_sim_native_guest;
/* Maps the image, allocates the stack and return trap, and attaches the
 * runner to a WindowsGuestRuntime. NULL only on OOM. */
qa_app_sim_native_guest *qa_app_sim_native_guest_create(const qa_app_sim_native_guest_options *options);
/* Frees the host-side structures; guest mappings go with the memory. */
void qa_app_sim_native_guest_destroy(qa_app_sim_native_guest *guest);
qa_compat_q2_memory *qa_app_sim_native_guest_memory(qa_app_sim_native_guest *guest);
qa_compat_q2_runner *qa_app_sim_native_guest_runner(qa_app_sim_native_guest *guest);
qa_sparse_guest_memory *qa_app_sim_native_guest_sparse(qa_app_sim_native_guest *guest);
qa_guest_call_runner *qa_app_sim_native_guest_call_runner(qa_app_sim_native_guest *guest);
qa_guest_callback_table *qa_app_sim_native_guest_callbacks(qa_app_sim_native_guest *guest);
qa_guest_processor_state *qa_app_sim_native_guest_state(qa_app_sim_native_guest *guest);
qa_windows_guest_runtime *qa_app_sim_native_guest_runtime(qa_app_sim_native_guest *guest);
qa_pe_image *qa_app_sim_native_guest_image(qa_app_sim_native_guest *guest);
/* TS image.base and resolvePeExport(image, {name}).address. */
qa_guest_address qa_app_sim_native_guest_image_base(qa_app_sim_native_guest *guest);
qa_guest_address qa_app_sim_native_guest_entry(qa_app_sim_native_guest *guest, const char *name);
/* GuestCallRunner.instructionsExecuted. */
uint64_t qa_app_sim_native_guest_instructions(qa_app_sim_native_guest *guest);
/* runtime.initialize / runtime.detach with a native-guest context at target. */
void qa_app_sim_native_guest_initialize(qa_app_sim_native_guest *guest, qa_guest_address target, qa_native_call_abi abi, double budget);
void qa_app_sim_native_guest_detach(qa_app_sim_native_guest *guest, qa_guest_address target, qa_native_call_abi abi, double budget);
/* TS releaseMemory: unmap every mapping. */
void qa_app_sim_native_guest_release_memory(qa_app_sim_native_guest *guest);
size_t qa_app_sim_native_guest_mapping_count(qa_app_sim_native_guest *guest);
/* Address conversions between the contracts and guest spellings. */
qa_guest_address qa_app_sim_native_guest_address(const qa_app_sim_native_guest *guest, uint64_t byte_offset);

/* --- PreparedClassicGuest --- */
typedef struct qa_app_sim_prepared_classic_guest {
  const char *edition; /* "classic" */
  /* execution.artifact borrows the caller's qa_resolved_resource_reference. */
  qa_resolved_execution_module execution;
  uint8_t *bytes; size_t length; /* owned */
  bool has_primary;
  qa_compat_q2_native_primary_declaration primary;
  /* Owned storage behind primary.profile (NULL without a declaration). */
  qa_compat_q2_native_compatibility_result *compatibility;
} qa_app_sim_prepared_classic_guest;
/* TS prepareClassicGuest over mounted content. Aborts as the TS throws. */
void qa_app_sim_prepare_classic_guest(const qa_resolved_execution_module *execution, qa_content_mounted *mounts,
                                      qa_app_sim_prepared_classic_guest *out);
void qa_app_sim_prepared_classic_guest_free(qa_app_sim_prepared_classic_guest *prepared);
/* TS readNativeCompatibility: NULL when the content declares no profile
 * for this artifact (owned result otherwise). */
qa_compat_q2_native_compatibility_result *qa_app_sim_read_native_compatibility(qa_content_mounted *mounts,
                                                                               const qa_resolved_execution_module *execution);
/* The shared body of prepareClassicGuest/prepareRereleaseGuest: reads the
 * artifact, checks its PE ABI, reads and validates the primary
 * declaration (NULL when none; primary_out filled otherwise). */
qa_compat_q2_native_compatibility_result *qa_app_sim_prepare_native_artifact(const qa_resolved_execution_module *execution, qa_content_mounted *mounts,
                                                                           const char *edition, const char *abi_message, uint8_t **bytes, size_t *length,
                                                                           qa_compat_q2_native_primary_declaration *primary_out);
void qa_app_sim_native_compatibility_free(qa_compat_q2_native_compatibility_result *result);
/* TS nativeModuleIdentity(prepared). revision is written into scratch
 * when a primary declaration exists. */
qa_module_identity qa_app_sim_prepared_module_identity(const qa_resolved_execution_module *execution, bool has_primary,
                                                       const qa_compat_q2_native_primary_declaration *primary,
                                                       char *scratch, size_t scratch_size);

/* --- ClassicGuestSource --- */
typedef struct qa_app_sim_classic_guest_source_options {
  /* ClassicQ2GuestHostOptions.importBoundary; NULL is TS undefined. */
  void *import_boundary_ctx;
  qa_guest_call_result (*import_boundary)(void *ctx, const char *name, const qa_guest_call_value *arguments, size_t count,
                                          qa_guest_call_result (*invoke)(void *), void *invoke_ctx);
  /* TS services(memory): fills the engine services the host imports call. */
  void *services_ctx;
  void (*services)(void *ctx, qa_compat_q2_memory *memory, qa_compat_q2_classic_engine_services *out);
  qa_windows_capabilities capabilities;
  bool has_instruction_budget; double instruction_budget;
} qa_app_sim_classic_guest_source_options;

typedef struct qa_app_sim_classic_guest_source qa_app_sim_classic_guest_source;
qa_app_sim_classic_guest_source *qa_app_sim_classic_guest_source_create(const qa_app_sim_prepared_classic_guest *prepared,
                                                                        const qa_app_sim_classic_guest_source_options *options);
/* Frees the C structures (after close or discard). */
void qa_app_sim_classic_guest_source_destroy(qa_app_sim_classic_guest_source *source);
qa_compat_q2_classic_guest_host *qa_app_sim_classic_guest_source_host(qa_app_sim_classic_guest_source *source);
qa_windows_guest_runtime *qa_app_sim_classic_guest_source_runtime(qa_app_sim_classic_guest_source *source);
qa_compat_q2_memory *qa_app_sim_classic_guest_source_memory(qa_app_sim_classic_guest_source *source);
qa_app_sim_native_guest *qa_app_sim_classic_guest_source_guest(qa_app_sim_classic_guest_source *source);
qa_guest_address qa_app_sim_classic_guest_source_image_base(qa_app_sim_classic_guest_source *source);
qa_guest_address qa_app_sim_classic_guest_source_entry(qa_app_sim_classic_guest_source *source, const char *name);
void qa_app_sim_classic_guest_source_init(qa_app_sim_classic_guest_source *source);
void qa_app_sim_classic_guest_source_init_loading(qa_app_sim_classic_guest_source *source, void (*next_frame)(void *), void *next_ctx);
/* Candidate disposal runs no game exports or DLL detach callbacks. */
void qa_app_sim_classic_guest_source_discard(qa_app_sim_classic_guest_source *source);
void qa_app_sim_classic_guest_source_close(qa_app_sim_classic_guest_source *source);
size_t qa_app_sim_classic_guest_source_open_files(qa_app_sim_classic_guest_source *source);
#endif

/* Port of src/app/bootstrap/simulation/q3/native-guest-source.ts.
 * Q3NativeGuestSource hosts one native Q3 qagame module in a private guest
 * process: a Windows i386 PE or a Linux i386 ELF runs through
 * dllEntry(syscall)/vmMain with every trap decoded by the same table that
 * serves interpreted modules; the Quake Live ELF form runs through
 * dllEntry(output, imports, version) and its export table. It implements the
 * Q3GameEngine table (engine.h). Async entry points are synchronous;
 * AggregateError collection raises "AggregateError" with the TS message after
 * every step has run. GPL-2.0-or-later. */
#ifndef QA_APP_BOOTSTRAP_SIMULATION_Q3_NATIVE_GUEST_SOURCE_H
#define QA_APP_BOOTSTRAP_SIMULATION_Q3_NATIVE_GUEST_SOURCE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/app/bootstrap/simulation/q3/engine.h"
#include "qa/compat/q3/native/engine_services.h"
#include "qa/compat/q3/native/imports.h"
#include "qa/compat/qvm/module.h"
#include "qa/compat/qvm/syscalls.h"
#include "qa/content/mounts.h"
#include "qa/contracts/content.h"
#include "qa/guest/abi/runner.h"
#include "qa/guest/core/memory.h"
#include "qa/guest/runtime/windows/contracts.h"

/* TS Q3NativeProfile: "q3-pe" | "q3-elf" | "quake-live-elf" (exact strings). */
typedef const char *qa_app_q3_native_profile_name;

/* TS Q3NativeImage. bytes and module strings are borrowed from the owner (a PreparedQ3Native). */
typedef struct qa_app_q3_native_image {
  const uint8_t *bytes;
  size_t length;
  qa_app_q3_native_profile_name profile;
  qa_module_identity module;
} qa_app_q3_native_image;
/* TS PreparedQ3Native: the image of a selected execution module. Owns the bytes and module strings;
 * execution is borrowed from the recipe. */
typedef struct qa_app_prepared_q3_native {
  qa_app_q3_native_image image;
  const qa_resolved_execution_module *execution;
  uint8_t *owned_bytes;
  char *owned_strings[4];
} qa_app_prepared_q3_native;
void qa_app_prepared_q3_native_destroy(qa_app_prepared_q3_native *prepared);

/* q3NativeProfile(bytes): raises "Native Q3 server game must be a Windows PE or a Linux ELF image". */
qa_app_q3_native_profile_name qa_app_q3_native_profile(const uint8_t *bytes, size_t length);
/* prepareQ3Native(execution, mounts): provider admission; raises the TS errors. *out is owned. */
void qa_app_prepare_q3_native(const qa_resolved_execution_module *execution, qa_content_mounted *mounts,
                              qa_app_prepared_q3_native *out);

/* TS NativeQ3GameServices: NativeQ3EngineServices plus milliseconds(); both use engine.ctx. */
typedef struct qa_app_q3_native_game_services {
  qa_compat_q3_native_engine_services engine;
  double (*milliseconds)(void *ctx);
} qa_app_q3_native_game_services;

/* TS Q3NativeGuestOptions. Pointers are borrowed for the source's lifetime; NULL is TS undefined. */
typedef struct qa_app_q3_native_guest_options {
  const qa_app_q3_native_image *prepared;
  /* Q3 1.32 modules: the server game's shared trap table. */
  void *traps_ctx;
  qa_qvm_host_result (*traps)(void *ctx, const qa_qvm_host_call *call);
  /* Quake Live API 10 modules: the engine services behind the import table. */
  const qa_app_q3_native_game_services *services;
  /* Saves and restores the server game's own record; required to checkpoint. */
  const qa_qvm_host_state *host_state;
  const qa_windows_capabilities *windows;
  bool has_instruction_budget;
  int64_t instruction_budget;
} qa_app_q3_native_guest_options;

typedef struct qa_app_q3_native_guest_source qa_app_q3_native_guest_source;
/* Q3NativeGuestSource.create; construction errors release the process and raise. */
qa_app_q3_native_guest_source *qa_app_q3_native_guest_source_create(const qa_app_q3_native_guest_options *options);
/* The Q3GameEngine view (borrowed). destroy through it frees the source. */
qa_app_q3_game_engine *qa_app_q3_native_guest_source_engine(qa_app_q3_native_guest_source *source);
/* The source behind an engine, or NULL when the engine is not a native module. */
qa_app_q3_native_guest_source *qa_app_q3_native_guest_source_of(qa_app_q3_game_engine *engine);
qa_app_q3_native_profile_name qa_app_q3_native_guest_source_profile(const qa_app_q3_native_guest_source *source);
qa_sparse_guest_memory *qa_app_q3_native_guest_source_memory(qa_app_q3_native_guest_source *source);
qa_guest_call_runner *qa_app_q3_native_guest_source_runner(qa_app_q3_native_guest_source *source);
/* TS coverage: Quake Live import slots the module has reached (empty for the 1.32 forms). Returns the count; fills
 * up to capacity. */
size_t qa_app_q3_native_guest_source_coverage(const qa_app_q3_native_guest_source *source,
                                              qa_compat_q3_native_service_coverage *out, size_t capacity);
/* close(): shuts the running game down, then retires the process. */
void qa_app_q3_native_guest_source_close(qa_app_q3_native_guest_source *source);
#endif

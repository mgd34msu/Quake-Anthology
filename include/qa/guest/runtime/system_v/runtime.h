/*
 * Port of src/guest/runtime/system-v/runtime.ts: one Linux guest process
 * and thread (SystemVGuestRuntime). The class embeds its
 * qa_system_v_service_host (ctx is the runtime). Symbol/image/heap/TLS
 * Maps become owned lists in insertion order. Loaded images are
 * caller-owned qa_elf_guest_image values tracked by pointer identity;
 * coverage/traces borrow runtime-owned strings. Plain TS Errors abort
 * with the same message, so the TS try/catch state recovery around
 * initialize/finalize/load protect-restores is unobservable and omitted
 * (state is still set to "failed" only where execution can continue,
 * which never happens after an abort). GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_SYSTEM_V_RUNTIME_H
#define QA_GUEST_RUNTIME_SYSTEM_V_RUNTIME_H

#include "qa/guest/elf.h"
#include "qa/guest/runtime/system_v/contracts.h"
#include "qa/guest/runtime/system_v/iostream.h"

typedef enum qa_system_v_lifecycle_state {
  QA_SV_LOADED = 0, QA_SV_INITIALIZING, QA_SV_INITIALIZED, QA_SV_FINALIZING, QA_SV_FINALIZED,
  QA_SV_FAILED
} qa_system_v_lifecycle_state;
const char *qa_system_v_lifecycle_state_name(qa_system_v_lifecycle_state state);

typedef struct qa_system_v_lifecycle_trace_entry {
  qa_guest_module_identity image;
  bool finalize; /* false: initialize */
  qa_guest_core_address target;
} qa_system_v_lifecycle_trace_entry;

typedef struct qa_system_v_load_options {
  const uint8_t *bytes;
  size_t bytes_len;
  qa_guest_module_identity module;
  uint64_t load_bias;
} qa_system_v_load_options;

typedef struct qa_system_v_guest_runtime qa_system_v_guest_runtime;
struct qa_system_v_guest_runtime {
  qa_system_v_runtime_options options; /* borrowed pointers, as TS options */
  qa_system_v_service_host host;
  qa_elf_unique_symbols unique_symbols;
  qa_guest_core_address thread_pointer;
  qa_guest_core_address empty_string_representation;
  qa_system_v_iostreams *iostreams; /* owned */
  /* private state (runtime.c layout companions are opaque pointers) */
  qa_guest_core_address errno_address_;
  qa_guest_core_address dtv;
  qa_guest_core_address argv_address, envp_address;
  void *symbols; size_t symbol_count, symbol_cap;
  void *images; size_t image_count, image_cap;
  void *heap; size_t heap_count, heap_cap;
  void *tls; size_t tls_count, tls_cap;
  void *destructors; size_t destructor_count, destructor_cap;
  qa_system_v_lifecycle_trace_entry *lifecycle_trace; size_t lifecycle_trace_count, lifecycle_trace_cap;
  qa_guest_call_runner *runner_or_null;
  bool loading;
  int64_t budget;
  uint64_t tls_used;
};

void qa_system_v_guest_runtime_init(qa_system_v_guest_runtime *runtime,
                                    const qa_system_v_runtime_options *options);
void qa_system_v_guest_runtime_destroy(qa_system_v_guest_runtime *runtime);
/* TS getter: aborts "Attach the shared GuestCallRunner..." when unset. */
qa_guest_call_runner *qa_system_v_guest_runtime_runner(qa_system_v_guest_runtime *runtime);
size_t qa_system_v_guest_runtime_image_count(const qa_system_v_guest_runtime *runtime);
qa_elf_guest_image *qa_system_v_guest_runtime_image(const qa_system_v_guest_runtime *runtime, size_t index);
size_t qa_system_v_guest_runtime_coverage_count(const qa_system_v_guest_runtime *runtime);
qa_system_v_import_coverage qa_system_v_guest_runtime_coverage(const qa_system_v_guest_runtime *runtime,
                                                               size_t index);
/* Malloc'd array of borrowed names; caller frees the array only. */
const char **qa_system_v_guest_runtime_dependencies(const qa_system_v_guest_runtime *runtime,
                                                    size_t *out_count);
void qa_system_v_guest_runtime_attach_runner(qa_system_v_guest_runtime *runtime,
                                             qa_guest_call_runner *runner);
/* layouts_out has capacity parameter_count; out borrows it. */
void qa_system_v_guest_runtime_signature(qa_system_v_guest_runtime *runtime,
                                         const qa_abi_storage *parameters, size_t parameter_count,
                                         bool result_is_void, qa_abi_storage result,
                                         qa_abi_value_layout *layouts_out,
                                         qa_guest_call_signature *out);
qa_guest_import_resolver qa_system_v_guest_runtime_resolver(qa_system_v_guest_runtime *runtime);
qa_guest_import_resolution qa_system_v_guest_runtime_resolve(qa_system_v_guest_runtime *runtime,
                                                             const qa_guest_import *import_,
                                                             const qa_guest_image *requesting);
bool qa_system_v_guest_runtime_resolve_symbol_size(qa_system_v_guest_runtime *runtime,
                                                   const qa_guest_import *import_, size_t *out);
void qa_system_v_guest_runtime_load(qa_system_v_guest_runtime *runtime,
                                    const qa_system_v_load_options *options,
                                    qa_elf_guest_image *out_image);
qa_guest_core_address qa_system_v_guest_runtime_tls_address(qa_system_v_guest_runtime *runtime,
                                                       uint64_t module_id, int64_t offset);
void qa_system_v_guest_runtime_initialize(qa_system_v_guest_runtime *runtime, qa_elf_guest_image *image,
                                          const qa_system_v_initialize_options *options);
void qa_system_v_guest_runtime_finalize(qa_system_v_guest_runtime *runtime, qa_elf_guest_image *image,
                                        const qa_system_v_initialize_options *options);
/* TS heapState: malloc'd blocks in heap-table order (heap 0); caller frees. */
qa_native_heap_block *qa_system_v_guest_runtime_heap_state(const qa_system_v_guest_runtime *runtime, size_t *count);
/* TS restoreHeapState: every block must lie in a mapping already restored. */
void qa_system_v_guest_runtime_restore_heap_state(qa_system_v_guest_runtime *runtime, const qa_native_heap_block *blocks,
                                                  size_t count);

#endif

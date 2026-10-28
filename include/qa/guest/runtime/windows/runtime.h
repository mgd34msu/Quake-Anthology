/*
 * Port of src/guest/runtime/windows/runtime.ts: one emulated Windows
 * thread (WindowsGuestRuntime). The class embeds its
 * qa_windows_service_host (ctx is the runtime). Import/data/library/
 * allocation Maps become owned lists in insertion order; images are
 * caller-owned qa_pe_image values tracked by pointer. The TS
 * try/catch failure counters (failed/lastFailure) cannot observe an
 * abort in C, so they stay 0/NULL when an invocation aborts.
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_RUNTIME_WINDOWS_RUNTIME_H
#define QA_GUEST_RUNTIME_WINDOWS_RUNTIME_H

#include "qa/guest/pe/image.h"
#include "qa/guest/runtime/windows/contracts.h"

typedef struct qa_windows_guest_runtime qa_windows_guest_runtime;
struct qa_windows_guest_runtime {
  qa_windows_runtime_options options; /* borrowed pointers, as TS options */
  qa_windows_service_host host;
  qa_guest_core_address teb, peb, static_tls;
  /* private state (runtime.c layout companions are opaque pointers) */
  void *imports; size_t import_count, import_cap;
  void *data; size_t data_count, data_cap;
  void *requested; size_t requested_count, requested_cap;
  void *libraries; size_t library_count, library_cap;
  qa_pe_image **images; size_t image_count, image_cap;
  void *library_references; size_t library_reference_count, library_reference_cap;
  void *prepared; size_t prepared_count, prepared_cap;
  void *initialized; size_t initialized_count, initialized_cap;
  void *allocations; size_t allocation_count, allocation_cap;
  void *tls_blocks; size_t tls_block_count, tls_block_cap;
  uint64_t *cfg_targets; size_t cfg_target_count, cfg_target_cap;
  bool has_cfg_check, has_cfg_dispatch;
  qa_guest_core_address cfg_check, cfg_dispatch;
  qa_guest_call_runner *runner_or_null;
  int64_t budget;
};

void qa_windows_guest_runtime_init(qa_windows_guest_runtime *runtime,
                                   const qa_windows_runtime_options *options);
void qa_windows_guest_runtime_destroy(qa_windows_guest_runtime *runtime);
/* TS getter: aborts "Attach the shared GuestCallRunner..." when unset. */
qa_guest_call_runner *qa_windows_guest_runtime_runner(qa_windows_guest_runtime *runtime);
size_t qa_windows_guest_runtime_coverage_count(const qa_windows_guest_runtime *runtime);
qa_windows_import_coverage qa_windows_guest_runtime_coverage(const qa_windows_guest_runtime *runtime,
                                                             size_t index);
void qa_windows_guest_runtime_attach_runner(qa_windows_guest_runtime *runtime,
                                            qa_guest_call_runner *runner);
/* layouts_out has capacity parameter_count; out borrows it. */
void qa_windows_guest_runtime_signature(qa_windows_guest_runtime *runtime, const char *library,
                                        const qa_abi_storage *parameters, size_t parameter_count,
                                        bool result_is_void, qa_abi_storage result,
                                        qa_abi_value_layout *layouts_out,
                                        qa_guest_call_signature *out);
qa_guest_core_address qa_windows_guest_runtime_register_data(qa_windows_guest_runtime *runtime,
                                                        const char *library, const char *name,
                                                        const uint8_t *bytes, size_t len);
qa_guest_import_resolver qa_windows_guest_runtime_resolver(qa_windows_guest_runtime *runtime);
qa_guest_import_resolution qa_windows_guest_runtime_resolve(qa_windows_guest_runtime *runtime,
                                                            const qa_guest_import *import_,
                                                            const qa_guest_image *requesting);
void qa_windows_guest_runtime_prepare_image(qa_windows_guest_runtime *runtime, qa_pe_image *image);
void qa_windows_guest_runtime_initialize(qa_windows_guest_runtime *runtime, qa_pe_image *image,
                                         const qa_windows_initialize_options *options);
void qa_windows_guest_runtime_detach(qa_windows_guest_runtime *runtime, qa_pe_image *image,
                                     const qa_windows_initialize_options *options);
bool qa_windows_guest_runtime_resolve_address(qa_windows_guest_runtime *runtime, const char *library,
                                              const char *name, qa_guest_core_address *out);
bool qa_windows_guest_runtime_library_handle(qa_windows_guest_runtime *runtime, const char *library,
                                             qa_guest_core_address *out);
bool qa_windows_guest_runtime_load_library(qa_windows_guest_runtime *runtime, const char *library,
                                           qa_guest_core_address *out);
bool qa_windows_guest_runtime_free_library(qa_windows_guest_runtime *runtime, qa_guest_core_address handle);
bool qa_windows_guest_runtime_allocate(qa_windows_guest_runtime *runtime, size_t size, uint64_t heap,
                                       qa_guest_core_address *out);
bool qa_windows_guest_runtime_free(qa_windows_guest_runtime *runtime, bool has_address,
                                   qa_guest_core_address address, uint64_t heap);
bool qa_windows_guest_runtime_allocation_size(qa_windows_guest_runtime *runtime,
                                              qa_guest_core_address address, uint64_t heap, size_t *out);

#endif

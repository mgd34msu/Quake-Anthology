/* Port of src/compat/q3/native/module.ts. GPL-2.0-or-later.
 * GuestAddress has no addressSpace token in C, so the TS
 * address-space identity check in QuakeLiveGameModule cannot be
 * represented (registers.h precedent). TS TypeError/Error throws
 * abort via qa_range_error with the identical message. Signature
 * parameter layouts are caller-owned storage filled by
 * qa_compat_q3_native_signature. */
#ifndef QA_COMPAT_Q3_NATIVE_MODULE_H
#define QA_COMPAT_Q3_NATIVE_MODULE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/guest/abi/runner.h"

/* Builds a scalar signature; layouts/result_layout are caller storage
 * (layouts needs parameter_count entries) borrowed by the result. */
qa_guest_call_signature qa_compat_q3_native_signature(qa_guest_native_abi abi,
  const qa_abi_storage *parameters, size_t parameter_count,
  bool has_result, qa_abi_storage result, bool variadic,
  qa_abi_value_layout *layouts, qa_abi_value_layout *result_layout);
/* False = TS null. */
bool qa_compat_q3_native_export(const qa_guest_image *image, const char *name, qa_guest_core_address *out);

typedef struct qa_compat_q3_native_module_options {
  qa_guest_image image;
  qa_guest_call_runner *runner;
  qa_guest_core_call_context context;
  int64_t instruction_budget;
} qa_compat_q3_native_module_options;

typedef struct qa_compat_q3_quakelive_game_module {
  qa_compat_q3_native_module_options options;
  qa_mapped_guest_memory *memory;
  qa_guest_core_address exports;
  int64_t api_version;
} qa_compat_q3_quakelive_game_module;
void qa_compat_q3_quakelive_game_module_init(qa_compat_q3_quakelive_game_module *m,
  qa_compat_q3_native_module_options options, qa_guest_core_address imports);
void qa_compat_q3_quakelive_game_module_register_cvars(qa_compat_q3_quakelive_game_module *m);
void qa_compat_q3_quakelive_game_module_initialize(qa_compat_q3_quakelive_game_module *m, double level_time, double random_seed, bool restart);
void qa_compat_q3_quakelive_game_module_shutdown(qa_compat_q3_quakelive_game_module *m, bool restart);
void qa_compat_q3_quakelive_game_module_run_frame(qa_compat_q3_quakelive_game_module *m, double time);
bool qa_compat_q3_quakelive_game_module_console_command(qa_compat_q3_quakelive_game_module *m);
bool qa_compat_q3_quakelive_game_module_client_connect(qa_compat_q3_quakelive_game_module *m, double client, bool first_time, bool is_bot, qa_guest_core_address *out);

typedef struct qa_compat_q3_vm_module {
  qa_compat_q3_native_module_options options;
  qa_guest_core_address vm_main;
} qa_compat_q3_vm_module;
void qa_compat_q3_vm_module_init(qa_compat_q3_vm_module *m, qa_compat_q3_native_module_options options, qa_guest_core_address syscall);
double qa_compat_q3_vm_module_call(qa_compat_q3_vm_module *m, double command, const double *arguments, size_t argument_count);
#endif

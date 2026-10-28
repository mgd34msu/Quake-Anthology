/* Port of src/compat/q3/native/imports.ts. GPL-2.0-or-later.
 * NativeQ3ServiceUnavailable/NativeQ3GameError throws abort via
 * qa_range_error with the identical message. Engine/configstring
 * services are mirrored fn+ctx (TS owner
 * src/app/bootstrap/simulation/q3/types.ts, unported); cvars are
 * mirrored too (the landed cvar header cannot share a TU with
 * guest types). Per-slot callback, id and
 * signature layout storage is owned by the imports struct. */
#ifndef QA_COMPAT_Q3_NATIVE_IMPORTS_H
#define QA_COMPAT_Q3_NATIVE_IMPORTS_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>
#include "qa/compat/q3/native/module.h"
#include "qa/guest/core/callbacks.h"

typedef struct qa_compat_q3_native_service_coverage {
  int64_t slot; const char *name; bool implemented; int64_t reached;
} qa_compat_q3_native_service_coverage;

noreturn void qa_compat_q3_native_service_unavailable(int64_t slot, const char *service);
noreturn void qa_compat_q3_native_game_error(const char *message);

typedef struct qa_compat_q3_native_engine_services {
  void *ctx;
  void (*append_console_command)(void *ctx, const char *text);
  void (*print)(void *ctx, const char *text);
  void (*drop_client)(void *ctx, double client, const char *reason);
  void (*send_server_command)(void *ctx, double client, const char *text);
  const char *(*get_userinfo)(void *ctx, double client);
  void (*set_userinfo)(void *ctx, double client, const char *value);
  /* Mirrored CvarRegistry picks. cvar_variable_string and the
   * cvar_read_vm value are malloc'd (caller frees). */
  double (*cvar_variable_value)(void *ctx, const char *name);
  char *(*cvar_variable_string)(void *ctx, const char *name);
  void (*cvar_set)(void *ctx, const char *name, const char *value, bool force);
  int64_t (*cvar_bind_vm)(void *ctx, const char *name, const char *default_value, double flags);
  bool (*cvar_read_vm)(void *ctx, int64_t handle, double *out_modification_count,
    char **out_value, double *out_numeric_value, double *out_integer_value);
  void (*configstring_set)(void *ctx, double index, const char *value);
  const char *(*configstring_get)(void *ctx, double index);
  const char *const *(*command_arguments)(void *ctx, size_t *out_count);
} qa_compat_q3_native_engine_services;

typedef qa_call_result (*qa_compat_q3_native_service)(void *ctx, const qa_guest_call_context *context,
  const qa_call_value *arguments, size_t argument_count);

typedef struct qa_compat_q3_quakelive_game_imports qa_compat_q3_quakelive_game_imports;
struct qa_compat_q3_quakelive_game_imports {
  qa_sparse_guest_memory *memory;
  qa_guest_callback_table *callbacks;
  qa_guest_native_abi abi;
  qa_compat_q3_native_engine_services host;
  qa_guest_address address;
  struct qa_compat_q3_native_import_slot *slots; /* 206, owned */
  void **retired; size_t retired_count, retired_capacity; /* rebind graveyard (table borrows ids/layouts) */
};
bool qa_compat_q3_quakelive_game_imports_init(qa_compat_q3_quakelive_game_imports *imports,
  qa_sparse_guest_memory *memory, qa_guest_callback_table *callbacks,
  qa_guest_native_abi abi, qa_compat_q3_native_engine_services host);
void qa_compat_q3_quakelive_game_imports_destroy(qa_compat_q3_quakelive_game_imports *imports);
/* Returns the coverage count (206); fills out[] up to capacity. */
size_t qa_compat_q3_quakelive_game_imports_coverage(const qa_compat_q3_quakelive_game_imports *imports,
  qa_compat_q3_native_service_coverage *out, size_t capacity);
qa_guest_address qa_compat_q3_quakelive_game_imports_bind(qa_compat_q3_quakelive_game_imports *imports,
  int64_t slot, const char *name, const qa_abi_storage *parameters, size_t parameter_count,
  bool has_result, qa_abi_storage result, void *ctx, qa_compat_q3_native_service invoke, bool implemented);
void qa_compat_q3_quakelive_game_imports_write_string(qa_compat_q3_quakelive_game_imports *imports,
  qa_guest_address address, const char *value, int64_t capacity);
#endif

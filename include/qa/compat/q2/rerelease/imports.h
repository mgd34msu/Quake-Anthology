/* Port of src/compat/q2/rerelease/imports.ts. GPL-2.0-or-later.
 * readGuestString decodes UTF-8 losslessly for byte strings. */
#ifndef QA_COMPAT_Q2_RERELEASE_IMPORTS_H
#define QA_COMPAT_Q2_RERELEASE_IMPORTS_H
#include <stdbool.h>
#include "qa/compat/q2/rerelease/module.h"
#include "qa/core/cvars/index.h"

/* Malloc'd; caller frees. maximum < 0 selects 1048576. */
char *qa_compat_q2_read_guest_string(const qa_compat_q2_memory *memory, qa_guest_address address, double maximum);

typedef struct qa_compat_q2_rerelease_core_services {
  void *ctx;
  qa_cvar_registry *cvars;
  void (*print)(void *ctx, const char *text);
  const char *(*get_configstring)(void *ctx, double index);
  void (*set_configstring)(void *ctx, double index, const char *value);
  double (*resource_index)(void *ctx, const char *kind, const char *name);
  double (*server_frame)(void *ctx);
  const char *const *(*command_arguments)(void *ctx, size_t *out_count);
  const char *(*command_tail)(void *ctx);
  void (*add_command)(void *ctx, const char *text);
  bool (*extension)(void *ctx, const char *name, const char *api, qa_guest_address *out); /* false = TS null */
  /* Optional invoke: false means TS undefined (not handled). */
  bool (*invoke)(void *ctx, qa_compat_q2_rerelease_import_call call, qa_guest_call_result *out);
} qa_compat_q2_rerelease_core_services;

typedef struct qa_compat_q2_rerelease_core_imports qa_compat_q2_rerelease_core_imports;
/* NULL only on OOM. */
qa_compat_q2_rerelease_core_imports *qa_compat_q2_rerelease_core_imports_create(qa_compat_q2_memory *memory,
                                                                              qa_compat_q2_rerelease_core_services services);
void qa_compat_q2_rerelease_core_imports_destroy(qa_compat_q2_rerelease_core_imports *c);
qa_compat_q2_memory *qa_compat_q2_rerelease_core_imports_get_memory(const qa_compat_q2_rerelease_core_imports *c);
qa_guest_address qa_compat_q2_rerelease_core_imports_string(qa_compat_q2_rerelease_core_imports *c, const char *text);
void qa_compat_q2_rerelease_core_imports_refresh_cvars(qa_compat_q2_rerelease_core_imports *c);
/* snapshot NULL is the TS undefined (returns false for null result). */
bool qa_compat_q2_rerelease_core_imports_cvar(qa_compat_q2_rerelease_core_imports *c, const qa_cvar_snapshot *value, qa_guest_address *out);
void qa_compat_q2_rerelease_core_imports_free(qa_compat_q2_rerelease_core_imports *c, bool has, qa_guest_address address);
qa_guest_call_result qa_compat_q2_rerelease_core_imports_invoke(qa_compat_q2_rerelease_core_imports *c, qa_compat_q2_rerelease_import_call call);
/* TS withRereleaseWeaponProvisioning / weapon cvar key order. */
extern const char *const qa_compat_q2_rerelease_weapon_cvar_keys[];
extern const size_t qa_compat_q2_rerelease_weapon_cvar_key_count;
/* capture is a qa_save_value from qa_cvar_registry_capture_world_transfer_state. */
qa_original_weapon_provisioning qa_compat_q2_with_rerelease_weapon_provisioning(
    qa_cvar_registry *cvars, void *operation_ctx, qa_original_weapon_provisioning (*operation)(void *));
#endif

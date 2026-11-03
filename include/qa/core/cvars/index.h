/*
 * Port of src/core/cvars/index.ts (CvarRegistry and friends).
 * C accommodations:
 *  - The registry owns its states; CvarRead/CvarSnapshot become
 *    qa_cvar_read value copies with owned strings (caller frees with
 *    qa_cvar_read_free). JS object identity (canonical snapshot and
 *    alias-read caches, live getters in find()) is not preserved:
 *    find/get return point-in-time copies with equal fields.
 *  - Closures (print, onEffect, commandExists, cheatsAllowed, value
 *    bindings, alias conversions, archive include, writeVariables)
 *    become function pointer + ctx. cheatsAllowed returns -1 for TS
 *    undefined, else 0/1.
 *  - bindOutput/bindValue return small integer handles to unbind.
 *  - prepareCandidate/prepareTransfer return a transfer object with
 *    validate/publish function pointers; publish state is internal.
 *  - Flags are int32 (JS bitwise semantics); counts the TS holds as
 *    numbers stay double. Snapshots that TS returns as undefined are
 *    reported by a bool return with a zeroed output.
 *  - Plain TS Errors abort with the identical message; RangeErrors
 *    use qa_range_error.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_CVARS_INDEX_H
#define QA_CORE_CVARS_INDEX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "qa/contracts/common.h"
#include "qa/core/commands/documentation.h"
#include "qa/core/commands/text.h"
#include "qa/persistence/value.h"

/* TS CvarFlag (Q3 ABI flag words). */
enum {
  QA_CVAR_FLAG_NONE = 0, QA_CVAR_FLAG_ARCHIVE = 1, QA_CVAR_FLAG_USER_INFO = 2,
  QA_CVAR_FLAG_SERVER_INFO = 4, QA_CVAR_FLAG_SYSTEM_INFO = 8, QA_CVAR_FLAG_INIT = 16,
  QA_CVAR_FLAG_LATCH = 32, QA_CVAR_FLAG_READ_ONLY = 64, QA_CVAR_FLAG_USER_CREATED = 128,
  QA_CVAR_FLAG_TEMPORARY = 256, QA_CVAR_FLAG_CHEAT = 512, QA_CVAR_FLAG_NO_RESTART = 1024
};
/* TS Q2CvarFlag. */
enum {
  QA_Q2_CVAR_FLAG_NONE = 0, QA_Q2_CVAR_FLAG_ARCHIVE = 1, QA_Q2_CVAR_FLAG_USER_INFO = 2,
  QA_Q2_CVAR_FLAG_SERVER_INFO = 4, QA_Q2_CVAR_FLAG_NO_SET = 8, QA_Q2_CVAR_FLAG_LATCH = 16,
  QA_Q2_CVAR_FLAG_CHEAT = 32, QA_Q2_CVAR_FLAG_PRIVATE = 64, QA_Q2_CVAR_FLAG_READ_ONLY = 128,
  QA_Q2_CVAR_FLAG_MODIFIED = 256, QA_Q2_CVAR_FLAG_CUSTOM = 512, QA_Q2_CVAR_FLAG_WEAK = 1024,
  QA_Q2_CVAR_FLAG_GAME = 2048, QA_Q2_CVAR_FLAG_NO_ARCHIVE = 4096, QA_Q2_CVAR_FLAG_FILES = 8192,
  QA_Q2_CVAR_FLAG_REFRESH = 16384, QA_Q2_CVAR_FLAG_SOUND = 32768
};

/* TS CvarRead / CvarSnapshot. Strings are owned copies. */
typedef struct qa_cvar_read {
  char *name;
  char *value;
  char *reset_value;
  char *latched_value; /* NULL is TS undefined */
  int32_t flags;
  bool modified;
  double modification_count;
  double numeric_value;
  double integer_value;
} qa_cvar_read;
typedef qa_cvar_read qa_cvar_snapshot;
void qa_cvar_read_free(qa_cvar_read *read);

typedef struct qa_cvar_archive_entry {
  char *name;
  char *value;
} qa_cvar_archive_entry;

/** TS CvarValueBinding. validate returns a malloc'd explanation or
 * NULL to accept. */
typedef struct qa_cvar_value_binding {
  void *ctx;
  char *(*validate)(void *ctx, const char *value);
  void (*changed)(void *ctx, const char *value);
} qa_cvar_value_binding;

/* TS CvarAlias conversion: read returns a malloc'd projection;
 * write fills value (malloc'd) and returns true, or fills message
 * (malloc'd) and returns false. */
typedef struct qa_cvar_alias_conversion {
  void *ctx;
  bool is_identity;
  char *(*read)(void *ctx, const char *value);
  bool (*write)(void *ctx, const char *value, char **out_value, char **out_message);
} qa_cvar_alias_conversion;
typedef struct qa_cvar_alias {
  const char *name;
  const char *target;
  qa_command_documentation documentation;
  qa_cvar_alias_conversion conversion;
} qa_cvar_alias;

/* TS CvarEffect union on kind. Strings are owned copies; command is
 * NULL for the TS null. execute_autoexec is the literal true. */
typedef enum qa_cvar_effect_kind {
  QA_CVAR_EFFECT_USERINFO, QA_CVAR_EFFECT_SERVER_INFO,
  QA_CVAR_EFFECT_BROADCAST, QA_CVAR_EFFECT_GAME_DIRECTORY
} qa_cvar_effect_kind;
typedef struct qa_cvar_effect {
  qa_cvar_effect_kind kind;
  qa_command_context context;
  char *name;
  char *value;
  char *info;
  char *command;   /* userinfo, nullable */
  char *text;      /* broadcast */
  char *directory; /* game-directory */
} qa_cvar_effect;
void qa_cvar_effect_free(qa_cvar_effect *effect);

typedef struct qa_cvar_registry qa_cvar_registry;
typedef void (*qa_cvar_effect_fn)(void *ctx, const qa_cvar_effect *effect);
typedef bool (*qa_cvar_command_exists_fn)(void *ctx, const char *name);
/* Returns -1 for TS undefined, else 0/1. */
typedef int (*qa_cvar_cheats_allowed_fn)(void *ctx);

typedef struct qa_cvar_registry_options {
  qa_command_dialect dialect;
  qa_command_context context;
  qa_command_print_fn print; /* NULL when absent */
  void *print_ctx;
  qa_cvar_effect_fn on_effect; /* NULL when absent */
  void *on_effect_ctx;
  qa_cvar_command_exists_fn command_exists; /* NULL when absent */
  void *command_exists_ctx;
  const qa_cvar_info_target *info_targets; /* NULL uses the TS default */
  size_t info_target_count;
  qa_cvar_cheats_allowed_fn cheats_allowed; /* NULL when absent */
  void *cheats_allowed_ctx;
} qa_cvar_registry_options;

/* NULL only on OOM. */
qa_cvar_registry *qa_cvar_registry_create(const qa_cvar_registry_options *options);
void qa_cvar_registry_destroy(qa_cvar_registry *registry);
qa_command_dialect qa_cvar_registry_dialect(const qa_cvar_registry *registry);
qa_command_context qa_cvar_registry_context(const qa_cvar_registry *registry);

/* Owned checkpoint trees; NULL only on OOM (capture aborts on
 * undrained effects, as the TS throws). */
qa_save_value *qa_cvar_registry_capture_save_state(qa_cvar_registry *registry);
qa_save_value *qa_cvar_registry_capture_world_transfer_state(qa_cvar_registry *registry);
qa_save_value *qa_cvar_registry_capture_quakec_state(qa_cvar_registry *registry);
void qa_cvar_registry_restore_save_state(qa_cvar_registry *registry, const qa_save_value *value);
void qa_cvar_registry_restore_quakec_state(qa_cvar_registry *registry, const qa_save_value *value);

typedef struct qa_cvar_transfer qa_cvar_transfer;
struct qa_cvar_transfer {
  void *ctx;
  qa_cvar_registry *cvars; /* prepareCandidate's fresh registry */
  void (*validate_publication)(void *ctx);
  void (*publish)(void *ctx);
  void (*destroy)(void *ctx);
};
/* has_print false selects the owner's configured print (TS default parameter). NULL only on OOM.
 * destroy(ctx) releases the transfer state; the caller then frees the qa_cvar_transfer itself. */
qa_cvar_transfer *qa_cvar_registry_prepare_candidate(qa_cvar_registry *registry,
                                                     qa_command_print_fn print,
                                                     void *print_ctx, bool has_print);
qa_cvar_transfer *qa_cvar_registry_prepare_transfer(qa_cvar_registry *registry,
                                                    qa_cvar_registry *candidate);

double qa_cvar_registry_modified_flags(const qa_cvar_registry *registry);
bool qa_cvar_registry_userinfo_modified(const qa_cvar_registry *registry);
double qa_cvar_registry_index_count(const qa_cvar_registry *registry);
int64_t qa_cvar_registry_bind_output(qa_cvar_registry *registry, qa_command_print_fn print, void *ctx);
void qa_cvar_registry_unbind_output(qa_cvar_registry *registry, int64_t binding);

void qa_cvar_registry_register_alias(qa_cvar_registry *registry, qa_cvar_alias alias);
/* Malloc'd canonical name (the input name when not an alias). */
char *qa_cvar_registry_canonical_name(qa_cvar_registry *registry, const char *name);
bool qa_cvar_registry_find(qa_cvar_registry *registry, const char *name, qa_cvar_read *out);
bool qa_cvar_registry_get(qa_cvar_registry *registry, const char *name, qa_cvar_snapshot *out);
bool qa_cvar_registry_is_console_created(qa_cvar_registry *registry, const char *name);
/* Returns an unbind handle. */
int64_t qa_cvar_registry_bind_value(qa_cvar_registry *registry, const char *name,
                                    qa_cvar_value_binding binding);
void qa_cvar_registry_unbind_value(qa_cvar_registry *registry, int64_t binding);
void qa_cvar_registry_document(qa_cvar_registry *registry, const char *name,
                               qa_command_documentation documentation);
/* Borrowed documentation or NULL (TS undefined). */
const qa_command_documentation *qa_cvar_registry_documentation(qa_cvar_registry *registry,
                                                               const char *name);
/* Malloc'd string ("" when missing); NULL only on OOM. */
char *qa_cvar_registry_variable_string(qa_cvar_registry *registry, const char *name);
double qa_cvar_registry_variable_value(qa_cvar_registry *registry, const char *name);
void qa_cvar_registry_set_server_active(qa_cvar_registry *registry, bool active);
void qa_cvar_registry_set_client_connected(qa_cvar_registry *registry, bool connected);
void qa_cvar_registry_set_server_high_characters(qa_cvar_registry *registry, bool enabled);

/* TS CvarRegistry.localInfo, QuakeWorld's server-side `localinfo` store: an
 * insertion-ordered Map<string, string> the registry owns. get returns a
 * borrowed value or NULL (TS undefined); entry walks it in Map order. */
const char *qa_cvar_registry_local_info_get(const qa_cvar_registry *registry, const char *key);
void qa_cvar_registry_local_info_set(qa_cvar_registry *registry, const char *key, const char *value);
bool qa_cvar_registry_local_info_delete(qa_cvar_registry *registry, const char *key);
size_t qa_cvar_registry_local_info_count(const qa_cvar_registry *registry);
bool qa_cvar_registry_local_info_entry(const qa_cvar_registry *registry, size_t index,
                                       const char **key, const char **value);

bool qa_cvar_registry_register(qa_cvar_registry *registry, const char *name,
                               const char *default_value, double flags, qa_cvar_snapshot *out);
bool qa_cvar_registry_set(qa_cvar_registry *registry, const char *name, const char *value,
                          bool force, qa_cvar_snapshot *out);
bool qa_cvar_registry_set_console(qa_cvar_registry *registry, const char *name,
                                  const char *value, qa_cvar_snapshot *out);
/* TS kind: "archive" | "userinfo" | "serverinfo". */
typedef enum qa_cvar_command_flag_kind {
  QA_CVAR_COMMAND_FLAG_ARCHIVE, QA_CVAR_COMMAND_FLAG_USERINFO, QA_CVAR_COMMAND_FLAG_SERVERINFO
} qa_cvar_command_flag_kind;
void qa_cvar_registry_set_command_flags(qa_cvar_registry *registry, const char *name,
                                        const char *value, qa_cvar_command_flag_kind kind);
void qa_cvar_registry_reset_console(qa_cvar_registry *registry, const char *name, bool all);
bool qa_cvar_registry_full_set(qa_cvar_registry *registry, const char *name,
                               const char *value, double flags, qa_cvar_snapshot *out);
bool qa_cvar_registry_set_value(qa_cvar_registry *registry, const char *name, double value,
                                qa_cvar_snapshot *out);
bool qa_cvar_registry_stage(qa_cvar_registry *registry, const char *name, const char *input,
                            qa_cvar_snapshot *out);
/* Malloc'd snapshot array; *out_count set. name NULL applies all. */
qa_cvar_snapshot *qa_cvar_registry_apply_latched(qa_cvar_registry *registry,
                                                 const char *name, size_t *out_count);
bool qa_cvar_registry_reset(qa_cvar_registry *registry, const char *name, bool force,
                            qa_cvar_snapshot *out);
void qa_cvar_registry_reset_all(qa_cvar_registry *registry);
void qa_cvar_registry_set_cheats_enabled(qa_cvar_registry *registry, bool enabled);
qa_cvar_snapshot *qa_cvar_registry_canonical_snapshots(qa_cvar_registry *registry,
                                                       double flags, size_t *out_count);
qa_cvar_snapshot *qa_cvar_registry_snapshots(qa_cvar_registry *registry, double flags,
                                             size_t *out_count);
/* Malloc'd completed name, NULL for TS undefined or OOM. */
char *qa_cvar_registry_complete(qa_cvar_registry *registry, const char *partial);
/* maximum_length < 0 selects the TS dialect default. */
char *qa_cvar_registry_info_string(qa_cvar_registry *registry, double flags,
                                   int64_t maximum_length);
/* Malloc'd copy of the propagated info string. */
char *qa_cvar_registry_propagated_info(qa_cvar_registry *registry, qa_cvar_info_target target);
typedef bool (*qa_cvar_include_fn)(void *ctx, const char *name);
qa_cvar_archive_entry *qa_cvar_registry_archive_entries(qa_cvar_registry *registry,
                                                        qa_cvar_include_fn include,
                                                        void *include_ctx, size_t *out_count);
void qa_cvar_registry_apply_archive(qa_cvar_registry *registry,
                                    const qa_cvar_archive_entry *entries, size_t count);
/* Malloc'd string array (each string malloc'd). */
char **qa_cvar_registry_archive_commands(qa_cvar_registry *registry,
                                         qa_cvar_include_fn include, void *include_ctx,
                                         size_t *out_count);
typedef void (*qa_cvar_write_fn)(void *ctx, const char *text);
void qa_cvar_registry_write_variables(qa_cvar_registry *registry, qa_cvar_write_fn write, void *ctx);
double qa_cvar_registry_take_modified_flags(qa_cvar_registry *registry);
void qa_cvar_registry_mark_modified_flags(qa_cvar_registry *registry, double flags);
void qa_cvar_registry_clear_modified_flags(qa_cvar_registry *registry, double flags);
void qa_cvar_registry_clear_userinfo_modified(qa_cvar_registry *registry);
void qa_cvar_registry_add_flags(qa_cvar_registry *registry, const char *name, double flags);
void qa_cvar_registry_clear_modified(qa_cvar_registry *registry, const char *name);
/* Drained effects (malloc'd array; free with qa_cvar_effect_free). */
qa_cvar_effect *qa_cvar_registry_take_effects(qa_cvar_registry *registry, size_t *out_count);

/* TS VmCvar / RegistryVmCvar. */
typedef struct qa_vm_cvar {
  qa_cvar_registry *registry; /* borrowed */
  bool has_handle;
  int64_t handle;
  char *text; /* owned */
  double numeric, integer, count;
} qa_vm_cvar;
void qa_vm_cvar_init(qa_vm_cvar *vm, qa_cvar_registry *registry);
void qa_vm_cvar_destroy(qa_vm_cvar *vm);
void qa_vm_cvar_register(qa_vm_cvar *vm, const char *name, const char *default_value,
                         double flags);
void qa_vm_cvar_update(qa_vm_cvar *vm);
void qa_vm_cvar_write_integer(qa_vm_cvar *vm, double value);
bool qa_cvar_registry_create_vm(qa_cvar_registry *registry, qa_vm_cvar *out);
bool qa_cvar_registry_register_vm(qa_cvar_registry *registry, const char *name,
                                  const char *default_value, double flags, qa_vm_cvar *out);
int64_t qa_cvar_registry_bind_vm(qa_cvar_registry *registry, const char *name,
                                 const char *default_value, double flags);
bool qa_cvar_registry_read_vm(qa_cvar_registry *registry, double handle, qa_cvar_snapshot *out);

#endif

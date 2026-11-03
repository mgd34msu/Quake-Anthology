/* Port of src/compat/qc/machine.ts. GPL-2.0-or-later.
 * fail() raises "QcRuntimeError" through qa_raise, message
 * "QuakeC <fn>:<stmt>: <msg>": aborts with no qa_try active, otherwise
 * unwinds like the TS throw (frames, depths and boundary scopes restored
 * on the way). Cancellation travels the same path under its own name.
 * Builtin registry entries are caller-owned arrays (borrowed). Snapshot
 * byte arrays are malloc'd, freed by qa_compat_qc_machine_snapshot_destroy. */
#ifndef QA_COMPAT_QC_MACHINE_H
#define QA_COMPAT_QC_MACHINE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>
#include "qa/compat/qc/memory.h"
#include "qa/compat/qc/program.h"
#include "qa/contracts/numeric.h"

typedef struct qa_compat_qc_machine qa_compat_qc_machine;
/* TS QcBuiltin closure: fn + ctx. fn NULL means unbound. */
typedef struct qa_compat_qc_builtin { void *ctx; void (*fn)(void *ctx, qa_compat_qc_machine *machine); } qa_compat_qc_builtin;
typedef struct qa_compat_qc_numbered_builtin { int64_t number; qa_compat_qc_builtin fn; } qa_compat_qc_numbered_builtin;
typedef struct qa_compat_qc_named_builtin { const char *name; qa_compat_qc_builtin fn; } qa_compat_qc_named_builtin;
typedef struct qa_compat_qc_builtin_registry {
  qa_compat_qc_numbered_builtin *numbered; size_t numbered_count, numbered_capacity;
  qa_compat_qc_named_builtin *named; size_t named_count, named_capacity;
  /* Closure state for builtins installed by createQcBuiltins (each
   * builtin's ctx). Owned via state_destroy when set. */
  void *state; void (*state_destroy)(void *state);
} qa_compat_qc_builtin_registry;
/* Registry helpers (TS Map set/get/has). Names borrowed. */
bool qa_compat_qc_registry_set_numbered(qa_compat_qc_builtin_registry *r, int64_t number, qa_compat_qc_builtin fn);
bool qa_compat_qc_registry_set_named(qa_compat_qc_builtin_registry *r, const char *name, qa_compat_qc_builtin fn);
qa_compat_qc_builtin qa_compat_qc_registry_get_numbered(const qa_compat_qc_builtin_registry *r, int64_t number);
qa_compat_qc_builtin qa_compat_qc_registry_get_named(const qa_compat_qc_builtin_registry *r, const char *name);
void qa_compat_qc_registry_destroy(qa_compat_qc_builtin_registry *r);

typedef struct qa_compat_qc_entity_store_observation {
  int64_t function_index, statement, reference, word;
  const uint8_t *before; size_t before_length; const uint8_t *after; size_t after_length;
} qa_compat_qc_entity_store_observation;
typedef struct qa_compat_qc_call_site { int64_t function_index, caller, statement; } qa_compat_qc_call_site;

/* TS QcFunctionExecution (callable + skip + cancel). */
/* A retained handle stays safe to call after its boundary returned: it fails by name. */
typedef struct qa_compat_qc_function_execution {
  void *ctx;
  void (*run)(void *ctx, const qa_compat_qc_builtin *prepare); /* prepare may be NULL */
  void (*skip)(void *ctx, const int64_t return_words[3]);
  void (*cancel)(void *ctx, const int64_t return_words[3]);
} qa_compat_qc_function_execution;
typedef struct qa_compat_qc_inline_region {
  int64_t function_index, entry, exit; bool replaceable;
  bool has_standalone; int64_t standalone_saved; const char *standalone_scope; /* NULL|"frame"|"global" */
} qa_compat_qc_inline_region;
typedef struct qa_compat_qc_inline_continuation {
  void *ctx; void (*run)(void *ctx); void (*skip_to_join)(void *ctx);
} qa_compat_qc_inline_continuation;
typedef struct qa_compat_qc_inline_boundary {
  void *ctx; const qa_compat_qc_inline_region *regions; size_t region_count;
  void (*run)(void *ctx, qa_compat_qc_inline_region region, qa_compat_qc_inline_continuation execute);
} qa_compat_qc_inline_boundary;
typedef struct qa_compat_qc_function_boundary {
  void *ctx; const int64_t *functions; size_t function_count;
  void (*run)(void *ctx, qa_compat_qc_call_site call, qa_compat_qc_function_execution execute);
} qa_compat_qc_function_boundary;

typedef struct qa_compat_qc_machine_options {
  qa_compat_qc_program *program;              /* borrowed */
  const qa_numeric_operations *numeric;       /* borrowed */
  qa_compat_qc_entity_memory *entities;       /* borrowed */
  qa_compat_qc_builtin_registry *builtins;    /* borrowed */
  void *ctx;
  bool (*server_active)(void *ctx);
  int64_t statement_limit, stack_limit, local_stack_words; /* 0 = TS default */
  void (*trace)(void *ctx, qa_compat_qc_machine *machine);
  void (*observe_call)(void *ctx, qa_compat_qc_call_site call);
  const qa_compat_qc_function_boundary *function_boundary;
  const qa_compat_qc_inline_boundary *inline_boundary;
  void (*observe_entity_store)(void *ctx, qa_compat_qc_entity_store_observation store);
  /* kind "read"|"write"; words 1|3 */
  void (*validate_entity_access)(void *ctx, int64_t reference, int64_t word, int64_t words, const char *kind);
} qa_compat_qc_machine_options;

typedef struct qa_compat_qc_machine_snapshot {
  uint8_t *globals, *entities, *strings; size_t globals_length, entities_length, strings_length;
  int64_t entity_count, statement, function_index, argument_count;
  double *profiling; size_t profiling_count; bool trace_enabled;
} qa_compat_qc_machine_snapshot;
void qa_compat_qc_machine_snapshot_destroy(qa_compat_qc_machine_snapshot *s);

typedef struct qa_compat_qc_frame { int64_t statement, function_index; uint8_t *locals; size_t locals_length; } qa_compat_qc_frame;
struct qa_compat_qc_machine {
  qa_compat_qc_program *program; qa_compat_qc_words globals; uint8_t *globals_bytes;
  qa_compat_qc_strings strings; qa_compat_qc_entity_memory *entities;
  const qa_numeric_operations *numeric; double *profiling;
  qa_compat_qc_machine_options options;
  qa_compat_qc_frame *frames; size_t frame_count, frame_capacity;
  int64_t statement_limit, stack_limit, local_stack_limit, local_words;
  int64_t function_index, statement, argument_count, builtin_depth, boundary_depth;
  int64_t *boundary_functions; size_t boundary_function_count;
  qa_compat_qc_inline_region *inline_regions; size_t inline_region_count;
  bool trace_enabled;
  /* Live and recently expired continuations (private to machine.c). */
  struct qa_compat_qc_continuation **continuations; size_t continuation_count, continuation_next;
};

bool qa_compat_qc_machine_init(qa_compat_qc_machine *m, qa_compat_qc_machine_options options);
void qa_compat_qc_machine_destroy(qa_compat_qc_machine *m);
int64_t qa_compat_qc_machine_get_argc(const qa_compat_qc_machine *m);
int64_t qa_compat_qc_machine_get_current_statement(const qa_compat_qc_machine *m);
int64_t qa_compat_qc_machine_get_current_function(const qa_compat_qc_machine *m);
int64_t qa_compat_qc_machine_get_depth(const qa_compat_qc_machine *m);
noreturn void qa_compat_qc_machine_fail(qa_compat_qc_machine *m, const char *format, ...);
int64_t qa_compat_qc_machine_global_offset(qa_compat_qc_machine *m, const char *name);
bool qa_compat_qc_machine_global_offset_or_null(const qa_compat_qc_machine *m, const char *name, int64_t *out);
int64_t qa_compat_qc_machine_field_offset(qa_compat_qc_machine *m, const char *name);
double qa_compat_qc_machine_entity_float(qa_compat_qc_machine *m, int64_t reference, const char *field);
int32_t qa_compat_qc_machine_entity_int(qa_compat_qc_machine *m, int64_t reference, const char *field);
qa_vec3 qa_compat_qc_machine_entity_vector(qa_compat_qc_machine *m, int64_t reference, const char *field);
void qa_compat_qc_machine_set_entity_float(qa_compat_qc_machine *m, int64_t reference, const char *field, double value);
void qa_compat_qc_machine_set_entity_int(qa_compat_qc_machine *m, int64_t reference, const char *field, double value);
void qa_compat_qc_machine_set_entity_vector(qa_compat_qc_machine *m, int64_t reference, const char *field, qa_vec3 value);
double qa_compat_qc_machine_arg_float(qa_compat_qc_machine *m, int64_t index);
int32_t qa_compat_qc_machine_arg_int(qa_compat_qc_machine *m, int64_t index);
qa_vec3 qa_compat_qc_machine_arg_vector(qa_compat_qc_machine *m, int64_t index);
const char *qa_compat_qc_machine_arg_string(qa_compat_qc_machine *m, int64_t index);
void qa_compat_qc_machine_return_float(qa_compat_qc_machine *m, double value);
void qa_compat_qc_machine_return_int(qa_compat_qc_machine *m, double value);
void qa_compat_qc_machine_return_vector(qa_compat_qc_machine *m, qa_vec3 value);
/* Malloc'd concatenation (caller frees); NULL on OOM. */
char *qa_compat_qc_machine_var_string(qa_compat_qc_machine *m, int64_t first);
qa_compat_qc_words *qa_compat_qc_machine_self(qa_compat_qc_machine *m);
typedef struct qa_compat_qc_missing_builtin { int64_t function_index; const char *name; bool has_number; int64_t number; } qa_compat_qc_missing_builtin;
/* Returns count; fills out[] up to capacity (names borrowed). */
size_t qa_compat_qc_machine_missing_builtins(qa_compat_qc_machine *m, qa_compat_qc_missing_builtin *out, size_t capacity);
void qa_compat_qc_machine_execute(qa_compat_qc_machine *m, int64_t function_index, int64_t argument_count);
double qa_compat_qc_machine_execute_region(qa_compat_qc_machine *m, qa_compat_qc_inline_region region, int64_t argument_count);
bool qa_compat_qc_machine_get_snapshot(qa_compat_qc_machine *m, qa_compat_qc_machine_snapshot *out);
/* TS validateSnapshot: the checks restore runs before it changes anything. */
void qa_compat_qc_machine_validate_snapshot(qa_compat_qc_machine *m, const qa_compat_qc_machine_snapshot *s);
bool qa_compat_qc_machine_restore(qa_compat_qc_machine *m, const qa_compat_qc_machine_snapshot *snapshot);
#endif

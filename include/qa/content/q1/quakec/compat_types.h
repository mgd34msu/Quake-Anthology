/*
 * Mirrors of src/compat/qc types whose C owner has not landed, for
 * the src/content/q1/quakec port. Collision-proof qa_q1qc_ names;
 * TS owners:
 *  - program/definition/statement/function/opcode/program_error:
 *    src/compat/qc/program.ts (QcProgram is data-only here; the
 *    progs.dat loader belongs to the compat owner)
 *  - words/entity_memory/strings access: src/compat/qc/memory.ts
 *  - machine/call_site/function_execution/inline regions/boundaries,
 *    entity_store_observation: src/compat/qc/machine.ts (machine is
 *    an fn+ctx surface; the interpreter belongs to the compat owner)
 *  - source-call validation/projection: src/compat/qc/source-call.ts
 *  - source (program/entities/actors/slots pick):
 *    src/compat/qc/world-host.ts QcWorldHostOptions; provider and
 *    slot_free mirror slots.options.provider and
 *    slots.options.storage.read(slot).free, which the landed slots
 *    header does not expose.
 * QcProgramError aborts through qa_range_error with the TS
 * "source: message" text. QcFunctionExecution.cancel is noreturn
 * (TS throws QcFunctionCancellation). GPL-2.0-or-later.
 */
#ifndef QA_CONTENT_Q1_QUAKEC_COMPAT_TYPES_H
#define QA_CONTENT_Q1_QUAKEC_COMPAT_TYPES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdnoreturn.h>

#include "qa/contracts/gameplay.h"
#include "qa/contracts/identity.h"
#include "qa/contracts/math.h"
#include "qa/contracts/mod_callbacks.h"
#include "qa/contracts/numeric.h"
#include "qa/world/actors/registry.h"
#include "qa/world/actors/source_slots.h"

typedef enum qa_q1qc_opcode {
  QA_Q1QC_OP_DONE, QA_Q1QC_OP_MUL_F, QA_Q1QC_OP_MUL_V, QA_Q1QC_OP_MUL_FV,
  QA_Q1QC_OP_MUL_VF, QA_Q1QC_OP_DIV_F, QA_Q1QC_OP_ADD_F, QA_Q1QC_OP_ADD_V,
  QA_Q1QC_OP_SUB_F, QA_Q1QC_OP_SUB_V, QA_Q1QC_OP_EQ_F, QA_Q1QC_OP_EQ_V,
  QA_Q1QC_OP_EQ_S, QA_Q1QC_OP_EQ_E, QA_Q1QC_OP_EQ_FN, QA_Q1QC_OP_NE_F,
  QA_Q1QC_OP_NE_V, QA_Q1QC_OP_NE_S, QA_Q1QC_OP_NE_E, QA_Q1QC_OP_NE_FN,
  QA_Q1QC_OP_LE, QA_Q1QC_OP_GE, QA_Q1QC_OP_LT, QA_Q1QC_OP_GT,
  QA_Q1QC_OP_LOAD_F, QA_Q1QC_OP_LOAD_V, QA_Q1QC_OP_LOAD_S, QA_Q1QC_OP_LOAD_ENT,
  QA_Q1QC_OP_LOAD_FLD, QA_Q1QC_OP_LOAD_FN, QA_Q1QC_OP_ADDRESS,
  QA_Q1QC_OP_STORE_F, QA_Q1QC_OP_STORE_V, QA_Q1QC_OP_STORE_S, QA_Q1QC_OP_STORE_ENT,
  QA_Q1QC_OP_STORE_FLD, QA_Q1QC_OP_STORE_FN,
  QA_Q1QC_OP_STORE_PF, QA_Q1QC_OP_STORE_PV, QA_Q1QC_OP_STORE_PS,
  QA_Q1QC_OP_STORE_PENT, QA_Q1QC_OP_STORE_PFLD, QA_Q1QC_OP_STORE_PFN,
  QA_Q1QC_OP_RETURN, QA_Q1QC_OP_NOT_F, QA_Q1QC_OP_NOT_V, QA_Q1QC_OP_NOT_S,
  QA_Q1QC_OP_NOT_ENT, QA_Q1QC_OP_NOT_FN, QA_Q1QC_OP_IF, QA_Q1QC_OP_IF_NOT,
  QA_Q1QC_OP_CALL0, QA_Q1QC_OP_CALL1, QA_Q1QC_OP_CALL2, QA_Q1QC_OP_CALL3,
  QA_Q1QC_OP_CALL4, QA_Q1QC_OP_CALL5, QA_Q1QC_OP_CALL6, QA_Q1QC_OP_CALL7,
  QA_Q1QC_OP_CALL8, QA_Q1QC_OP_STATE, QA_Q1QC_OP_GOTO, QA_Q1QC_OP_AND,
  QA_Q1QC_OP_OR, QA_Q1QC_OP_BIT_AND, QA_Q1QC_OP_BIT_OR
} qa_q1qc_opcode;

typedef struct qa_q1qc_definition {
  const char *type; /* QcValueType string */
  int64_t native_type; bool save; int64_t offset; const char *name;
} qa_q1qc_definition;
typedef struct qa_q1qc_statement {
  qa_q1qc_opcode opcode; int64_t a; int64_t b; int64_t c;
} qa_q1qc_statement;
typedef struct qa_q1qc_function {
  int64_t index; int64_t first_statement; int64_t parameter_start;
  int64_t local_words; const char *name; const char *file;
  const int64_t *parameter_sizes; size_t parameter_size_count;
  bool named_builtin;
} qa_q1qc_function;
typedef struct qa_q1qc_program {
  const char *source;
  const char *api_kind; /* "q1-netquake" | "q1-quakeworld" */
  const qa_q1qc_statement *statements; size_t statement_count;
  const qa_q1qc_definition *globals; size_t global_count;
  const qa_q1qc_definition *fields; size_t field_count;
  const qa_q1qc_function *functions; size_t function_count;
  const uint8_t *strings; size_t string_length;
  const uint8_t *initial_globals; size_t initial_globals_length;
  int64_t entity_field_words; int64_t checksum;
  const char *digest;
} qa_q1qc_program;

noreturn void qa_q1qc_program_error(const qa_q1qc_program *program, const char *format, ...);
noreturn void qa_q1qc_error(const char *source, const char *format, ...);
int64_t qa_q1qc_signed_branch(int64_t word);
const qa_q1qc_definition *qa_q1qc_global_by_name(const qa_q1qc_program *program, const char *name);
const qa_q1qc_definition *qa_q1qc_field_by_name(const qa_q1qc_program *program, const char *name);
const qa_q1qc_function *qa_q1qc_function_at(const qa_q1qc_program *program, int64_t index);
const qa_q1qc_function *qa_q1qc_function_at_or_null(const qa_q1qc_program *program, int64_t index);
const qa_q1qc_function *qa_q1qc_function_named(const qa_q1qc_program *program, const char *name);
const qa_q1qc_function *qa_q1qc_function_named_or_null(const qa_q1qc_program *program, const char *name);
int64_t qa_q1qc_function_end(const qa_q1qc_program *program, const qa_q1qc_function *fn);
int32_t qa_q1qc_initial_int32(const qa_q1qc_program *program, int64_t word);
float qa_q1qc_initial_float32(const qa_q1qc_program *program, int64_t word);

/* TS QcWords: a view over word storage (bytes borrowed). */
typedef struct qa_q1qc_words { uint8_t *bytes; size_t byte_length; } qa_q1qc_words;
size_t qa_q1qc_words_length(const qa_q1qc_words *words);
int32_t qa_q1qc_words_int(const qa_q1qc_words *words, int64_t word);
double qa_q1qc_words_float(const qa_q1qc_words *words, int64_t word);
void qa_q1qc_words_set_int(qa_q1qc_words *words, int64_t word, int64_t value);
void qa_q1qc_words_set_float(qa_q1qc_words *words, int64_t word, double value);
qa_vec3 qa_q1qc_words_vector(const qa_q1qc_words *words, int64_t word);
void qa_q1qc_words_set_vector(qa_q1qc_words *words, int64_t word, qa_vec3 value);

/* TS QcEntityMemory (bytes borrowed from the compat owner). */
typedef struct qa_q1qc_entity_memory {
  int64_t stride_bytes; int64_t variables_offset_bytes; int64_t field_words;
  int64_t capacity; int64_t count;
  uint8_t *bytes;
} qa_q1qc_entity_memory;
int64_t qa_q1qc_entities_reference(const qa_q1qc_entity_memory *memory, int64_t slot);
int64_t qa_q1qc_entities_slot(const qa_q1qc_entity_memory *memory, int64_t reference);
void qa_q1qc_entities_at(const qa_q1qc_entity_memory *memory, int64_t slot, qa_q1qc_words *out);
void qa_q1qc_entities_from_reference(const qa_q1qc_entity_memory *memory, int64_t reference, qa_q1qc_words *out);

typedef struct qa_q1qc_machine qa_q1qc_machine;
/* TS `machine: () => QcMachine` closure: fn + ctx. */
typedef qa_q1qc_machine *(*qa_q1qc_machine_fn)(void *ctx);
typedef struct qa_q1qc_call_site {
  int64_t function_index; int64_t caller; int64_t statement;
} qa_q1qc_call_site;
typedef struct qa_q1qc_inline_region {
  int64_t function_index; int64_t entry; int64_t exit;
  bool replaceable; /* TS replaceable?: true (false = absent) */
  bool has_standalone; int64_t standalone_saved;
  const char *standalone_scope; /* NULL = absent ("frame" default) */
} qa_q1qc_inline_region;
typedef void (*qa_q1qc_prepare_fn)(void *ctx, qa_q1qc_machine *machine);
typedef struct qa_q1qc_function_execution {
  void *ctx;
  void (*run)(void *ctx, qa_q1qc_prepare_fn prepare, void *prepare_ctx);
  void (*skip)(void *ctx, const int32_t return_words[3]);
  void (*cancel)(void *ctx, const int32_t return_words[3]); /* must not return */
} qa_q1qc_function_execution;
void qa_q1qc_execution_run(const qa_q1qc_function_execution *execution);
typedef struct qa_q1qc_inline_continuation {
  void *ctx;
  void (*run)(void *ctx);
  void (*skip_to_join)(void *ctx);
} qa_q1qc_inline_continuation;
typedef struct qa_q1qc_function_boundary {
  void *ctx;
  const int64_t *functions; size_t function_count;
  void (*run)(void *ctx, qa_q1qc_call_site call, qa_q1qc_function_execution execute);
} qa_q1qc_function_boundary;
bool qa_q1qc_boundary_has(const qa_q1qc_function_boundary *boundary, int64_t function_index);
typedef struct qa_q1qc_inline_boundary {
  void *ctx;
  const qa_q1qc_inline_region *regions; size_t region_count;
  void (*run)(void *ctx, qa_q1qc_inline_region region, qa_q1qc_inline_continuation execute);
} qa_q1qc_inline_boundary;
typedef struct qa_q1qc_entity_store_observation {
  int64_t function_index; int64_t statement; int64_t reference; int64_t word;
  const uint8_t *before; size_t before_length;
  const uint8_t *after; size_t after_length;
} qa_q1qc_entity_store_observation;

/* TS QcMachine surface used by this slice. */
struct qa_q1qc_machine {
  void *ctx;
  qa_q1qc_program *program;
  qa_q1qc_words globals;
  qa_q1qc_entity_memory *entities;
  qa_numeric_operations numeric;
  const uint8_t *strings; size_t strings_length; bool strings_quakeworld;
  /* Optional owner string table; NULL uses the arena bytes above. */
  const char *(*strings_get)(void *ctx, int64_t reference, char *buf, size_t size);
  int64_t (*strings_set_engine)(void *ctx, const char *name, const char *text, size_t capacity);
  void (*fail)(void *ctx, const char *message); /* must not return */
  void (*execute)(void *ctx, int64_t function_index, int64_t argument_count);
  double (*execute_region)(void *ctx, qa_q1qc_inline_region region, int64_t argument_count);
};
noreturn void qa_q1qc_machine_fail(qa_q1qc_machine *machine, const char *format, ...);
const char *qa_q1qc_machine_string(qa_q1qc_machine *machine, int64_t reference, char *buf, size_t size);
int64_t qa_q1qc_machine_global_offset(qa_q1qc_machine *machine, const char *name);
int64_t qa_q1qc_machine_field_offset(qa_q1qc_machine *machine, const char *name);
double qa_q1qc_machine_arg_float(qa_q1qc_machine *machine, int64_t index);
int64_t qa_q1qc_machine_arg_int(qa_q1qc_machine *machine, int64_t index);

/* TS Pick<QcWorldHostOptions, "program"|"entities"|"actors"|"slots">
 * plus the slots.options surface noted in the header. */
typedef struct qa_q1qc_source {
  qa_q1qc_program *program;
  qa_q1qc_entity_memory *entities;
  qa_session_actor_registry *actors;
  qa_source_actor_slots *slots;
  qa_provider_id provider;
  void *slot_free_ctx;
  bool (*slot_free)(void *ctx, int64_t slot);
} qa_q1qc_source;
/* TS Partial<Pick<QcWorldHostOptions, "foreignActor" | "foreignReference">>,
 * the optional half of the id1 attack/environment/projectile sources
 * (foreign actors borrowing QC rows). Passed beside qa_q1qc_source; a NULL
 * table or NULL member is the TS absent member. Signatures match
 * qa_compat_qc_world_host_options. */
typedef struct qa_q1qc_foreign_source {
  void *ctx;
  bool (*foreign_actor)(void *ctx, int64_t slot, qa_owned_actor *out); /* false = TS null */
  int64_t (*foreign_reference)(void *ctx, qa_actor_id actor);
} qa_q1qc_foreign_source;

/* src/compat/qc/source-call.ts mirrors. */
const char *qa_q1qc_source_value_type(const qa_mod_callback_value *value);
bool qa_q1qc_validate_source_call_check(const qa_q1qc_program *program,
                                        const qa_mod_source_call *call,
                                        const char *const *available, size_t available_count,
                                        const char *label, char *error, size_t error_size);
void qa_q1qc_validate_source_call(const qa_q1qc_program *program,
                                  const qa_mod_source_call *call,
                                  const char *const *available, size_t available_count,
                                  const char *label);
typedef int64_t (*qa_q1qc_actor_reference_fn)(void *ctx, const qa_actor_id *actor);
typedef struct qa_q1qc_source_input {
  qa_mod_callback_input name; qa_mod_runtime_value value;
} qa_q1qc_source_input;
double qa_q1qc_with_source_call(qa_q1qc_machine *machine, const qa_mod_source_call *call,
                                const qa_q1qc_source_input *inputs, size_t input_count,
                                qa_q1qc_actor_reference_fn reference, void *reference_ctx,
                                double (*execute)(void *ctx, int64_t argument_count), void *execute_ctx);
bool qa_q1qc_source_call_equal(const qa_mod_source_call *a, const qa_mod_source_call *b);
bool qa_q1qc_actor_equal(qa_actor_id a, qa_actor_id b);
bool qa_q1qc_owned_equal(qa_owned_actor a, qa_owned_actor b);

#endif

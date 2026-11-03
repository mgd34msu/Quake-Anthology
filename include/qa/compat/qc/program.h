/* Port of src/compat/qc/program.ts. GPL-2.0-or-later.
 * QcProgramError throws abort via qa_range_error (message
 * "<source>: <msg>"). Names are strdup'd (owned by program).
 * digest is an owned qa_content_digest string. */
#ifndef QA_COMPAT_QC_PROGRAM_H
#define QA_COMPAT_QC_PROGRAM_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "qa/contracts/content.h"
#include "qa/contracts/execution.h"

typedef enum qa_compat_qc_opcode {
  QA_QC_OP_DONE, QA_QC_OP_MUL_F, QA_QC_OP_MUL_V, QA_QC_OP_MUL_FV, QA_QC_OP_MUL_VF,
  QA_QC_OP_DIV_F, QA_QC_OP_ADD_F, QA_QC_OP_ADD_V, QA_QC_OP_SUB_F, QA_QC_OP_SUB_V,
  QA_QC_OP_EQ_F, QA_QC_OP_EQ_V, QA_QC_OP_EQ_S, QA_QC_OP_EQ_E, QA_QC_OP_EQ_FN,
  QA_QC_OP_NE_F, QA_QC_OP_NE_V, QA_QC_OP_NE_S, QA_QC_OP_NE_E, QA_QC_OP_NE_FN,
  QA_QC_OP_LE, QA_QC_OP_GE, QA_QC_OP_LT, QA_QC_OP_GT,
  QA_QC_OP_LOAD_F, QA_QC_OP_LOAD_V, QA_QC_OP_LOAD_S, QA_QC_OP_LOAD_ENT, QA_QC_OP_LOAD_FLD, QA_QC_OP_LOAD_FN,
  QA_QC_OP_ADDRESS,
  QA_QC_OP_STORE_F, QA_QC_OP_STORE_V, QA_QC_OP_STORE_S, QA_QC_OP_STORE_ENT, QA_QC_OP_STORE_FLD, QA_QC_OP_STORE_FN,
  QA_QC_OP_STOREP_F, QA_QC_OP_STOREP_V, QA_QC_OP_STOREP_S, QA_QC_OP_STOREP_ENT, QA_QC_OP_STOREP_FLD, QA_QC_OP_STOREP_FN,
  QA_QC_OP_RETURN, QA_QC_OP_NOT_F, QA_QC_OP_NOT_V, QA_QC_OP_NOT_S, QA_QC_OP_NOT_ENT, QA_QC_OP_NOT_FN,
  QA_QC_OP_IF, QA_QC_OP_IFNOT,
  QA_QC_OP_CALL0, QA_QC_OP_CALL1, QA_QC_OP_CALL2, QA_QC_OP_CALL3, QA_QC_OP_CALL4,
  QA_QC_OP_CALL5, QA_QC_OP_CALL6, QA_QC_OP_CALL7, QA_QC_OP_CALL8,
  QA_QC_OP_STATE, QA_QC_OP_GOTO, QA_QC_OP_AND, QA_QC_OP_OR, QA_QC_OP_BITAND, QA_QC_OP_BITOR
} qa_compat_qc_opcode;

/* TS QcValueType strings kept verbatim. */
typedef const char *qa_compat_qc_value_type;

typedef struct qa_compat_qc_definition {
  qa_compat_qc_value_type type; int64_t native_type; bool save; int64_t offset; char *name; /* owned */
} qa_compat_qc_definition;
typedef struct qa_compat_qc_statement { qa_compat_qc_opcode opcode; int64_t a, b, c; } qa_compat_qc_statement;
typedef struct qa_compat_qc_function {
  int64_t index, first_statement, parameter_start, local_words;
  char *name, *file; /* owned */
  int64_t *parameter_sizes; size_t parameter_size_count; bool named_builtin;
} qa_compat_qc_function;

typedef struct qa_compat_qc_program {
  char *source; /* owned */
  qa_quakec_api_identity api;
  qa_compat_qc_statement *statements; size_t statement_count;
  qa_compat_qc_definition *globals; size_t global_count;
  qa_compat_qc_definition *fields; size_t field_count;
  qa_compat_qc_function *functions; size_t function_count;
  uint8_t *strings; size_t string_length;
  uint8_t *initial_globals; size_t initial_globals_length;
  int64_t entity_field_words; int64_t checksum; char *digest; /* owned qa_content_digest */
} qa_compat_qc_program;

/* TS QcProgramError: noreturn, aborts with "<source>: <msg>". */
#include <stdnoreturn.h>
noreturn void qa_compat_qc_program_error(const char *source, const char *format, ...);
/* Malloc'd byte string at offset (caller frees); aborts on bad offset. NULL only on OOM. */
char *qa_compat_qc_byte_string(const uint8_t *bytes, size_t length, int64_t offset);
int64_t qa_compat_qc_signed_branch(int64_t word);
/* First-match name lookups (TS *ByName maps); NULL when absent. */
/* TS functionEnd: the statement index one past the function, the next function's first statement or the end of the program. */
int64_t qa_compat_qc_program_function_end(const qa_compat_qc_program *p, int64_t first_statement);
const qa_compat_qc_definition *qa_compat_qc_program_global_named(const qa_compat_qc_program *p, const char *name);
const qa_compat_qc_definition *qa_compat_qc_program_field_named(const qa_compat_qc_program *p, const char *name);
const qa_compat_qc_function *qa_compat_qc_program_function_named_map(const qa_compat_qc_program *p, const char *name);
/* TS functionAt / functionNamed: abort (QcProgramError) when invalid. */
const qa_compat_qc_function *qa_compat_qc_program_function_at(const qa_compat_qc_program *p, int64_t index);
const qa_compat_qc_function *qa_compat_qc_program_function_named(const qa_compat_qc_program *p, const char *name);
/* expected_api NULL = TS undefined; source NULL = "progs.dat". False only on OOM. */
bool qa_compat_qc_load_program(const uint8_t *bytes, size_t length, const qa_quakec_api_identity *expected_api, const char *source, qa_compat_qc_program *out);
void qa_compat_qc_program_destroy(qa_compat_qc_program *p);
#endif

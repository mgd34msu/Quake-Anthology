/*
 * Port of src/bots/behavior/library/structure.ts (botlib/l_struct.c).
 * C accommodations: StructureReader methods that TS lets throw
 * ScriptLanguageError return false with reader->failed set (the failing
 * diagnostic is the last entry of the reader's reported list, unless the
 * failure came from the token source). Set capture_range_errors for a caller
 * whose TS catch also handles RangeError from structureFloat: the message lands
 * in range_error and the method returns false. Otherwise RangeErrors abort.
 * GPL-2.0-or-later.
 */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_STRUCTURE_H
#define QA_BOTS_BEHAVIOR_LIBRARY_STRUCTURE_H
#include "qa/ui/common/legacy/script/lexer.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_MAX_STRINGFIELD 80
#define QA_STRUCTURE_DEFAULT_STRING_BYTES 79
typedef enum qa_structure_field_type {
  QA_STRUCT_CHAR = 1,
  QA_STRUCT_INT = 2,
  QA_STRUCT_FLOAT = 3,
  QA_STRUCT_STRING = 4,
  QA_STRUCT_STRUCT = 6,
  QA_STRUCT_TYPE = 0xff,
  QA_STRUCT_ARRAY = 0x100,
  QA_STRUCT_BOUNDED = 0x200,
  QA_STRUCT_UNSIGNED = 0x400
} qa_structure_field_type;
typedef struct qa_structure_definition qa_structure_definition;
typedef struct qa_structure_field_definition {
  const char *name;
  int64_t offset;
  int64_t type;
  int64_t maxarray;
  double floatmin, floatmax;
  const qa_structure_definition *substruct;
} qa_structure_field_definition;
struct qa_structure_definition {
  int64_t size;
  const qa_structure_field_definition *fields;
  size_t field_count;
};
/** fprintf-compatible result: a negative count stops the source writer. */
typedef int64_t (*qa_structure_write_fn)(void *ctx, const char *text);
const qa_structure_field_definition *
qa_find_structure_field(const qa_structure_field_definition *fields, size_t count,
                        const char *name);

enum { QA_STRUCTURE_SOURCE_FAILURE = -1, QA_STRUCTURE_SOURCE_END = 0, QA_STRUCTURE_SOURCE_TOKEN = 1 };
/** TS StructureTokenSource. next stores one owned token reference when it
 * returns TOKEN; SOURCE_FAILURE is a thrown error isSourceFailure accepts.
 * unread consumes the caller's token reference. */
typedef struct qa_structure_token_source {
  void *ctx;
  int (*next)(void *ctx, qa_ui_script_token **out);
  void (*unread)(void *ctx, qa_ui_script_token *token);
} qa_structure_token_source;
/** TS StructureField: holds one token reference for name and location. */
typedef struct qa_structure_field {
  const char *name;
  const qa_ui_script_location *location;
  qa_ui_script_token *token;
} qa_structure_field;
void qa_structure_field_release(qa_structure_field *field);

typedef struct qa_structure_reader {
  qa_structure_token_source source;
  char *path;
  qa_ui_script_diagnostic *reported;
  size_t reported_count, reported_capacity;
  qa_ui_script_report_fn report;
  void *report_ctx;
  bool failed;
  bool capture_range_errors;
  char *range_error;
} qa_structure_reader;
void qa_structure_reader_init(qa_structure_reader *reader, qa_structure_token_source source,
                              const char *path, const qa_ui_script_diagnostic *prior,
                              size_t prior_count, qa_ui_script_report_fn report, void *report_ctx);
void qa_structure_reader_destroy(qa_structure_reader *reader);
const qa_ui_script_diagnostic *qa_structure_reader_diagnostics(const qa_structure_reader *reader,
                                                               size_t *out_count);
bool qa_structure_reader_begin(qa_structure_reader *reader);
/* True on success; *out_found false is the closing brace (TS undefined). */
bool qa_structure_reader_next_field(qa_structure_reader *reader, qa_structure_field *out,
                                    bool *out_found);
/* TS rejectField: reports and returns false. */
bool qa_structure_reader_reject_field(qa_structure_reader *reader, const qa_structure_field *field);
bool qa_structure_reader_read_int(qa_structure_reader *reader, int32_t *out);
bool qa_structure_reader_read_float(qa_structure_reader *reader, float *out);
/* *out is malloc'd (bytes before NUL, cut to maximum_bytes). */
bool qa_structure_reader_read_string(qa_structure_reader *reader, int64_t maximum_bytes, char **out);
bool qa_structure_reader_read_float_array(qa_structure_reader *reader, int64_t maximum_values,
                                          float *out, size_t *out_count);
/* TS readStructure(definition, bytes, base); false reports a source failure. */
bool qa_structure_reader_read_structure(qa_structure_reader *reader,
                                        const qa_structure_definition *definition, uint8_t *bytes,
                                        size_t length, int64_t base);
bool qa_write_indent_bots_a(qa_structure_write_fn write, void *ctx, int64_t indent);
bool qa_write_float_bots_a(qa_structure_write_fn write, void *ctx, double value);
bool qa_write_structure_bots_a(qa_structure_write_fn write, void *ctx,
                               const qa_structure_definition *def, const uint8_t *bytes,
                               size_t length);
bool qa_write_struct_with_indent_bots_a(qa_structure_write_fn write, void *ctx,
                                        const qa_structure_definition *def, const uint8_t *bytes,
                                        size_t length, int64_t indent, int64_t base);
#endif

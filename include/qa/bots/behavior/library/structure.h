/* Port of src/bots/behavior/library/structure.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_LIBRARY_STRUCTURE_H
#define QA_BOTS_BEHAVIOR_LIBRARY_STRUCTURE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#define QA_MAX_STRINGFIELD 80
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
typedef int64_t (*qa_structure_write_fn)(void *ctx, const char *text);
const qa_structure_field_definition *
qa_find_structure_field(const qa_structure_field_definition *fields, size_t count,
                        const char *name);
bool qa_write_indent_bots_a(qa_structure_write_fn write, void *ctx, int64_t indent);
bool qa_write_float_bots_a(qa_structure_write_fn write, void *ctx, double value);
bool qa_write_structure_bots_a(qa_structure_write_fn write, void *ctx,
                               const qa_structure_definition *def, const uint8_t *bytes,
                               size_t length);
bool qa_write_struct_with_indent_bots_a(qa_structure_write_fn write, void *ctx,
                                        const qa_structure_definition *def, const uint8_t *bytes,
                                        size_t length, int64_t indent);
#endif

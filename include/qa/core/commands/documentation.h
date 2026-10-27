/*
 * Port of src/core/commands/documentation.ts (CommandDocumentation):
 * a types-only module, so this header has no .c counterpart.
 * C accommodation: readonly string arrays become pointer + count;
 * the optional allowedValues is NULL when absent (the TS `?` field),
 * which stays distinct from a present empty array via the count.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_COMMANDS_DOCUMENTATION_H
#define QA_CORE_COMMANDS_DOCUMENTATION_H

#include <stddef.h>

typedef struct qa_command_documentation {
  const char *summary;
  const char *usage;
  const char *const *examples;
  size_t example_count;
  const char *const *allowed_values; /* NULL when the TS field is absent */
  size_t allowed_value_count;
} qa_command_documentation;

#endif

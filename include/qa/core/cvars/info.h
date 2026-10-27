/*
 * Port of src/core/cvars/info.ts: Info_SetValueForKey. C
 * accommodations: strings are NUL-terminated bytes (lengths are byte
 * counts; the TS works on source byte strings), the result is a
 * malloc'd string (NULL only on allocation failure), and the options'
 * print closure becomes function pointer + ctx. RangeErrors abort
 * with the TS messages.
 * GPL-2.0-or-later.
 */
#ifndef QA_CORE_CVARS_INFO_H
#define QA_CORE_CVARS_INFO_H

#include <stdbool.h>
#include <stdint.h>

#include "qa/contracts/common.h"
#include "qa/core/commands/text.h"

/** TS InfoOptions. */
typedef struct qa_info_options {
  qa_command_dialect dialect;
  int64_t maximum_length;
  qa_cvar_info_target target;
  bool server_high_characters;
  qa_command_print_fn print;
  void *print_ctx;
} qa_info_options;

char *qa_set_info_value(const char *input, const char *key_input,
                        const char *value_input, const qa_info_options *options);

#endif

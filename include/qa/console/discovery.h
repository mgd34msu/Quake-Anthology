/*
 * Port of src/console/discovery.ts: command/alias/cvar discovery
 * entries, their help text, and the find/help engine commands.
 * C accommodations:
 *  - Entry strings are owned copies; NULL is the TS undefined.
 *    has_allowed_values distinguishes a present empty TS array from
 *    an absent allowedValues field.
 *  - Query/find results are malloc'd arrays freed with
 *    qa_console_discovery_entries_free(); help lines with
 *    qa_console_discovery_lines_free().
 *  - The TS unregister closure becomes a registration object freed
 *    by qa_console_discovery_registration_remove(), which unregisters
 *    exactly the names that were registered.
 *  - Sorting uses strcmp on the name: TS localeCompare can differ
 *    for case/punctuation in some locales; entry names here are
 *    command identifiers (see src/llm/models.c for the same note).
 * GPL-2.0-or-later.
 */
#ifndef QA_CONSOLE_DISCOVERY_H
#define QA_CONSOLE_DISCOVERY_H

#include <stdbool.h>
#include <stddef.h>

#include "qa/contracts/common.h"
#include "qa/core/commands/index.h"

typedef enum qa_console_discovery_kind {
  QA_CONSOLE_ENTRY_COMMAND, /* "command" */
  QA_CONSOLE_ENTRY_ALIAS,   /* "alias" */
  QA_CONSOLE_ENTRY_CVAR     /* "cvar" */
} qa_console_discovery_kind;

const char *qa_console_discovery_kind_name(qa_console_discovery_kind kind);

typedef struct qa_console_discovery_entry {
  qa_console_discovery_kind kind;
  char *name;
  char *summary; /* NULL = TS undefined */
  char *usage;   /* NULL = TS undefined */
  char **examples;
  size_t example_count;
  char **allowed_values;
  size_t allowed_value_count;
  bool has_allowed_values; /* false = TS undefined */
  char *value;             /* alias/cvar; NULL for command */
  char *reset_value;       /* cvar only */
  char *latched_value;     /* cvar; NULL = TS undefined */
} qa_console_discovery_entry;

/* `source` NULL is the TS omitted argument. Caller frees the array. */
qa_console_discovery_entry *qa_query_console_entries(
    qa_command_buffer *commands, const qa_command_context *source,
    size_t *out_count);
qa_console_discovery_entry *qa_find_console_entries(
    qa_command_buffer *commands, const char *text,
    const qa_command_context *source, size_t *out_count);
void qa_console_discovery_entries_free(qa_console_discovery_entry *entries,
                                       size_t count);
/* Malloc'd help lines (without trailing newlines); caller frees. */
char **qa_console_entry_help(const qa_console_discovery_entry *entry,
                             size_t *out_count);
void qa_console_discovery_lines_free(char **lines, size_t count);

typedef struct qa_console_discovery_registration {
  qa_command_buffer *commands;
  qa_command_print_fn print;
  void *print_ctx;
  const char *names[2];
  size_t name_count;
} qa_console_discovery_registration;

/* NULL only on OOM. The registration borrows commands/print. */
qa_console_discovery_registration *qa_register_discovery_commands(
    qa_command_buffer *commands, qa_command_print_fn print, void *print_ctx);
/* The TS returned closure: unregisters find/help and frees. */
void qa_console_discovery_registration_remove(
    qa_console_discovery_registration *registration);

#endif

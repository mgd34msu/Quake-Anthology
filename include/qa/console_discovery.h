#ifndef QA_CONSOLE_DISCOVERY_H
#define QA_CONSOLE_DISCOVERY_H
#include "qa/console.h"
typedef enum qa_console_entry_kind {
    QA_CONSOLE_COMMAND,
    QA_CONSOLE_ALIAS,
    QA_CONSOLE_CVAR
} qa_console_entry_kind;
typedef struct qa_console_discovery_entry {
    qa_console_entry_kind kind;
    const char *name, *summary, *value, *reset_value, *latched_value;
    const qa_console_documentation *documentation;
} qa_console_discovery_entry;
typedef struct qa_console_discovery {
    qa_console_discovery_entry *entries;
    size_t count;
} qa_console_discovery;
/* Snapshot array owns its allocation. Entry strings are borrowed from the
 * registries, which must remain unchanged until it has been consumed. */
bool qa_console_discover(qa_console *, const qa_command_context *, qa_console_discovery *,
                         qa_error *);
void qa_console_discovery_free(qa_console_discovery *);
bool qa_console_discovery_find(qa_console *, const qa_command_context *, const char *name,
                               qa_console_discovery_entry *out);
bool qa_console_entry_matches(const qa_console_discovery_entry *, const char *query);
bool qa_console_entry_help(const qa_console_discovery_entry *, qa_buffer *out, qa_error *);
typedef struct qa_console_discovery_commands qa_console_discovery_commands;
typedef struct qa_console_discovery_options {
    qa_console *commands;
    uint64_t owner;
    void *context;
    void (*print)(void *, const qa_command_context *, const char *);
} qa_console_discovery_options;
qa_console_discovery_commands *qa_console_discovery_register(const qa_console_discovery_options *,
                                                             qa_error *);
/* Destroy before the command registry, outside an active invocation. */
void qa_console_discovery_unregister(qa_console_discovery_commands *);
#endif

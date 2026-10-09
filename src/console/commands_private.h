#ifndef QA_CONSOLE_COMMANDS_PRIVATE_H
#define QA_CONSOLE_COMMANDS_PRIVATE_H
#include "internal.h"
typedef struct qa_console_release qa_console_release;

typedef struct command_contribution {
    qa_console_entry view;
    uint64_t lifetime_owner, cvar_view;
    qa_command_handler handler;
    qa_command_fallback callback;
    void *user;
    uint32_t seat;
    bool ordinary, scoped, retired;
    struct command_contribution *next;
} command_contribution;
typedef struct command_entry {
    char *name;
    command_contribution *contributions;
    unsigned calls;
    struct command_entry *next;
} command_entry;
typedef struct alias_entry {
    qa_console_entry view;
    qa_ruleset_id dialect;
    bool console_text;
    struct alias_entry *next;
} alias_entry;
typedef struct command_chunk {
    qa_command_context context;
    qa_command_context caller;
    /* Borrow scripts from context/caller while a prepared tail awaits its
     * actual publication. These fields never own strings or handlers. */
    qa_command_context program_source, program_caller;
    qa_command_context program_bound, program_bound_caller;
    bool program_pending;
    char *text;
    size_t offset;
    size_t length;
    bool completion;
    bool success;
    struct command_chunk *next;
} command_chunk;
struct qa_console_release {
    qa_console *console;
    struct qa_console_release *next;
    qa_command_context context;
    command_chunk *prepared;
    command_chunk *head, *tail, *deferred, *deferred_tail;
    size_t queued_bytes, deferred_bytes;
    int32_t wait;
    qa_command_context wait_context;
    size_t alias_count;
    bool drain_yielded, started, entered, complete;
    bool imported, claimed;
    qa_error fault;
};
typedef struct retired_id {
    uint64_t value;
    struct retired_id *next;
} retired_id;
typedef struct command_call {
    uint64_t receiver, lifetime_owner, cvar_view;
    struct command_call *parent;
} command_call;
typedef struct command_frame {
    const qa_command_invocation *invocation;
    command_call *contributions;
    struct command_frame *parent;
} command_frame;
typedef struct output_frame {
    void (*print)(void *, const qa_command_context *, const char *);
    void *user;
    struct output_frame *parent;
} output_frame;
typedef struct console_source {
    qa_console_options options;
    char *startup;
    struct console_source *next;
} console_source;
struct qa_console_program;
struct qac_cvar_scope;
struct qa_console {
    qa_console_options options;
    console_source *sources;
    char *startup;
    command_entry *commands;
    alias_entry *aliases;
    command_chunk *head;
    command_chunk *tail;
    command_chunk *deferred;
    command_chunk *deferred_tail;
    size_t queued_bytes;
    size_t deferred_bytes;
    int32_t wait;
    qa_command_context wait_context;
    retired_id *owners;
    retired_id *clients;
    command_frame *frame;
    output_frame *redirect;
    unsigned output_calls;
    size_t alias_count;
    unsigned program_leases;
    uint64_t program_revision;
    bool program_revision_exhausted;
    struct qa_console_program *pending_program;
    bool program_unpublished, program_aborted;
    qa_command_context program_wait_source, program_wait_bound;
    bool program_wait_pending;
    bool draining;
    bool drain_yielded;
    size_t release_leases;
    qa_console_release *release_owner;
    qa_console_release *release_first;
    bool release_advancing;
    const qa_command_context *release_dispatch_context;
    struct qac_cvar_scope *cvar_scope;
};
bool qac_console_context_capture(qa_console *, const qa_command_context *, qa_command_context *, qa_error *);
bool qac_console_context_current(const qa_console *, const qa_command_context *, qa_error *);
bool qac_console_context_view_current(const qa_console *,const qa_command_context *,bool publication,qa_error *);
bool qac_console_release_access(const qa_console *, qa_error *);
void qac_console_release_enter(qa_console *);
/* Shared namespaces remain visible to the retained program during a release.
 * Only the actual isolated release queue may omit its transient mutations. */
void qac_console_program_touch(qa_console *, bool shared_namespace);
bool qac_console_program_drain_allowed(const qa_console *, qa_error *);
bool qac_console_program_immediate_allowed(const qa_console *, qa_error *);
#endif

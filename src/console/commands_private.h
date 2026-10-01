#ifndef QA_CONSOLE_COMMANDS_PRIVATE_H
#define QA_CONSOLE_COMMANDS_PRIVATE_H
#include "internal.h"

typedef struct command_contribution {
    uint64_t owner;
    struct command_contribution *next;
} command_contribution;
typedef struct command_entry {
    qa_console_entry view;
    qa_command_handler handler;
    void *user;
    command_contribution *contributions;
    uint64_t registration_owner;
    bool ordinary_registration;
    struct command_entry *next;
} command_entry;
typedef struct alias_entry {
    qa_console_entry view;
    qa_console_dialect dialect;
    bool console_text;
    struct alias_entry *next;
} alias_entry;
typedef struct command_chunk {
    qa_command_context context;
    qa_command_context caller;
    char *text;
    size_t offset;
    size_t length;
    bool completion;
    bool success;
    struct command_chunk *next;
} command_chunk;
typedef struct retired_id {
    uint64_t value;
    struct retired_id *next;
} retired_id;
typedef struct command_frame {
    const qa_command_invocation *invocation;
    struct command_frame *parent;
} command_frame;
typedef struct output_frame {
    void (*print)(void *, const qa_command_context *, const char *);
    void *user;
    struct output_frame *parent;
} output_frame;
struct qa_console {
    qa_console_options options;
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
    bool draining;
    bool drain_yielded;
};
#endif

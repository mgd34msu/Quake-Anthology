/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct command_entry {
    qa_console_entry view;
    qa_command_handler handler;
    void *user;
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
    size_t alias_count;
    bool draining;
};

static bool builtin(qa_console *console, const qa_command_invocation *command,
                     bool *handled, qa_error *error);
static bool dispatch(qa_console *console, const qa_command_context *context,
                      const char *raw, qa_error *error);

static bool retired(const retired_id *list, uint64_t id)
{
    for (; list != NULL; list = list->next) if (list->value == id) return true;
    return false;
}

static const qa_command_context *context_for(const qa_console *console,
                                              const qa_command_context *context)
{
    if (context != NULL) return context;
    return console->frame == NULL ? &console->options.context : &console->frame->invocation->context;
}

static bool valid_context(const qa_console *console, const qa_command_context *context,
                           qa_error *error)
{
    if (context->session != console->options.context.session || !qac_dialect_valid(context->dialect) ||
        context->origin < QA_COMMAND_LOCAL || context->origin > QA_COMMAND_REMOTE)
        return qac_fail(error, QA_ERROR_ARGUMENT, "command context does not belong to this session");
    if (retired(console->owners, context->owner) || (context->client != 0 && retired(console->clients, context->client)))
        return qac_fail(error, QA_ERROR_ARGUMENT, "command owner or client has retired");
    return true;
}

static bool same_context(const qa_command_context *a, const qa_command_context *b,
                          bool ignore_direct)
{
    return a->session == b->session && a->owner == b->owner && a->client == b->client &&
           a->seat == b->seat && a->dialect == b->dialect && a->origin == b->origin &&
           a->console_text == b->console_text && (ignore_direct || a->direct == b->direct) &&
           ((a->script == NULL && b->script == NULL) ||
            (a->script != NULL && b->script != NULL && strcmp(a->script, b->script) == 0));
}

static bool copy_context(qa_command_context *out, const qa_command_context *source,
                          qa_error *error)
{
    *out = *source;
    if (source->script != NULL) {
        out->script = qac_copy(source->script, error);
        if (out->script == NULL) return false;
    }
    return true;
}

static void free_chunk(command_chunk *chunk)
{
    free((char *)chunk->context.script);
    free((char *)chunk->caller.script);
    free(chunk->text);
    free(chunk);
}

static void free_chunks(command_chunk *chunk)
{
    while (chunk != NULL) {
        command_chunk *next = chunk->next;
        free_chunk(chunk);
        chunk = next;
    }
}

static void output(qa_console *console, const qa_command_context *context, const char *text)
{
    if (console->options.print != NULL) console->options.print(console->options.user, context, text);
}

static void output_value(qa_console *console, const qa_command_context *context,
                           const char *name, const char *value)
{
    output(console, context, "\"");
    output(console, context, name);
    output(console, context, "\" is \"");
    output(console, context, value);
    output(console, context, "\"\n");
}

static qa_cvars *cvar_owner(qa_console *console, const qa_command_context *context,
                             const char *name)
{
    return console->options.cvar_owner == NULL ? console->options.cvars :
        console->options.cvar_owner(console->options.user, context, name);
}

static qa_cvars *visible_cvars(qa_console *console, const qa_command_context *context,
                                size_t index)
{
    if (console->options.visible_cvars != NULL)
        return console->options.visible_cvars(console->options.user, context, index);
    return index == 0 ? cvar_owner(console, context, "") : NULL;
}

static size_t buffer_limit(const qa_console *console, qa_console_dialect dialect)
{
    return console->options.maximum_buffer != 0 ? console->options.maximum_buffer :
        dialect == QA_CONSOLE_Q3 ? 16384 : 8192;
}

static command_chunk *text_chunk(const qa_command_context *context, const char *text,
                                   size_t length, bool newline, qa_error *error)
{
    command_chunk *chunk = calloc(1, sizeof(*chunk));
    if (chunk == NULL) { qac_fail(error, QA_ERROR_MEMORY, "allocating command chunk"); return NULL; }
    if (!copy_context(&chunk->context, context, error)) { free_chunk(chunk); return NULL; }
    qac_text contents = {0};
    if (!qac_text_add(&contents, text, length, error) ||
        (newline && !qac_text_add(&contents, "\n", 1, error))) {
        free(contents.data);
        free_chunk(chunk);
        return NULL;
    }
    chunk->text = contents.data;
    chunk->length = contents.size;
    return chunk;
}

static bool queue_text(qa_console *console, const qa_command_context *context,
                        const char *text, bool insert, qa_error *error)
{
    if (console == NULL || text == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid command buffer arguments");
    qa_command_context inherited = *context_for(console, context);
    if (context == NULL && console->frame != NULL) inherited.direct = false;
    context = &inherited;
    if (!valid_context(console, context, error)) return false;
    bool newline = insert && (context->dialect == QA_CONSOLE_QW || context->dialect == QA_CONSOLE_Q3);
    size_t length = strlen(text);
    size_t limit = buffer_limit(console, context->dialect);
    if (length > SIZE_MAX - (newline ? 1u : 0u))
        return qac_fail(error, QA_ERROR_MEMORY, "command text is too large");
    size_t added = length + (newline ? 1u : 0u);
    if (console->queued_bytes > limit || added > limit - console->queued_bytes ||
        (!insert && added == limit - console->queued_bytes) ||
        (insert && context->dialect != QA_CONSOLE_Q3 && added >= limit))
        return qac_fail(error, QA_ERROR_FORMAT, "command buffer overflow");
    if (added == 0) return true;
    command_chunk *chunk = text_chunk(context, text, length, newline, error);
    if (chunk == NULL) return false;
    if (insert) {
        chunk->next = console->head;
        console->head = chunk;
        if (console->tail == NULL) console->tail = chunk;
    } else {
        if (console->tail == NULL) console->head = chunk;
        else console->tail->next = chunk;
        console->tail = chunk;
    }
    console->queued_bytes += chunk->length;
    return true;
}

qa_console *qa_console_create(const qa_console_options *options, qa_error *error)
{
    if (options == NULL || !qac_dialect_valid(options->context.dialect) ||
        options->context.origin < QA_COMMAND_LOCAL || options->context.origin > QA_COMMAND_REMOTE ||
        options->maximum_command == 1) {
        qac_fail(error, QA_ERROR_ARGUMENT, "invalid console options");
        return NULL;
    }
    qa_console *console = calloc(1, sizeof(*console));
    if (console == NULL) { qac_fail(error, QA_ERROR_MEMORY, "allocating console"); return NULL; }
    console->options = *options;
    console->options.context.script = NULL;
    if (!copy_context(&console->options.context, &options->context, error)) { free(console); return NULL; }
    if (options->startup_commands != NULL) {
        console->startup = qac_copy(options->startup_commands, error);
        if (console->startup == NULL) { qa_console_destroy(console); return NULL; }
    }
    console->options.startup_commands = console->startup;
    return console;
}

static void free_command(command_entry *entry)
{
    free((char *)entry->view.name);
    free((char *)entry->view.description);
    free(entry);
}

static void free_alias(alias_entry *alias)
{
    free((char *)alias->view.name);
    free((char *)alias->view.alias_text);
    free(alias);
}

void qa_console_destroy(qa_console *console)
{
    if (console == NULL) return;
    while (console->commands != NULL) {
        command_entry *next = console->commands->next;
        free_command(console->commands);
        console->commands = next;
    }
    while (console->aliases != NULL) {
        alias_entry *next = console->aliases->next;
        free_alias(console->aliases);
        console->aliases = next;
    }
    free_chunks(console->head);
    free_chunks(console->deferred);
    while (console->owners != NULL) {
        retired_id *next = console->owners->next;
        free(console->owners);
        console->owners = next;
    }
    while (console->clients != NULL) {
        retired_id *next = console->clients->next;
        free(console->clients);
        console->clients = next;
    }
    free((char *)console->options.context.script);
    free((char *)console->wait_context.script);
    free(console->startup);
    free(console);
}

bool qa_console_set_profile(qa_console *console, qa_console_dialect dialect,
                              qa_cvars *cvars, qa_error *error)
{
    if (console == NULL || console->frame != NULL || !qac_dialect_valid(dialect) ||
        (cvars != NULL && qa_cvars_dialect(cvars) != dialect))
        return qac_fail(error, QA_ERROR_ARGUMENT, "console profile requires an inactive matching registry");
    console->options.context.dialect = dialect;
    console->options.cvars = cvars;
    return true;
}

static const qa_console_entry builtin_entries[] = {
    {"stuffcmds", "Execute startup commands", NULL, 0, true},
    {"exec", "Execute a content script", NULL, 0, true},
    {"echo", "Print console text", NULL, 0, true},
    {"alias", "Define a command alias", NULL, 0, true},
    {"cmd", "Forward a command to the server", NULL, 0, true},
    {"wait", "Pause queued commands", NULL, 0, true},
    {"cmdlist", "List console commands", NULL, 0, true},
    {"set", "Set a console variable", NULL, 0, true},
    {"cvarlist", "List visible console variables", NULL, 0, true},
    {"toggle", "Toggle or cycle a variable", NULL, 0, true},
    {"sets", "Set a server-info variable", NULL, 0, true},
    {"setu", "Set a user-info variable", NULL, 0, true},
    {"seta", "Set an archived variable", NULL, 0, true},
    {"reset", "Restore a variable default", NULL, 0, true},
    {"cvar_restart", "Restart the Q3 cvar registry", NULL, 0, true},
    {"vstr", "Execute a variable as commands", NULL, 0, true},
    {"inc", "Increase a numeric variable", NULL, 0, true},
    {"dec", "Decrease a numeric variable", NULL, 0, true},
    {"resetall", "Restore variable defaults", NULL, 0, true}
};

static bool builtin_allowed(qa_console_dialect dialect, const char *name)
{
    if (strcmp(name, "alias") == 0) return dialect != QA_CONSOLE_Q3;
    if (strcmp(name, "stuffcmds") == 0) return qac_q1(dialect);
    if (strcmp(name, "cvar_restart") == 0) return dialect == QA_CONSOLE_Q3;
    return true;
}

static bool is_builtin(const qa_console *console, const char *name)
{
    if (console->options.disable_builtins) return false;
    for (size_t i = 0; i < sizeof(builtin_entries) / sizeof(builtin_entries[0]); ++i)
        if (qac_equal(name, builtin_entries[i].name) &&
            builtin_allowed(context_for(console, NULL)->dialect, builtin_entries[i].name)) return true;
    return false;
}

bool qa_console_register(qa_console *console, const char *name, const char *description,
                           uint64_t owner, bool engine_command, qa_command_handler handler,
                           void *user, qa_error *error)
{
    if (console == NULL || name == NULL || *name == '\0' || strpbrk(name, " \t\r\n;\"") != NULL ||
        retired(console->owners, owner))
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid command declaration");
    if (is_builtin(console, name)) return qac_fail(error, QA_ERROR_ARGUMENT, "command name is a builtin");
    for (command_entry *entry = console->commands; entry != NULL; entry = entry->next)
        if (entry->view.owner == owner && strcmp(entry->view.name, name) == 0)
            return qac_fail(error, QA_ERROR_ARGUMENT, "command is already registered");
    qa_cvars *registry = cvar_owner(console, context_for(console, NULL), name);
    const qa_cvar_view *variable = qa_cvars_find(registry, name);
    if (context_for(console, NULL)->dialect != QA_CONSOLE_Q3 && variable != NULL && *variable->value != '\0')
        return qac_fail(error, QA_ERROR_ARGUMENT, "command name is already a cvar");
    command_entry *entry = calloc(1, sizeof(*entry));
    if (entry == NULL) return qac_fail(error, QA_ERROR_MEMORY, "allocating console command");
    entry->view.name = qac_copy(name, error);
    entry->view.description = qac_copy(description == NULL ? "" : description, error);
    if (entry->view.name == NULL || entry->view.description == NULL) { free_command(entry); return false; }
    entry->view.owner = owner;
    entry->view.engine_command = engine_command;
    entry->handler = handler;
    entry->user = user;
    entry->next = console->commands;
    console->commands = entry;
    return true;
}

bool qa_console_unregister(qa_console *console, const char *name, uint64_t owner)
{
    if (console == NULL || name == NULL) return false;
    command_entry **link = &console->commands;
    while (*link != NULL) {
        command_entry *entry = *link;
        if (entry->view.owner == owner && strcmp(entry->view.name, name) == 0) {
            *link = entry->next;
            free_command(entry);
            return true;
        }
        link = &entry->next;
    }
    return false;
}

const qa_console_entry *qa_console_entry_at(const qa_console *console, size_t ordinal)
{
    if (console == NULL) return NULL;
    for (command_entry *entry = console->commands; entry != NULL; entry = entry->next)
        if (ordinal-- == 0) return &entry->view;
    if (!console->options.disable_builtins)
        for (size_t i = 0; i < sizeof(builtin_entries) / sizeof(builtin_entries[0]); ++i)
            if (builtin_allowed(context_for(console, NULL)->dialect, builtin_entries[i].name) && ordinal-- == 0)
                return &builtin_entries[i];
    return NULL;
}

bool qa_console_alias(qa_console *console, const qa_command_context *context,
                        const char *name, const char *text, qa_error *error)
{
    if (console == NULL || name == NULL || text == NULL || *name == '\0' || strlen(name) >= 32)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid console alias name");
    context = context_for(console, context);
    if (!valid_context(console, context, error)) return false;
    if (context->dialect == QA_CONSOLE_Q3)
        return qac_fail(error, QA_ERROR_UNSUPPORTED, "Q3 uses vstr rather than command aliases");
    char *copy = qac_copy(text, error);
    if (copy == NULL) return false;
    for (alias_entry *alias = console->aliases; alias != NULL; alias = alias->next) {
        if (alias->view.owner != context->owner || strcmp(alias->view.name, name) != 0) continue;
        free((char *)alias->view.alias_text);
        alias->view.alias_text = copy;
        alias->dialect = context->dialect;
        alias->console_text = context->console_text;
        return true;
    }
    alias_entry *alias = calloc(1, sizeof(*alias));
    if (alias == NULL) { free(copy); return qac_fail(error, QA_ERROR_MEMORY, "allocating command alias"); }
    alias->view.name = qac_copy(name, error);
    if (alias->view.name == NULL) { free(copy); free(alias); return false; }
    alias->view.alias_text = copy;
    alias->view.owner = context->owner;
    alias->dialect = context->dialect;
    alias->console_text = context->console_text;
    alias->next = console->aliases;
    console->aliases = alias;
    return true;
}

const qa_console_entry *qa_console_alias_at(const qa_console *console, uint64_t owner,
                                            size_t ordinal)
{
    if (console == NULL) return NULL;
    for (alias_entry *alias = console->aliases; alias != NULL; alias = alias->next)
        if (alias->view.owner == owner && ordinal-- == 0) return &alias->view;
    return NULL;
}

bool qa_console_append(qa_console *console, const qa_command_context *context,
                         const char *text, qa_error *error)
{
    return queue_text(console, context, text, false, error);
}

bool qa_console_insert(qa_console *console, const qa_command_context *context,
                         const char *text, qa_error *error)
{
    return queue_text(console, context, text, true, error);
}

static bool expand_macros(qa_console *console, const qa_command_context *context,
                           const char *input, char **out, qa_error *error)
{
    size_t budget = strlen(input);
    if (budget >= 1024) return qac_fail(error, QA_ERROR_FORMAT, "Q2 command line exceeds 1023 bytes");
    char *text = qac_copy(input, error);
    if (text == NULL) return false;
    bool quoted = false;
    size_t expansions = 0;
    for (size_t offset = 0; text[offset] != '\0'; ++offset) {
        if (text[offset] == '"') quoted = !quoted;
        if (quoted || text[offset] != '$') continue;
        qac_token token;
        size_t length = strlen(text);
        if (!qac_parse_token(text, length, offset + 1, QA_CONSOLE_Q2, context->console_text, &token, error)) { free(text); return false; }
        if (!token.found) continue;
        char *name = qac_copy_n(text + token.start, token.size, error);
        if (name == NULL) { free(text); return false; }
        qa_cvars *registry = cvar_owner(console, context, name);
        const qa_cvar_view *variable = qa_cvars_find(registry, name);
        free(name);
        const char *value = variable == NULL || (qac_q2(qa_cvars_dialect(registry)) &&
            (variable->flags & QA_Q2_CVAR_PRIVATE) != 0) ? "" : variable->value;
        size_t added = strlen(value);
        if (added >= 1024 - budget || ++expansions >= 100) {
            free(text);
            return qac_fail(error, QA_ERROR_FORMAT, "Q2 macro expansion exceeds length or recursion limit");
        }
        budget += added;
        qac_text expanded = {0};
        bool ok = qac_text_add(&expanded, text, offset, error) &&
                  qac_text_add(&expanded, value, added, error) &&
                  qac_text_add(&expanded, text + token.end, length - token.end, error);
        free(text);
        if (!ok) { free(expanded.data); return false; }
        text = expanded.data;
        if (offset == 0) offset = SIZE_MAX;
        else --offset;
    }
    if (quoted) { free(text); return qac_fail(error, QA_ERROR_FORMAT, "Q2 command has unmatched quotes"); }
    *out = text;
    return true;
}

static qa_command_result fallback_call(qa_console *console, qa_command_fallback handler,
                                        const qa_command_invocation *command, qa_error *error)
{
    return handler == NULL ? QA_COMMAND_UNHANDLED : handler(console->options.user, command, error);
}

static bool fallback(qa_console *console, const qa_command_invocation *command, qa_error *error)
{
    qa_cvars *registry = cvar_owner(console, &command->context, command->argv[0]);
    const qa_cvar_view *variable = qa_cvars_find(registry, command->argv[0]);
    if (variable != NULL) {
        if (command->argc > 1) return qa_cvars_set(registry, variable->name, command->argv[1], false, error);
        output_value(console, &command->context, variable->name, variable->value);
        if (command->context.dialect == QA_CONSOLE_Q3) {
            output(console, &command->context, "default: ");
            output(console, &command->context, variable->reset_value);
            output(console, &command->context, "\n");
            if (variable->latched_value != NULL) {
                output(console, &command->context, "latched: ");
                output(console, &command->context, variable->latched_value);
                output(console, &command->context, "\n");
            }
        }
        return true;
    }
    if (command->context.dialect == QA_CONSOLE_Q3) {
        qa_command_fallback callbacks[] = {console->options.client_game, console->options.server_game, console->options.ui};
        for (size_t i = 0; i < sizeof(callbacks) / sizeof(callbacks[0]); ++i) {
            qa_command_result result = fallback_call(console, callbacks[i], command, error);
            if (result != QA_COMMAND_UNHANDLED) return result == QA_COMMAND_HANDLED;
        }
    }
    if (!qac_q1(command->context.dialect)) {
        qa_command_result result = fallback_call(console, console->options.forward, command, error);
        return result != QA_COMMAND_FAILED;
    }
    bool warn = command->context.dialect != QA_CONSOLE_QW;
    if (!warn) {
        const qa_cvar_view *warncmd = qa_cvars_find(cvar_owner(console, &command->context, "cl_warncmd"), "cl_warncmd");
        const qa_cvar_view *developer = qa_cvars_find(cvar_owner(console, &command->context, "developer"), "developer");
        warn = (warncmd != NULL && warncmd->number != 0) || (developer != NULL && developer->number != 0);
    }
    if (warn) {
        output(console, &command->context, "Unknown command \"");
        output(console, &command->context, command->argv[0]);
        output(console, &command->context, "\"\n");
    }
    return true;
}

static command_entry *select_command(const qa_console *console, const qa_command_context *context,
                                       const char *name)
{
    command_entry *global = NULL;
    for (command_entry *entry = console->commands; entry != NULL; entry = entry->next) {
        if (!qac_equal(entry->view.name, name)) continue;
        if (entry->view.engine_command && entry->view.owner == 0) return entry;
        if (entry->view.owner == context->owner) return entry;
        if (entry->view.owner == 0 && global == NULL) global = entry;
    }
    return global;
}

const qa_console_entry *qa_console_find(const qa_console *console,
                                         const qa_command_context *context,
                                         const char *name)
{
    if (console == NULL || name == NULL) return NULL;
    context = context_for(console, context);
    if (!console->options.disable_builtins)
        for (size_t i = 0; i < sizeof(builtin_entries) / sizeof(builtin_entries[0]); ++i)
            if (qac_equal(name, builtin_entries[i].name) && builtin_allowed(context->dialect, builtin_entries[i].name))
                return &builtin_entries[i];
    const command_entry *entry = select_command(console, context, name);
    return entry == NULL ? NULL : &entry->view;
}

static bool dispatch(qa_console *console, const qa_command_context *context,
                      const char *raw, qa_error *error)
{
    if (!valid_context(console, context, error)) return false;
    char *expanded = NULL;
    if (qac_q2(context->dialect)) {
        qa_error expansion_error = {0};
        if (!expand_macros(console, context, raw, &expanded, &expansion_error)) {
            if (expansion_error.code == QA_ERROR_FORMAT) {
                output(console, context, expansion_error.message);
                output(console, context, ", discarded.\n");
                return true;
            }
            if (error != NULL) *error = expansion_error;
            return false;
        }
    } else {
        expanded = qac_copy(raw, error);
        if (expanded == NULL) return false;
    }
    qa_command_tokens tokens = {0};
    if (!qa_command_tokenize(expanded, context->dialect, context->console_text, &tokens, error)) { free(expanded); return false; }
    free(expanded);
    if (tokens.count == 0) { qa_command_tokens_free(&tokens); return true; }
    qa_command_invocation command = {console, *context, tokens.count,
        (const char *const *)tokens.values, tokens.args_text, raw};
    command.context.direct = context->direct && context->script == NULL &&
        (context->origin == QA_COMMAND_LOCAL || context->origin == QA_COMMAND_SEAT);
    command_frame frame = {&command, console->frame};
    console->frame = &frame;
    bool success = true;
    if (console->options.allow_command != NULL && !console->options.allow_command(console->options.user, &command)) goto done;
    if (!valid_context(console, context, error)) { success = false; goto done; }
    bool handled = false;
    if (!console->options.disable_builtins) {
        success = builtin(console, &command, &handled, error);
        if (!success || handled) goto done;
    }
    command_entry *entry = select_command(console, context, tokens.values[0]);
    if (entry == NULL || !entry->view.engine_command) {
        qa_command_result result = fallback_call(console, console->options.source_command, &command, error);
        if (result != QA_COMMAND_UNHANDLED) { success = result == QA_COMMAND_HANDLED; goto done; }
        if (!valid_context(console, context, error)) { success = false; goto done; }
        /* The source callback may change the command registry. */
        entry = select_command(console, context, tokens.values[0]);
    }
    if (entry != NULL) {
        qa_command_handler handler = entry->handler;
        void *user = entry->user;
        if (context->dialect == QA_CONSOLE_Q3 && entry != console->commands) {
            command_entry **link = &console->commands;
            while (*link != entry) link = &(*link)->next;
            *link = entry->next;
            entry->next = console->commands;
            console->commands = entry;
        }
        if (handler != NULL) success = handler(user, &command, error);
        else if (qac_q1(context->dialect)) success = qac_fail(error, QA_ERROR_ARGUMENT, "Q1 command has no callback");
        else if (qac_q2(context->dialect)) {
            qa_command_result result = fallback_call(console, console->options.forward, &command, error);
            success = result != QA_COMMAND_FAILED;
        } else success = fallback(console, &command, error);
        goto done;
    }
    if (context->dialect != QA_CONSOLE_Q3) {
        for (alias_entry *alias = console->aliases; alias != NULL; alias = alias->next) {
            if (alias->view.owner != context->owner || !qac_equal(alias->view.name, tokens.values[0])) continue;
            if (qac_q2(context->dialect) && ++console->alias_count == 16) {
                output(console, context, "ALIAS_LOOP_COUNT\n");
                goto done;
            }
            qa_command_context derived = *context;
            derived.direct = false;
            derived.dialect = alias->dialect;
            derived.console_text = alias->console_text;
            success = qa_console_insert(console, &derived, alias->view.alias_text, error);
            goto done;
        }
    }
    success = fallback(console, &command, error);
done:
    console->frame = frame.parent;
    qa_command_tokens_free(&tokens);
    return success;
}

bool qa_console_execute_now(qa_console *console, const qa_command_context *context,
                              const char *text, qa_error *error)
{
    if (console == NULL) return qac_fail(error, QA_ERROR_ARGUMENT, "console is NULL");
    if (text == NULL || *text == '\0') return qa_console_drain(console, 0, NULL, error);
    qa_command_context inherited;
    if (!copy_context(&inherited, context_for(console, context), error)) return false;
    if (console->frame != NULL) inherited.direct = false;
    char *raw = qac_copy(text, error);
    bool ok = raw != NULL && dispatch(console, &inherited, raw, error);
    free(raw);
    free((char *)inherited.script);
    return ok;
}

static bool command_text(const command_chunk *head, qac_text *text, qa_error *error)
{
    qa_command_context expected = head->context;
    bool resumed = false;
    for (const command_chunk *chunk = head; chunk != NULL; chunk = chunk->next) {
        if (chunk->completion) {
            if (!qac_q2(head->context.dialect) || !chunk->success || expected.script == NULL ||
                !same_context(&expected, &chunk->context, true)) break;
            expected = chunk->caller;
            resumed = true;
            continue;
        }
        if (!same_context(&expected, &chunk->context, resumed)) break;
        if (!qac_text_add(text, chunk->text + chunk->offset, chunk->length - chunk->offset, error)) return false;
    }
    return true;
}

static void consume(qa_console *console, size_t bytes)
{
    command_chunk **link = &console->head;
    while (bytes != 0 && *link != NULL) {
        command_chunk *chunk = *link;
        if (chunk->completion) { link = &chunk->next; continue; }
        size_t available = chunk->length - chunk->offset;
        size_t taken = bytes < available ? bytes : available;
        chunk->offset += taken;
        console->queued_bytes -= taken;
        bytes -= taken;
        if (chunk->offset != chunk->length) break;
        *link = chunk->next;
        if (console->tail == chunk) console->tail = NULL;
        free_chunk(chunk);
    }
    if (console->head != NULL && console->tail == NULL) {
        console->tail = console->head;
        while (console->tail->next != NULL) console->tail = console->tail->next;
    }
}

bool qa_console_drain(qa_console *console, size_t budget, size_t *executed, qa_error *error)
{
    if (executed != NULL) *executed = 0;
    if (console == NULL || console->draining || console->frame != NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "command buffer is already executing");
    console->draining = true;
    console->alias_count = 0;
    size_t count = 0;
    bool success = true;
    if (console->head == NULL && console->wait != 0) {
        console->wait = console->wait > 0 ? console->wait - 1 : console->wait == INT32_MIN ? INT32_MAX : console->wait - 1;
    }
    while (console->head != NULL && (budget == 0 || count < budget)) {
        if (console->wait_context.dialect == QA_CONSOLE_Q3 && console->wait != 0) {
            if (console->wait > INT32_MIN) --console->wait;
            else console->wait = INT32_MAX;
            break;
        }
        command_chunk *first = console->head;
        if (first->completion) {
            console->head = first->next;
            if (console->tail == first) console->tail = NULL;
            qa_command_invocation invocation = {console, first->context, 0, NULL, "", ""};
            command_frame frame = {&invocation, console->frame};
            console->frame = &frame;
            if (console->options.script_complete != NULL)
                console->options.script_complete(console->options.user, &first->context, first->context.script, first->success);
            console->frame = frame.parent;
            free_chunk(first);
            continue;
        }
        qa_command_context context;
        if (!copy_context(&context, &first->context, error)) { success = false; break; }
        qac_text text = {0};
        if (!command_text(first, &text, error)) { free((char *)context.script); free(text.data); success = false; break; }
        bool quoted = false;
        size_t offset = 0;
        while (offset < text.size) {
            char c = text.data[offset];
            if (c == '"') quoted = !quoted;
            if ((!quoted && c == ';') || c == '\n' || (context.dialect == QA_CONSOLE_Q3 && c == '\r')) break;
            ++offset;
        }
        size_t maximum = console->options.maximum_command == 0 ? 1024 : console->options.maximum_command;
        if (offset >= maximum) {
            if (context.dialect != QA_CONSOLE_Q3) {
                success = qac_fail(error, QA_ERROR_FORMAT, "command line exceeds source buffer");
                free((char *)context.script); free(text.data); break;
            }
            offset = maximum - 1;
        }
        size_t consumed = offset < text.size ? offset + 1 : offset;
        text.data[offset] = '\0';
        consume(console, consumed);
        success = dispatch(console, &context, text.data, error);
        free((char *)context.script);
        free(text.data);
        ++count;
        if (!success) break;
        if (console->wait_context.dialect != QA_CONSOLE_Q3 && console->wait != 0) {
            console->wait = 0;
            break;
        }
    }
    console->draining = false;
    if (executed != NULL) *executed = count;
    return success;
}

bool qa_console_defer(qa_console *console, qa_error *error)
{
    if (console == NULL || !qac_q2(context_for(console, NULL)->dialect))
        return qac_fail(error, QA_ERROR_ARGUMENT, "deferred command buffers require Q2");
    free_chunks(console->deferred);
    console->deferred = console->head;
    console->deferred_tail = console->tail;
    console->deferred_bytes = console->queued_bytes;
    console->head = console->tail = NULL;
    console->queued_bytes = 0;
    return true;
}

bool qa_console_resume(qa_console *console, qa_error *error)
{
    if (console == NULL || !qac_q2(context_for(console, NULL)->dialect))
        return qac_fail(error, QA_ERROR_ARGUMENT, "deferred command buffers require Q2");
    size_t limit = buffer_limit(console, context_for(console, NULL)->dialect);
    if (console->queued_bytes > limit || console->deferred_bytes > limit - console->queued_bytes)
        return qac_fail(error, QA_ERROR_FORMAT, "resuming deferred commands overflows buffer");
    if (console->deferred == NULL) return true;
    console->deferred_tail->next = console->head;
    console->head = console->deferred;
    if (console->tail == NULL) console->tail = console->deferred_tail;
    console->queued_bytes += console->deferred_bytes;
    console->deferred = console->deferred_tail = NULL;
    console->deferred_bytes = 0;
    return true;
}

bool qa_console_pending(const qa_console *console)
{
    return console != NULL && (console->head != NULL || console->deferred != NULL || console->wait != 0);
}

static void discard_chunks(command_chunk **head, command_chunk **tail, size_t *bytes,
                             uint64_t id, bool owner)
{
    command_chunk **link = head;
    *tail = NULL;
    while (*link != NULL) {
        command_chunk *chunk = *link;
        if ((owner ? chunk->context.owner : chunk->context.client) == id) {
            *link = chunk->next;
            if (!chunk->completion) *bytes -= chunk->length - chunk->offset;
            free_chunk(chunk);
        } else {
            *tail = chunk;
            link = &chunk->next;
        }
    }
}

static bool remove_id(qa_console *console, uint64_t id, bool owner, qa_error *error)
{
    if (console == NULL || id == 0) return qac_fail(error, QA_ERROR_ARGUMENT, "cannot retire the engine or absent client");
    retired_id **list = owner ? &console->owners : &console->clients;
    if (retired(*list, id)) return true;
    retired_id *record = malloc(sizeof(*record));
    if (record == NULL) return qac_fail(error, QA_ERROR_MEMORY, "recording retired command owner");
    *record = (retired_id){id, *list};
    *list = record;
    discard_chunks(&console->head, &console->tail, &console->queued_bytes, id, owner);
    discard_chunks(&console->deferred, &console->deferred_tail, &console->deferred_bytes, id, owner);
    if ((owner ? console->wait_context.owner : console->wait_context.client) == id) {
        console->wait = 0;
        free((char *)console->wait_context.script);
        console->wait_context.script = NULL;
    }
    if (owner) {
        command_entry **command = &console->commands;
        while (*command != NULL) {
            command_entry *entry = *command;
            if (entry->view.owner != id) { command = &entry->next; continue; }
            *command = entry->next;
            free_command(entry);
        }
        alias_entry **alias = &console->aliases;
        while (*alias != NULL) {
            alias_entry *entry = *alias;
            if (entry->view.owner != id) { alias = &entry->next; continue; }
            *alias = entry->next;
            free_alias(entry);
        }
    }
    return true;
}

bool qa_console_remove_owner(qa_console *console, uint64_t owner, qa_error *error)
{
    return remove_id(console, owner, true, error);
}

bool qa_console_remove_client(qa_console *console, uint64_t client, qa_error *error)
{
    return remove_id(console, client, false, error);
}

static bool join_arguments(const qa_command_invocation *command, size_t first,
                            qac_text *out, qa_error *error)
{
    for (size_t i = first; i < command->argc; ++i)
        if ((i != first && !qac_text_add(out, " ", 1, error)) ||
            !qac_text_string(out, command->argv[i], error)) return false;
    return qac_text_add(out, "", 0, error);
}

static bool execute_script(qa_console *console, const qa_command_invocation *command,
                            qa_error *error)
{
    if (command->argc != 2) { output(console, &command->context, "exec <filename>\n"); return true; }
    qac_text filename = {0};
    if (!qac_text_string(&filename, command->argv[1], error)) return false;
    const char *base = strrchr(filename.data, '/');
    if (base == NULL) base = filename.data;
    if (command->context.dialect == QA_CONSOLE_Q3 && strchr(base, '.') == NULL &&
        !qac_text_add(&filename, ".cfg", 4, error)) { free(filename.data); return false; }
    qa_bytes bytes = {0};
    void *lease = NULL;
    qa_error read_error = {0};
    bool found = console->options.read_script != NULL &&
        console->options.read_script(console->options.user, &command->context, filename.data, &bytes, &lease, &read_error);
    if (bytes.data == NULL && bytes.size != 0) {
        found = false;
        qa_error_set(&read_error, QA_ERROR_ARGUMENT, 0, "script reader returned invalid bytes");
    }
    command_chunk *completion = calloc(1, sizeof(*completion));
    command_chunk *text = NULL;
    if (completion == NULL) { qac_fail(error, QA_ERROR_MEMORY, "allocating script completion"); goto fail; }
    completion->completion = true;
    completion->success = found;
    completion->context = command->context;
    completion->context.script = filename.data;
    completion->context.direct = false;
    filename.data = NULL;
    if (!copy_context(&completion->caller, &command->context, error)) goto fail;
    if (found) {
        size_t length = 0;
        while (length < bytes.size && bytes.data[length] != 0) ++length;
        bool newline = command->context.dialect == QA_CONSOLE_Q3 || command->context.dialect == QA_CONSOLE_QW ||
            (command->context.dialect == QA_CONSOLE_Q1 && (length == 0 || bytes.data[length - 1] != '\n'));
        text = text_chunk(&completion->context, (const char *)bytes.data, length, newline, error);
        if (text == NULL) goto fail;
        size_t limit = buffer_limit(console, command->context.dialect);
        if (console->queued_bytes > limit || text->length > limit - console->queued_bytes) {
            qac_fail(error, QA_ERROR_FORMAT, "exec script overflows command buffer");
            goto fail;
        }
    }
    if (console->options.release_script != NULL) console->options.release_script(console->options.user, lease);
    lease = NULL;
    output(console, &command->context, found ? "execing " : "couldn't exec ");
    output(console, &command->context, completion->context.script);
    if (!found && read_error.message[0] != '\0') {
        output(console, &command->context, ": ");
        output(console, &command->context, read_error.message);
    }
    output(console, &command->context, "\n");
    if (!valid_context(console, &completion->context, error)) {
        if (text != NULL) free_chunk(text);
        free_chunk(completion);
        return false;
    }
    size_t limit = buffer_limit(console, command->context.dialect);
    if (text != NULL && (console->queued_bytes > limit || text->length > limit - console->queued_bytes)) {
        free_chunk(text);
        free_chunk(completion);
        return qac_fail(error, QA_ERROR_FORMAT, "exec script overflows command buffer after content callback");
    }
    completion->next = console->head;
    if (text != NULL && text->length != 0) {
        text->next = completion;
        console->head = text;
        console->queued_bytes += text->length;
    } else {
        if (text != NULL) free_chunk(text);
        console->head = completion;
    }
    if (console->tail == NULL) console->tail = completion;
    return true;
fail:
    if (console->options.release_script != NULL) console->options.release_script(console->options.user, lease);
    free(filename.data);
    if (text != NULL) free_chunk(text);
    if (completion != NULL) free_chunk(completion);
    return false;
}

static bool decimal_text(const char *text)
{
    if (*text == '-') ++text;
    bool digit = false;
    bool dot = false;
    for (; *text != '\0'; ++text) {
        if (*text == '.' && !dot) { dot = true; continue; }
        if (*text < '0' || *text > '9') return false;
        digit = true;
    }
    return digit || dot;
}

static bool reset_all(qa_console *console, const qa_command_context *context, qa_error *error)
{
    for (size_t i = 0;; ++i) {
        qa_cvars *registry = visible_cvars(console, context, i);
        if (registry == NULL) break;
        for (size_t n = 0; n < qa_cvars_count(registry); ++n) {
            const qa_cvar_view *variable = qa_cvars_at(registry, n);
            if (cvar_owner(console, context, variable->name) != registry ||
                strcmp(variable->name, "game") == 0 || strcmp(variable->name, "fs_game") == 0) continue;
            qa_console_dialect dialect = qa_cvars_dialect(registry);
            uint32_t protected = qac_q2(dialect) ? QA_Q2_CVAR_NOSET | QA_Q2_CVAR_READONLY :
                dialect == QA_CONSOLE_Q3 ? QA_CVAR_READONLY | QA_CVAR_INIT | QA_CVAR_NO_RESTART : 0;
            if ((variable->flags & protected) != 0) continue;
            if (!qa_cvars_set_console(registry, variable->name, variable->reset_value, error)) return false;
        }
    }
    return true;
}

static bool builtin(qa_console *console, const qa_command_invocation *command,
                     bool *handled, qa_error *error)
{
    const char *name = command->argv[0];
    const qa_command_context *context = &command->context;
    *handled = true;
    if (qac_equal(name, "wait")) {
        qa_command_context saved;
        if (!copy_context(&saved, context, error)) return false;
        free((char *)console->wait_context.script);
        console->wait_context = saved;
        console->wait = context->dialect == QA_CONSOLE_Q3 && command->argc == 2 ? qac_integer(command->argv[1]) : 1;
        return true;
    }
    if (qac_equal(name, "echo")) {
        for (size_t i = 1; i < command->argc; ++i) { output(console, context, command->argv[i]); output(console, context, " "); }
        output(console, context, "\n");
        return true;
    }
    if (qac_equal(name, "cmd")) return fallback_call(console, console->options.forward, command, error) != QA_COMMAND_FAILED;
    if (qac_equal(name, "exec")) return execute_script(console, command, error);
    if (qac_equal(name, "stuffcmds") && qac_q1(context->dialect))
        return console->startup == NULL || qa_console_insert(console, context, console->startup, error);
    if (qac_equal(name, "alias") && context->dialect != QA_CONSOLE_Q3) {
        if (command->argc == 1) {
            for (alias_entry *alias = console->aliases; alias != NULL; alias = alias->next)
                if (alias->view.owner == context->owner) output_value(console, context, alias->view.name, alias->view.alias_text);
            return true;
        }
        qac_text text = {0};
        bool ok = join_arguments(command, 2, &text, error);
        if (ok && qac_q1(context->dialect) && command->argc > 2) ok = qac_text_add(&text, " ", 1, error);
        if (ok) ok = qac_text_add(&text, "\n", 1, error);
        if (ok && text.size >= 1024) ok = qac_fail(error, QA_ERROR_FORMAT, "alias body exceeds source buffer");
        if (ok) ok = qa_console_alias(console, context, command->argv[1], text.data, error);
        free(text.data);
        return ok;
    }
    if (qac_equal(name, "vstr")) {
        if (command->argc != 2) { output(console, context, "vstr <variable>\n"); return true; }
        const qa_cvar_view *variable = qa_cvars_find(cvar_owner(console, context, command->argv[1]), command->argv[1]);
        qac_text text = {0};
        bool ok = qac_text_string(&text, variable == NULL ? "" : variable->value, error) && qac_text_add(&text, "\n", 1, error);
        if (ok) ok = qa_console_insert(console, context, text.data, error);
        free(text.data);
        return ok;
    }
    if (qac_equal(name, "resetall")) return reset_all(console, context, error);
    if (qac_equal(name, "cvar_restart") && context->dialect == QA_CONSOLE_Q3) {
        for (size_t i = 0;; ++i) {
            qa_cvars *registry = visible_cvars(console, context, i);
            if (registry == NULL) break;
            if (qa_cvars_dialect(registry) == QA_CONSOLE_Q3 && !qa_cvars_restart(registry, error)) return false;
        }
        return true;
    }
    if (qac_equal(name, "cmdlist")) {
        const char *pattern = context->dialect == QA_CONSOLE_Q3 && command->argc > 1 ? command->argv[1] : NULL;
        size_t count = 0;
        for (size_t i = 0; i < sizeof(builtin_entries) / sizeof(builtin_entries[0]); ++i) {
            const char *entry_name = builtin_entries[i].name;
            if (!builtin_allowed(context->dialect, entry_name) ||
                (pattern != NULL && !qa_command_filter(pattern, entry_name, false))) continue;
            output(console, context, entry_name); output(console, context, "\n");
            ++count;
        }
        for (command_entry *entry = console->commands; entry != NULL; entry = entry->next) {
            if (entry->view.owner != 0 && entry->view.owner != context->owner) continue;
            if (pattern != NULL && !qa_command_filter(pattern, entry->view.name, false)) continue;
            output(console, context, entry->view.name); output(console, context, "\n");
            ++count;
        }
        char summary[64];
        (void)snprintf(summary, sizeof(summary), "%zu commands\n", count);
        output(console, context, summary);
        return true;
    }
    if (qac_equal(name, "cvarlist")) {
        const char *pattern = context->dialect == QA_CONSOLE_Q3 && command->argc > 1 ? command->argv[1] : NULL;
        size_t count = 0;
        size_t handles = 0;
        for (size_t i = 0;; ++i) {
            qa_cvars *registry = visible_cvars(console, context, i);
            if (registry == NULL) break;
            handles += qa_cvars_handle_count(registry);
            for (size_t n = 0; n < qa_cvars_count(registry); ++n) {
                const qa_cvar_view *variable = qa_cvars_at(registry, n);
                if (cvar_owner(console, context, variable->name) == registry &&
                    (pattern == NULL || qa_command_filter(pattern, variable->name, false))) {
                    char markers[9];
                    if (context->dialect == QA_CONSOLE_Q3) {
                        const uint32_t flags[] = {QA_CVAR_SERVERINFO, QA_CVAR_USERINFO, QA_CVAR_READONLY,
                            QA_CVAR_INIT, QA_CVAR_ARCHIVE, QA_CVAR_LATCH, QA_CVAR_CHEAT};
                        const char symbols[] = "SURIALC";
                        for (size_t f = 0; f < 7; ++f) markers[f] = (variable->flags & flags[f]) != 0 ? symbols[f] : ' ';
                        markers[7] = ' ';
                        markers[8] = '\0';
                    } else {
                        markers[0] = (variable->flags & QA_CVAR_ARCHIVE) != 0 ? '*' : ' ';
                        markers[1] = (variable->flags & QA_CVAR_USERINFO) != 0 ? 'U' : ' ';
                        markers[2] = (variable->flags & QA_CVAR_SERVERINFO) != 0 ? 'S' : ' ';
                        markers[3] = (variable->flags & QA_Q2_CVAR_NOSET) != 0 ? '-' :
                            (variable->flags & QA_Q2_CVAR_LATCH) != 0 ? 'L' : ' ';
                        markers[4] = ' ';
                        markers[5] = '\0';
                    }
                    output(console, context, markers);
                    output_value(console, context, variable->name, variable->value);
                    ++count;
                }
            }
        }
        char summary[64];
        (void)snprintf(summary, sizeof(summary), "%zu cvars\n", count);
        output(console, context, summary);
        if (context->dialect == QA_CONSOLE_Q3) {
            (void)snprintf(summary, sizeof(summary), "%zu cvar indexes\n", handles);
            output(console, context, summary);
        }
        return true;
    }
    bool set = qac_equal(name, "set");
    bool flagged = qac_equal(name, "seta") || qac_equal(name, "setu") || qac_equal(name, "sets");
    bool reset = qac_equal(name, "reset");
    bool toggle = qac_equal(name, "toggle");
    bool increment = qac_equal(name, "inc") || qac_equal(name, "dec");
    if (!set && !flagged && !reset && !toggle && !increment) { *handled = false; return true; }
    if (command->argc < 2) { output(console, context, "command requires a variable name\n"); return true; }
    const char *variable_name = command->argv[1];
    qa_cvars *registry = cvar_owner(console, context, variable_name);
    if (registry == NULL) return qac_fail(error, QA_ERROR_NOT_FOUND, "command has no cvar owner");
    if (set || flagged) {
        if (command->argc < 3 || (set && qac_q2(context->dialect) && command->argc > 4) ||
            (flagged && context->dialect == QA_CONSOLE_Q3 && command->argc != 3)) {
            output(console, context, "set <variable> <value>\n"); return true;
        }
        if (set && qac_q2(context->dialect) && command->argc == 4) {
            if (strcmp(command->argv[3], "u") != 0 && strcmp(command->argv[3], "s") != 0) {
                output(console, context, "flags can only be 'u' or 's'\n"); return true;
            }
            return qa_cvars_full_set(registry, variable_name, command->argv[2],
                strcmp(command->argv[3], "u") == 0 ? QA_CVAR_USERINFO : QA_CVAR_SERVERINFO, error);
        }
        qac_text value = {0};
        bool ok = context->dialect == QA_CONSOLE_Q3 || flagged ? join_arguments(command, 2, &value, error) :
            qac_text_string(&value, command->argv[2], error);
        if (ok) {
            if (flagged) {
                uint32_t flag = qac_equal(name, "seta") ? QA_CVAR_ARCHIVE : qac_equal(name, "setu") ? QA_CVAR_USERINFO : QA_CVAR_SERVERINFO;
                ok = qa_cvars_set_flags(registry, variable_name, value.data, flag, error);
            } else ok = qa_cvars_set(registry, variable_name, value.data, false, error);
        }
        free(value.data);
        return ok;
    }
    const qa_cvar_view *variable = qa_cvars_find(registry, variable_name);
    if (toggle && context->dialect == QA_CONSOLE_Q3 && command->argc == 2) {
        float value = variable == NULL ? 0 : variable->number;
        return qa_cvars_set(registry, variable_name, truncf(value) == 0 ? "1" : "0", false, error);
    }
    if (variable == NULL) return qac_fail(error, QA_ERROR_NOT_FOUND, "cvar is not registered");
    if (reset) {
        if (context->dialect == QA_CONSOLE_Q3 && command->argc != 2) { output(console, context, "reset <variable>\n"); return true; }
        return qa_cvars_set_console(registry, variable_name, variable->reset_value, error);
    }
    if (toggle) {
        if (command->argc == 2) {
            if (strcmp(variable->value, "0") != 0 && strcmp(variable->value, "1") != 0) {
                output(console, context, "toggle requires 0/1 or an explicit value list\n"); return true;
            }
            return qa_cvars_set_console(registry, variable_name, strcmp(variable->value, "0") == 0 ? "1" : "0", error);
        }
        for (size_t i = 2; i < command->argc; ++i)
            if (qac_equal(command->argv[i], variable->value))
                return qa_cvars_set_console(registry, variable_name, command->argv[i + 1 < command->argc ? i + 1 : 2], error);
        output(console, context, "current value is outside toggle cycle\n");
        return true;
    }
    if (!decimal_text(variable->value)) { output(console, context, "increment requires a decimal cvar\n"); return true; }
    float amount = command->argc > 2 ? qac_number(command->argv[2], QA_CONSOLE_Q2) : 1;
    float value = variable->number + (qac_equal(name, "dec") ? -amount : amount);
    if (value == variable->number) return qa_cvars_set_console(registry, variable_name, variable->value, error);
    char formatted[64];
    if (!isfinite(value)) (void)snprintf(formatted, sizeof(formatted), "%s", isnan(value) ? "nan" : value < 0 ? "-inf" : "inf");
    else if (value - floorf(value) < 0.000001f) (void)snprintf(formatted, sizeof(formatted), "%.0f", (double)value);
    else (void)snprintf(formatted, sizeof(formatted), "%f", (double)value);
    formatted[31] = '\0';
    return qa_cvars_set_console(registry, variable_name, formatted, error);
}

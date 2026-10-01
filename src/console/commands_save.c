#include "commands_private.h"
#include "save_fields.h"
#include "qa/console_save.h"
#include <stdlib.h>
#include <string.h>

static bool invalid(qa_error *error, const char *text)
{ return qac_fail(error, QA_ERROR_FORMAT, text); }

static bool remap_identity(const qa_console_save_resolvers *resolve,
    qa_console_save_identity kind, uint64_t saved, uint64_t *out, qa_error *error)
{
    if (!saved) { *out = 0; return true; }
    uint64_t restored = 0;
    if (!resolve || !resolve->identity ||
        !resolve->identity(resolve->context, kind, saved, &restored, error) || !restored)
        return invalid(error, "Console lifetime identity has no restored owner");
    *out = restored; return true;
}

static bool identity(qa_source_save_io *io, const qa_console_save_resolvers *resolve,
                     qa_console_save_identity kind, uint64_t *value)
{
    if (!qa_source_save_u64(io, value)) return false;
    return io->direction != QA_SOURCE_SAVE_READ || remap_identity(resolve, kind, *value, value, io->error);
}

static bool context_fields(qa_source_save_io *io, qa_command_context *context,
                            const qa_console_save_resolvers *resolve, uint64_t captured_registry)
{
    uint32_t dialect = context->dialect, origin = context->origin;
    if (!qa_source_save_u64(io, &context->session) ||
        !qa_source_save_u64(io, &context->owner) || !qa_source_save_u64(io, &context->client) ||
        !qa_source_save_u32(io, &context->seat) || !qa_source_save_u32(io, &dialect) ||
        !qac_dialect_valid((qa_console_dialect)dialect) ||
        !qa_source_save_u32(io, &origin) || origin > QA_COMMAND_REMOTE ||
        !qa_source_save_bool(io, &context->direct) || !qa_source_save_bool(io, &context->console_text) ||
        !qac_save_text(io, &context->script) || !qa_source_save_u64(io, &context->registry) ||
        !qa_source_save_u64(io, &context->generation) || !qa_source_save_actor(io, &context->actor)) return false;
    context->dialect = (qa_console_dialect)dialect; context->origin = (qa_command_origin)origin;
    if (io->direction != QA_SOURCE_SAVE_READ) return true;
    /* Empty caller/wait sentinels have no publication or lifetime owner. */
    if (!context->session && !context->owner && !context->client && !context->seat && !dialect && !origin &&
        !context->direct && !context->console_text && !context->script &&
        !context->registry && !context->generation && !context->actor.registry) return true;
    if (!resolve || !resolve->command_context)
        return invalid(io->error, "Console context has no restored publication descriptor");
    qa_command_context saved = *context, restored = saved;
    if (!resolve->command_context(resolve->context, captured_registry, &saved, &restored, io->error)) return false;
    uint64_t owner = 0, client = 0;
    if (!remap_identity(resolve, QA_CONSOLE_SAVE_OWNER, saved.owner, &owner, io->error) ||
        !remap_identity(resolve, QA_CONSOLE_SAVE_CLIENT, saved.client, &client, io->error)) return false;
    if (restored.dialect != saved.dialect || restored.origin != saved.origin || restored.seat != saved.seat ||
        restored.direct != saved.direct || restored.console_text != saved.console_text ||
        !qa_actor_id_equal(restored.actor, saved.actor) || restored.script != saved.script ||
        restored.owner != owner || restored.client != client)
        return invalid(io->error, "Console context resolver changed command semantics");
    *context = restored; return true;
}

static uint32_t capabilities(const qa_console_options *o)
{
    return (o->cvars ? 1u : 0u) | (o->print ? 2u : 0u) | (o->cvar_owner ? 4u : 0u) |
        (o->visible_cvars ? 8u : 0u) | (o->read_script ? 16u : 0u) | (o->release_script ? 32u : 0u) |
        (o->script_complete ? 64u : 0u) | (o->allow_command ? 128u : 0u) |
        (o->source_command ? 256u : 0u) | (o->client_game ? 512u : 0u) | (o->server_game ? 1024u : 0u) |
        (o->ui ? 2048u : 0u) | (o->forward ? 4096u : 0u) |
        (o->capture_context ? 8192u : 0u) | (o->context_active ? 16384u : 0u) |
        (o->cvar_edit ? 32768u : 0u);
}

static bool retired_fields(qa_source_save_io *io, retired_id **head,
                           const qa_console_save_resolvers *resolve, qa_console_save_identity kind)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = 0;
    if (!reading) for (retired_id *entry = *head; entry; entry = entry->next) ++count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(retired_id))) return false;
    if (reading && count > io->input.size - io->offset) return invalid(io->error, "Invalid retired console extent");
    retired_id **tail = head;
    for (size_t i = 0; i < count; ++i) {
        if (reading) {
            *tail = calloc(1, sizeof(**tail));
            if (!*tail) return qac_fail(io->error, QA_ERROR_MEMORY, "Retaining retired console identity");
        }
        if (!identity(io, resolve, kind, &(*tail)->value) || !(*tail)->value) return false;
        for (retired_id *prior = *head; prior != *tail; prior = prior->next)
            if (prior->value == (*tail)->value) return invalid(io->error, "Duplicate retired console identity");
        tail = &(*tail)->next;
    }
    return true;
}

static command_entry *find_command(qa_console *console, uint64_t owner, const char *name)
{
    for (command_entry *entry = console->commands; entry; entry = entry->next)
        if (entry->view.owner == owner && !strcmp(entry->view.name, name)) return entry;
    return NULL;
}

static bool commands_fields(qa_source_save_io *io, qa_console *state, qa_console *candidate,
                             const qa_console_save_resolvers *resolve)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = 0;
    if (!reading) for (command_entry *entry = state->commands; entry; entry = entry->next) ++count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(command_entry))) return false;
    if (reading && count > io->input.size - io->offset) return invalid(io->error, "Invalid command registration extent");
    command_entry **tail = &state->commands;
    for (size_t i = 0; i < count; ++i) {
        if (reading) {
            *tail = calloc(1, sizeof(**tail));
            if (!*tail) return qac_fail(io->error, QA_ERROR_MEMORY, "Retaining restored command registration");
        }
        command_entry *entry = *tail;
        bool handler = entry->handler != NULL;
        if (!qac_save_text(io, &entry->view.name) || !entry->view.name || !*entry->view.name ||
            strpbrk(entry->view.name, " \t\r\n;\"") ||
            !qac_save_text(io, &entry->view.description) || !entry->view.description ||
            !qac_save_documentation(io, &entry->view.documentation) ||
            !identity(io, resolve, QA_CONSOLE_SAVE_OWNER, &entry->view.owner) ||
            !qa_source_save_bool(io, &entry->view.engine_command) ||
            !identity(io, resolve, QA_CONSOLE_SAVE_OWNER, &entry->registration_owner) ||
            !qa_source_save_bool(io, &entry->ordinary_registration) || !qa_source_save_bool(io, &handler)) return false;
        for (command_entry *prior = state->commands; prior != entry; prior = prior->next)
            if (prior->view.owner == entry->view.owner && !strcmp(prior->view.name, entry->view.name))
                return invalid(io->error, "Duplicate restored command registration");
        size_t contributors = 0;
        if (!reading) for (command_contribution *p = entry->contributions; p; p = p->next) ++contributors;
        if (!qa_source_save_count(io, &contributors, SIZE_MAX / sizeof(command_contribution))) return false;
        if (reading && contributors > io->input.size - io->offset) return invalid(io->error, "Invalid command contribution extent");
        command_contribution **link = &entry->contributions;
        for (size_t j = 0; j < contributors; ++j) {
            if (reading) {
                *link = calloc(1, sizeof(**link));
                if (!*link) return qac_fail(io->error, QA_ERROR_MEMORY, "Retaining restored command contribution");
            }
            if (!identity(io, resolve, QA_CONSOLE_SAVE_OWNER, &(*link)->owner) || !(*link)->owner) return false;
            for (command_contribution *prior = entry->contributions; prior != *link; prior = prior->next)
                if (prior->owner == (*link)->owner) return invalid(io->error, "Duplicate command contribution owner");
            link = &(*link)->next;
        }
        if ((!entry->ordinary_registration && (handler || entry->view.engine_command || !contributors)) ||
            (handler && !entry->ordinary_registration)) return invalid(io->error, "Invalid command callback lifetime");
        if (reading) {
            command_entry *actual = find_command(candidate, entry->view.owner, entry->view.name);
            if (handler && (!actual || !actual->handler || !actual->ordinary_registration ||
                actual->registration_owner != entry->registration_owner || actual->view.engine_command != entry->view.engine_command))
                return invalid(io->error, "Saved command callback has no matching candidate source registration");
            if (!handler && actual && actual->handler)
                return invalid(io->error, "Saved command differs from an installed candidate callback");
            if (handler) { entry->handler = actual->handler; entry->user = actual->user; }
        }
        tail = &entry->next;
    }
    if (reading) for (command_entry *actual = candidate->commands; actual; actual = actual->next)
        if (actual->handler && !find_command(state, actual->view.owner, actual->view.name))
            return invalid(io->error, "Console restore would discard an installed candidate callback");
    return true;
}

static bool aliases_fields(qa_source_save_io *io, qa_console *state,
                            const qa_console_save_resolvers *resolve)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = 0;
    if (!reading) for (alias_entry *entry = state->aliases; entry; entry = entry->next) ++count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(alias_entry))) return false;
    if (reading && count > io->input.size - io->offset) return invalid(io->error, "Invalid console alias extent");
    alias_entry **tail = &state->aliases;
    for (size_t i = 0; i < count; ++i) {
        if (reading) {
            *tail = calloc(1, sizeof(**tail));
            if (!*tail) return qac_fail(io->error, QA_ERROR_MEMORY, "Retaining restored alias");
        }
        alias_entry *entry = *tail;
        uint32_t dialect = entry->dialect;
        if (!qac_save_text(io, &entry->view.name) || !entry->view.name || !*entry->view.name || strlen(entry->view.name) >= 32 ||
            !qac_save_text(io, &entry->view.alias_text) || !entry->view.alias_text ||
            !identity(io, resolve, QA_CONSOLE_SAVE_OWNER, &entry->view.owner) ||
            !qa_source_save_u32(io, &dialect) || !qac_dialect_valid((qa_console_dialect)dialect) || dialect == QA_CONSOLE_Q3 ||
            !qa_source_save_bool(io, &entry->console_text)) return false;
        entry->dialect = (qa_console_dialect)dialect;
        for (alias_entry *prior = state->aliases; prior != entry; prior = prior->next)
            if (prior->view.owner == entry->view.owner && !strcmp(prior->view.name, entry->view.name))
                return invalid(io->error, "Duplicate restored alias");
        tail = &entry->next;
    }
    return true;
}

static bool chunks_fields(qa_source_save_io *io, command_chunk **head, command_chunk **last,
                          size_t *bytes, const qa_console_save_resolvers *resolve, uint64_t captured_registry)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = 0, total = 0;
    if (!reading) for (command_chunk *entry = *head; entry; entry = entry->next) ++count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(command_chunk))) return false;
    if (reading && count > io->input.size - io->offset) return invalid(io->error, "Invalid console queue extent");
    command_chunk **tail = head;
    for (size_t i = 0; i < count; ++i) {
        if (reading) {
            *tail = calloc(1, sizeof(**tail));
            if (!*tail) return qac_fail(io->error, QA_ERROR_MEMORY, "Retaining restored command chunk");
        }
        command_chunk *entry = *tail;
        bool text = entry->text != NULL;
        bool ok = context_fields(io, &entry->context, resolve, captured_registry) &&
            context_fields(io, &entry->caller, resolve, captured_registry) &&
            qa_source_save_bool(io, &text);
        if (!ok || !qa_source_save_count(io, &entry->offset, SIZE_MAX) ||
            !qa_source_save_count(io, &entry->length, SIZE_MAX - 1) ||
            !qa_source_save_bool(io, &entry->completion) || !qa_source_save_bool(io, &entry->success) ||
            entry->offset > entry->length ||
            (entry->completion ? text || entry->offset || entry->length || !entry->context.script : !text))
            return invalid(io->error, "Invalid retained command chunk");
        if (text) {
            if (reading) {
                if (io->offset > io->input.size || entry->length > io->input.size - io->offset)
                    return invalid(io->error, "Truncated retained command bytes");
                entry->text = malloc(entry->length + 1);
                if (!entry->text) return qac_fail(io->error, QA_ERROR_MEMORY, "Retaining command bytes");
                entry->text[entry->length] = 0;
            }
            if (!qa_source_save_bytes(io, entry->text, entry->length)) return false;
        }
        size_t pending = entry->length - entry->offset;
        if (pending > SIZE_MAX - total) return invalid(io->error, "Retained console queue overflow");
        total += pending; *last = entry; tail = &entry->next;
    }
    if (!reading && total != *bytes) return invalid(io->error, "Console queue byte ownership differs");
    *bytes = total; return true;
}

static bool is_retired(const retired_id *head, uint64_t value)
{
    for (; head; head = head->next) if (head->value == value) return true;
    return false;
}

static bool queue_valid(const qa_console *state, const command_chunk *head, size_t total, qa_error *error)
{
    qa_command_context largest = state->options.context;
    largest.dialect = QA_CONSOLE_Q3;
    size_t maximum_command = 0, maximum_buffer = 0;
    if (!qa_console_limits((qa_console *)state, &largest, &maximum_command, &maximum_buffer, error) || total > maximum_buffer)
        return invalid(error, "Retained console queue exceeds source buffer limits");
    for (const command_chunk *chunk = head; chunk; chunk = chunk->next) {
        const qa_command_context *context = &chunk->context;
        if (context->session != state->options.context.session ||
            (chunk->completion && chunk->caller.session != state->options.context.session) ||
            is_retired(state->owners, context->owner) || (context->client && is_retired(state->clients, context->client)))
            return invalid(error, "Console queue has an incompatible lifetime");
    }
    return true;
}

static bool state_valid(const qa_console *state, qa_error *error)
{
    for (const command_entry *entry = state->commands; entry; entry = entry->next) {
        if (is_retired(state->owners, entry->view.owner) ||
            (entry->ordinary_registration && is_retired(state->owners, entry->registration_owner)))
            return invalid(error, "Console command registration names a retired owner");
        for (const command_contribution *p = entry->contributions; p; p = p->next)
            if (is_retired(state->owners, p->owner)) return invalid(error, "Command contribution owner retired");
    }
    for (const alias_entry *entry = state->aliases; entry; entry = entry->next)
        if (is_retired(state->owners, entry->view.owner)) return invalid(error, "Console alias owner retired");
    if (state->wait && (state->wait_context.session != state->options.context.session ||
        is_retired(state->owners, state->wait_context.owner) ||
        (state->wait_context.client && is_retired(state->clients, state->wait_context.client))))
        return invalid(error, "Console wait names a retired context");
    return queue_valid(state, state->head, state->queued_bytes, error) &&
        queue_valid(state, state->deferred, state->deferred_bytes, error);
}

static bool fields(qa_source_save_io *io, qa_console *state, qa_console *candidate,
                     const qa_console_save_resolvers *resolve)
{
    char magic[8] = {'Q','A','C','O','N','S','L',0};
    uint32_t version = 1, callbacks = capabilities(&state->options);
    uint32_t expected = capabilities(&candidate->options);
    uint64_t captured_registry = qa_actors_identity(qa_session_actors(io->session));
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QACONSL", 8) ||
        !qa_source_save_u32(io, &version) || version != 1 ||
        !qa_source_save_u64(io, &captured_registry) || !captured_registry ||
        !qa_source_save_u32(io, &callbacks) || callbacks != expected ||
        !context_fields(io, &state->options.context, resolve, captured_registry) ||
        state->options.context.session != candidate->options.context.session ||
        state->options.context.dialect != candidate->options.context.dialect ||
        !qac_save_text(io, &state->options.startup_commands) ||
        !qa_source_save_count(io, &state->options.maximum_buffer, SIZE_MAX) ||
        !qa_source_save_count(io, &state->options.maximum_command, SIZE_MAX) || state->options.maximum_command == 1 ||
        !qa_source_save_bool(io, &state->options.disable_builtins) ||
        !qa_source_save_i32(io, &state->wait) || !context_fields(io, &state->wait_context, resolve, captured_registry) ||
        !qa_source_save_count(io, &state->alias_count, SIZE_MAX) ||
        !retired_fields(io, &state->owners, resolve, QA_CONSOLE_SAVE_OWNER) ||
        !retired_fields(io, &state->clients, resolve, QA_CONSOLE_SAVE_CLIENT) ||
        !commands_fields(io, state, candidate, resolve) || !aliases_fields(io, state, resolve) ||
        !chunks_fields(io, &state->head, &state->tail, &state->queued_bytes, resolve, captured_registry) ||
        !chunks_fields(io, &state->deferred, &state->deferred_tail, &state->deferred_bytes, resolve, captured_registry)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) state->startup = (char *)state->options.startup_commands;
    return state_valid(state, io->error);
}

bool qa_console_save_capture(const qa_console *console, qa_session *session, qa_buffer *out, qa_error *error)
{
    if (!console || !session || !out || !qa_console_idle(console) || console->program_leases || console->release_leases ||
        console->program_unpublished)
        return qac_fail(error, QA_ERROR_ARGUMENT, "Console capture requires its idle owner and session");
    qa_source_save_io io;
    if (!qa_source_save_writer(&io, session, error)) return false;
    qa_console copy = *console;
    bool ok = fields(&io, &copy, (qa_console *)console, NULL) && qa_source_save_finish(&io, out);
    if (!ok && (!error || error->code == QA_OK)) invalid(error, "Invalid console continuation");
    qa_source_save_dispose(&io); return ok;
}

bool qa_console_save_restore(qa_console *console, qa_session *session,
    const qa_console_save_resolvers *resolve, qa_bytes bytes, qa_error *error)
{
    if (!console || !session || !resolve || !qa_console_idle(console) || console->program_leases || console->release_leases ||
        console->program_unpublished)
        return qac_fail(error, QA_ERROR_ARGUMENT, "Console restore requires an idle candidate owner");
    qa_buffer before = {0}, after = {0};
    if (!qa_console_save_capture(console, session, &before, error)) return false;
    qa_console *scratch = calloc(1, sizeof(*scratch));
    if (!scratch) { qa_buffer_free(&before); return qac_fail(error, QA_ERROR_MEMORY, "Allocating console restore candidate"); }
    scratch->options = console->options;
    scratch->options.context = (qa_command_context){0};
    scratch->options.startup_commands = NULL;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, session, bytes, error) && fields(&io, scratch, console, resolve) &&
        qa_source_save_finish(&io, NULL) && qa_console_idle(console) &&
        qa_console_save_capture(console, session, &after, error);
    /* qac_save_text's allocation is owned even if a later header field fails. */
    scratch->startup = (char *)scratch->options.startup_commands;
    if (ok && (before.size != after.size || memcmp(before.data, after.data, before.size)))
        ok = invalid(error, "Candidate console changed during descriptor resolution");
    if (ok) {
        qa_console saved = *console;
        *console = *scratch;
        *scratch = saved;
    }
    qa_console_destroy(scratch);
    qa_source_save_dispose(&io); qa_buffer_free(&before); qa_buffer_free(&after);
    if (!ok && (!error || error->code == QA_OK)) invalid(error, "Invalid saved console continuation");
    return ok;
}

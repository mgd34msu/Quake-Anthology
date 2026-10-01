#include "commands_private.h"
#include "qa/console_program.h"
#include <stdlib.h>
#include <string.h>

typedef struct program_state {
    command_chunk *head, *tail, *deferred, *deferred_tail;
    size_t queued_bytes, deferred_bytes, alias_count;
    int32_t wait;
    qa_command_context wait_context, context;
    qa_command_context wait_source;
    alias_entry *aliases;
    retired_id *owners, *clients;
    char *startup;
    qa_cvars *cvars;
    size_t maximum_buffer, maximum_command;
    bool disable_builtins;
    uint64_t revision;
    bool revision_exhausted;
} program_state;

struct qa_console_program {
    qa_console *source, *candidate;
    qa_console_program_resolvers resolve;
    program_state original, prepared, publication, prefix;
    bool ready, sealed, busy;
};

void qac_console_program_touch(qa_console *console, bool shared_namespace)
{
    if (!console || (!shared_namespace && console->release_owner &&
        console->release_leases && console->release_advancing)) return;
    if (console->program_revision == UINT64_MAX) console->program_revision_exhausted = true;
    else ++console->program_revision;
}

bool qac_console_program_drain_allowed(const qa_console *console, qa_error *error)
{
    return !console || !console->program_unpublished ||
        qac_fail(error, QA_ERROR_ARGUMENT, "prepared command program awaits actual publication");
}

bool qac_console_program_immediate_allowed(const qa_console *console, qa_error *error)
{
    return !console || !console->program_aborted ||
        qac_fail(error, QA_ERROR_ARGUMENT, "candidate command program was aborted");
}

static bool text_equal(const char *a, const char *b)
{ return a && b ? !strcmp(a, b) : a == b; }

static bool context_equal(const qa_command_context *a, const qa_command_context *b)
{
    return a->session == b->session && a->owner == b->owner && a->client == b->client &&
        a->seat == b->seat && a->dialect == b->dialect && a->origin == b->origin &&
        a->direct == b->direct && a->console_text == b->console_text &&
        text_equal(a->script, b->script) && a->registry == b->registry &&
        a->generation == b->generation && a->actor.registry == b->actor.registry &&
        a->actor.generation == b->actor.generation && a->actor.slot == b->actor.slot;
}

static bool context_copy(qa_command_context *out, const qa_command_context *source, qa_error *error)
{
    *out = *source; out->script = NULL;
    if (source->script) {
        out->script = qac_copy(source->script, error);
        if (!out->script) return false;
    }
    return true;
}

static void chunks_free(command_chunk *head)
{
    while (head) {
        command_chunk *next = head->next;
        free((char *)head->context.script); free((char *)head->caller.script);
        free(head->text); free(head); head = next;
    }
}

static void aliases_free(alias_entry *head)
{
    while (head) {
        alias_entry *next = head->next;
        free((char *)head->view.name); free((char *)head->view.alias_text);
        free(head); head = next;
    }
}

static void ids_free(retired_id *head)
{
    while (head) { retired_id *next = head->next; free(head); head = next; }
}

static void state_free(program_state *state)
{
    chunks_free(state->head); chunks_free(state->deferred);
    aliases_free(state->aliases); ids_free(state->owners); ids_free(state->clients);
    free((char *)state->context.script); free((char *)state->wait_context.script);
    free(state->startup); *state = (program_state){0};
}

static bool chunks_copy(const command_chunk *source, command_chunk **out,
    command_chunk **last, size_t expected_bytes, qa_error *error)
{
    command_chunk **link = out; size_t bytes = 0;
    for (; source; source = source->next) {
        if (source->offset > source->length ||
            (source->completion ? source->text || source->length || source->offset || !source->context.script : !source->text) ||
            source->length == SIZE_MAX || source->length - source->offset > SIZE_MAX - bytes)
            return qac_fail(error, QA_ERROR_ARGUMENT, "command program has invalid retained chunks");
        command_chunk *copy = calloc(1, sizeof(*copy));
        if (!copy) return qac_fail(error, QA_ERROR_MEMORY, "retaining command program chunks");
        *link = copy; *last = copy;
        copy->offset = source->offset; copy->length = source->length;
        copy->completion = source->completion; copy->success = source->success;
        if (!context_copy(&copy->context, &source->context, error) ||
            !context_copy(&copy->caller, &source->caller, error)) return false;
        copy->program_pending = source->program_pending;
        copy->program_source = source->program_source; copy->program_source.script = copy->context.script;
        copy->program_caller = source->program_caller; copy->program_caller.script = copy->caller.script;
        if (source->text) {
            copy->text = malloc(source->length + 1);
            if (!copy->text) return qac_fail(error, QA_ERROR_MEMORY, "retaining command program bytes");
            memcpy(copy->text, source->text, source->length); copy->text[source->length] = 0;
        }
        bytes += source->length - source->offset; link = &copy->next;
    }
    return bytes == expected_bytes || qac_fail(error, QA_ERROR_ARGUMENT, "command program byte ownership differs");
}

static bool aliases_copy(const alias_entry *source, alias_entry **out, qa_error *error)
{
    alias_entry **link = out;
    for (; source; source = source->next) {
        alias_entry *copy = calloc(1, sizeof(*copy));
        if (!copy) return qac_fail(error, QA_ERROR_MEMORY, "retaining command program aliases");
        *link = copy; copy->view = source->view;
        copy->view.name = NULL; copy->view.alias_text = NULL;
        copy->dialect = source->dialect; copy->console_text = source->console_text;
        copy->view.name = qac_copy(source->view.name, error);
        copy->view.alias_text = qac_copy(source->view.alias_text, error);
        if (!copy->view.name || !copy->view.alias_text) return false;
        link = &copy->next;
    }
    return true;
}

static bool ids_copy(const retired_id *source, retired_id **out, qa_error *error)
{
    retired_id **link = out;
    for (; source; source = source->next) {
        *link = malloc(sizeof(**link));
        if (!*link) return qac_fail(error, QA_ERROR_MEMORY, "retaining command program lifetimes");
        **link = (retired_id){.value = source->value}; link = &(*link)->next;
    }
    return true;
}

static bool state_capture(const qa_console *console, program_state *state, qa_error *error)
{
    *state = (program_state){.queued_bytes = console->queued_bytes, .deferred_bytes = console->deferred_bytes,
        .alias_count = console->alias_count, .wait = console->wait, .cvars = console->options.cvars,
        .maximum_buffer = console->options.maximum_buffer, .maximum_command = console->options.maximum_command,
        .disable_builtins = console->options.disable_builtins,
        .revision = console->program_revision, .revision_exhausted = console->program_revision_exhausted};
    if (!context_copy(&state->context, &console->options.context, error) ||
        !context_copy(&state->wait_context, &console->wait_context, error) ||
        !chunks_copy(console->head, &state->head, &state->tail, state->queued_bytes, error) ||
        !chunks_copy(console->deferred, &state->deferred, &state->deferred_tail, state->deferred_bytes, error) ||
        !aliases_copy(console->aliases, &state->aliases, error) ||
        !ids_copy(console->owners, &state->owners, error) || !ids_copy(console->clients, &state->clients, error)) return false;
    if (console->startup && !(state->startup = qac_copy(console->startup, error))) return false;
    return true;
}

static bool chunks_equal(const command_chunk *a, const command_chunk *b)
{
    for (; a && b; a = a->next, b = b->next)
        if (!context_equal(&a->context, &b->context) || !context_equal(&a->caller, &b->caller) ||
            a->offset != b->offset || a->length != b->length || a->completion != b->completion ||
            a->success != b->success || (a->text == NULL) != (b->text == NULL) ||
            (a->text && memcmp(a->text, b->text, a->length))) return false;
    return !a && !b;
}

static bool aliases_equal(const alias_entry *a, const alias_entry *b)
{
    for (; a && b; a = a->next, b = b->next)
        if (a->view.owner != b->view.owner || !text_equal(a->view.name, b->view.name) ||
            !text_equal(a->view.alias_text, b->view.alias_text) || a->dialect != b->dialect ||
            a->console_text != b->console_text) return false;
    return !a && !b;
}

static bool ids_equal(const retired_id *a, const retired_id *b)
{
    for (; a && b; a = a->next, b = b->next) if (a->value != b->value) return false;
    return !a && !b;
}

static bool state_current(const program_state *state, const qa_console *console)
{
    return qa_console_idle(console) && !console->release_leases && !console->program_revision_exhausted &&
        !state->revision_exhausted && state->revision == console->program_revision &&
        context_equal(&state->context, &console->options.context) &&
        state->cvars == console->options.cvars && state->maximum_buffer == console->options.maximum_buffer &&
        state->maximum_command == console->options.maximum_command &&
        state->disable_builtins == console->options.disable_builtins &&
        state->queued_bytes == console->queued_bytes && state->deferred_bytes == console->deferred_bytes &&
        state->wait == console->wait && context_equal(&state->wait_context, &console->wait_context) &&
        state->alias_count == console->alias_count && text_equal(state->startup, console->startup) &&
        chunks_equal(state->head, console->head) && chunks_equal(state->deferred, console->deferred) &&
        aliases_equal(state->aliases, console->aliases) && ids_equal(state->owners, console->owners) &&
        ids_equal(state->clients, console->clients);
}

static bool is_retired(const retired_id *head, uint64_t value)
{ for (; head; head = head->next) if (head->value == value) return true; return false; }

static bool map_identity(qa_console_program *program, qa_console_program_identity kind,
    uint64_t source, uint64_t *out, qa_error *error)
{
    if (!source) { *out = 0; return true; }
    uint64_t mapped = 0;
    if (!program->resolve.identity(program->resolve.context, kind, source, &mapped, error)) return false;
    if (!mapped) return qac_fail(error, QA_ERROR_ARGUMENT, "command program lifetime has no candidate declaration");
    *out = mapped; return true;
}

static bool map_ids(qa_console_program *program, retired_id *head,
    qa_console_program_identity kind, qa_error *error)
{
    for (retired_id *entry = head; entry; entry = entry->next) {
        if (!entry->value) return qac_fail(error, QA_ERROR_ARGUMENT, "command program has an absent retired lifetime");
        if (!map_identity(program, kind, entry->value, &entry->value, error)) return false;
        for (retired_id *prior = head; prior != entry; prior = prior->next)
            if (prior->value == entry->value)
                return qac_fail(error, QA_ERROR_ARGUMENT, "command program retired lifetimes alias candidate identities");
    }
    return true;
}

static bool map_aliases(qa_console_program *program, alias_entry *head, const retired_id *owners, qa_error *error)
{
    for (alias_entry *entry = head; entry; entry = entry->next) {
        if (!map_identity(program, QA_CONSOLE_PROGRAM_OWNER, entry->view.owner, &entry->view.owner, error)) return false;
        if (is_retired(owners, entry->view.owner))
            return qac_fail(error, QA_ERROR_ARGUMENT, "command program alias names a retired candidate owner");
        for (alias_entry *prior = head; prior != entry; prior = prior->next)
            if (prior->view.owner == entry->view.owner && !strcmp(prior->view.name, entry->view.name))
                return qac_fail(error, QA_ERROR_ARGUMENT, "command program aliases lose their actual owner distinction");
    }
    return true;
}

static bool declarations_live(const qa_console *candidate, const retired_id *owners, qa_error *error)
{
    for (const command_entry *entry = candidate->commands; entry; entry = entry->next) {
        if (is_retired(owners, entry->view.owner) ||
            (entry->ordinary_registration && is_retired(owners, entry->registration_owner)))
            return qac_fail(error, QA_ERROR_ARGUMENT, "command program would retire a fresh candidate handler");
        for (const command_contribution *part = entry->contributions; part; part = part->next)
            if (is_retired(owners, part->owner))
                return qac_fail(error, QA_ERROR_ARGUMENT, "command program would retire a fresh candidate contribution");
    }
    return true;
}

static void program_free(qa_console_program *program)
{
    if (program->source) --program->source->program_leases;
    if (program->candidate->pending_program == program) program->candidate->pending_program = NULL;
    --program->candidate->program_leases;
    state_free(&program->original); state_free(&program->prepared);
    state_free(&program->publication); state_free(&program->prefix);
    free(program);
}

qa_console_program *qa_console_program_prepare(qa_console *source, qa_console *candidate,
    const qa_console_program_resolvers *resolve, qa_error *error)
{
    if (!source || !candidate || source == candidate || !resolve || !resolve->identity ||
        !resolve->command_context || !resolve->published_context || !resolve->published_candidate_context ||
        !resolve->current || !resolve->published ||
        !qa_console_idle(source) || !qa_console_idle(candidate) || source->program_leases || candidate->program_leases ||
        source->release_leases || candidate->release_leases ||
        source->program_revision_exhausted || candidate->program_revision_exhausted ||
        source->program_unpublished || candidate->program_unpublished ||
        candidate->head || candidate->deferred || candidate->wait || candidate->queued_bytes || candidate->deferred_bytes ||
        candidate->alias_count || !context_equal(&candidate->wait_context, &(qa_command_context){0}) ||
        candidate->aliases || candidate->owners || candidate->clients)
        { qac_fail(error, QA_ERROR_ARGUMENT, "command program preparation requires its idle source and fresh candidate"); return NULL; }
    qa_console_program *program = calloc(1, sizeof(*program));
    if (!program) { qac_fail(error, QA_ERROR_MEMORY, "retaining prepared command program"); return NULL; }
    program->source = source; program->candidate = candidate; program->resolve = *resolve; program->busy = true;
    ++source->program_leases; ++candidate->program_leases;
    program_state fresh = {0}, inherited = {0};
    bool ok = state_capture(source, &program->original, error) && state_capture(candidate, &fresh, error) &&
        resolve->current(resolve->context, source, candidate, error) &&
        aliases_copy(program->original.aliases, &inherited.aliases, error) &&
        ids_copy(program->original.owners, &inherited.owners, error) &&
        ids_copy(program->original.clients, &inherited.clients, error) &&
        map_ids(program, inherited.owners, QA_CONSOLE_PROGRAM_OWNER, error) &&
        map_ids(program, inherited.clients, QA_CONSOLE_PROGRAM_CLIENT, error) &&
        map_aliases(program, inherited.aliases, inherited.owners, error) &&
        declarations_live(candidate, inherited.owners, error);
    if (ok && (!state_current(&program->original, source) || !state_current(&fresh, candidate)))
        ok = qac_fail(error, QA_ERROR_ARGUMENT, "command owner changed during program preparation");
    if (ok) {
        candidate->aliases = inherited.aliases; inherited.aliases = NULL;
        candidate->owners = inherited.owners; inherited.owners = NULL;
        candidate->clients = inherited.clients; inherited.clients = NULL;
        qac_console_program_touch(candidate, true);
    }
    state_free(&fresh); state_free(&inherited); program->busy = false;
    if (!ok) { program_free(program); return NULL; }
    return program;
}

static bool empty_context(const qa_command_context *context)
{
    const qa_command_context empty = {0}; return context_equal(context, &empty);
}

static bool map_context(qa_console_program *program, qa_command_context *context, qa_error *error)
{
    if (empty_context(context)) return true;
    qa_command_context mapped = *context; uint64_t owner, client;
    if (!program->resolve.command_context(program->resolve.context, context, &mapped, error) ||
        !map_identity(program, QA_CONSOLE_PROGRAM_OWNER, context->owner, &owner, error) ||
        !map_identity(program, QA_CONSOLE_PROGRAM_CLIENT, context->client, &client, error)) return false;
    if (mapped.session != program->candidate->options.context.session || mapped.owner != owner || mapped.client != client ||
        mapped.dialect != context->dialect || mapped.origin != context->origin || mapped.seat != context->seat ||
        mapped.direct != context->direct || mapped.console_text != context->console_text ||
        !text_equal(mapped.script, context->script) ||
        (mapped.actor.registry == 0) != (context->actor.registry == 0))
        return qac_fail(error, QA_ERROR_ARGUMENT, "command program resolver changed retained command semantics");
    mapped.script = context->script; *context = mapped; return true;
}

static bool context_live(const qa_console *candidate, const qa_command_context *context, bool published, qa_error *error)
{
    if (context->session != candidate->options.context.session || !qac_dialect_valid(context->dialect) ||
        context->origin < QA_COMMAND_LOCAL || context->origin > QA_COMMAND_REMOTE ||
        is_retired(candidate->owners, context->owner) ||
        (context->client && is_retired(candidate->clients, context->client)) ||
        (published && candidate->options.context_active &&
         !candidate->options.context_active(candidate->options.user, context)))
        return qac_fail(error, QA_ERROR_ARGUMENT, "command program context has no live candidate owner");
    return true;
}

static bool tail_valid(const qa_console_program *program, const program_state *tail, bool published, qa_error *error)
{
    const qa_console *candidate = program->candidate;
    qa_command_context largest = candidate->options.context; largest.dialect = QA_CONSOLE_Q3;
    size_t maximum_command, maximum_buffer;
    if (!qa_console_limits((qa_console *)candidate, &largest, &maximum_command, &maximum_buffer, error)) return false;
    if (tail->queued_bytes > maximum_buffer || tail->deferred_bytes > maximum_buffer)
        return qac_fail(error, QA_ERROR_FORMAT, "retained command program exceeds candidate buffer limits");
    const command_chunk *lists[] = {tail->head, tail->deferred};
    for (size_t i = 0; i < 2; ++i)
        for (const command_chunk *chunk = lists[i]; chunk; chunk = chunk->next)
            if (!context_live(candidate, &chunk->context, published, error) ||
                (chunk->completion && !context_live(candidate, &chunk->caller, published, error))) return false;
    return !tail->wait || context_live(candidate, &tail->wait_context, published, error);
}

static bool tail_copy(const program_state *source, program_state *out, qa_error *error)
{
    *out = (program_state){.queued_bytes = source->queued_bytes, .deferred_bytes = source->deferred_bytes,
        .alias_count = source->alias_count, .wait = source->wait};
    if (!context_copy(&out->wait_context, &source->wait_context, error) ||
        !chunks_copy(source->head, &out->head, &out->tail, source->queued_bytes, error) ||
        !chunks_copy(source->deferred, &out->deferred, &out->deferred_tail, source->deferred_bytes, error)) return false;
    out->wait_source = source->wait_source; out->wait_source.script = out->wait_context.script;
    if (source->startup && !(out->startup = qac_copy(source->startup, error))) return false;
    return true;
}

static bool context_retired(const qa_console *candidate, const qa_command_context *context)
{
    return is_retired(candidate->owners, context->owner) ||
        (context->client && is_retired(candidate->clients, context->client));
}

static void filter_chunks(const qa_console *candidate, command_chunk **head,
    command_chunk **tail, size_t *bytes)
{
    command_chunk **link = head; *tail = NULL;
    while (*link) {
        command_chunk *chunk = *link;
        if (context_retired(candidate, &chunk->context)) {
            *link = chunk->next;
            *bytes -= chunk->length - chunk->offset;
            chunk->next = NULL; chunks_free(chunk);
        } else { *tail = chunk; link = &chunk->next; }
    }
}

static void filter_tail(const qa_console *candidate, program_state *tail)
{
    filter_chunks(candidate, &tail->head, &tail->tail, &tail->queued_bytes);
    filter_chunks(candidate, &tail->deferred, &tail->deferred_tail, &tail->deferred_bytes);
    if (context_retired(candidate, &tail->wait_context)) {
        free((char *)tail->wait_context.script);
        tail->wait_context = (qa_command_context){0}; tail->wait_source = (qa_command_context){0}; tail->wait = 0;
    }
}

bool qa_console_program_preflight(qa_console_program *program, qa_error *error)
{
    if (!program || program->busy || program->ready || !state_current(&program->original, program->source) ||
        !qa_console_idle(program->candidate) || qa_console_pending(program->candidate))
        return qac_fail(error, QA_ERROR_ARGUMENT, "command program preflight requires its unchanged source and completed cfg prefix");
    program->busy = true; program_state prefix = {0};
    bool ok = state_capture(program->candidate, &prefix, error) &&
        program->resolve.current(program->resolve.context, program->source, program->candidate, error) &&
        state_capture(program->source, &program->prepared, error);
    command_chunk *lists[] = {program->prepared.head, program->prepared.deferred};
    for (size_t i = 0; ok && i < 2; ++i)
        for (command_chunk *chunk = lists[i]; ok && chunk; chunk = chunk->next) {
            chunk->program_source = chunk->context; chunk->program_caller = chunk->caller;
            chunk->program_pending = true;
            ok = map_context(program, &chunk->context, error) && map_context(program, &chunk->caller, error);
        }
    if (ok) {
        program->prepared.wait_source = program->prepared.wait_context;
        ok = map_context(program, &program->prepared.wait_context, error);
    }
    if (ok) {
        filter_tail(program->candidate, &program->prepared);
        ok = tail_valid(program, &program->prepared, false, error) &&
            tail_copy(&program->prepared, &program->publication, error);
    }
    if (ok && (!state_current(&program->original, program->source) || !state_current(&prefix, program->candidate)))
        ok = qac_fail(error, QA_ERROR_ARGUMENT, "command owner changed during program preflight");
    if (ok) { program->prefix = prefix; prefix = (program_state){0}; program->ready = true; }
    else { state_free(&program->prepared); state_free(&program->publication); }
    state_free(&prefix); program->busy = false; return ok;
}

bool qa_console_program_ready(const qa_console_program *program, qa_error *error)
{
    return (program && !program->busy && program->ready &&
        (program->sealed || state_current(&program->original, program->source)) &&
        (program->sealed ? qa_console_idle(program->candidate) && !program->candidate->release_leases &&
            program->candidate->pending_program == program : state_current(&program->prefix, program->candidate))) ||
        qac_fail(error, QA_ERROR_ARGUMENT, "prepared command program changed before publication");
}

bool qa_console_program_seal(qa_console_program *program, qa_error *error)
{
    if (!qa_console_program_ready(program, error)) return false;
    if (program->sealed) return qac_fail(error, QA_ERROR_ARGUMENT, "command program is already sealed");
    program->busy = true;
    bool ok = program->resolve.current(program->resolve.context, program->source, program->candidate, error);
    program->busy = false;
    if (ok) ok = qa_console_program_ready(program, error);
    if (!ok) return false;
    qa_console *candidate = program->candidate; program_state *tail = &program->publication;
    candidate->head = tail->head; candidate->tail = tail->tail; tail->head = tail->tail = NULL;
    candidate->deferred = tail->deferred; candidate->deferred_tail = tail->deferred_tail;
    tail->deferred = tail->deferred_tail = NULL;
    candidate->queued_bytes = tail->queued_bytes; candidate->deferred_bytes = tail->deferred_bytes;
    candidate->wait = tail->wait; free((char *)candidate->wait_context.script);
    candidate->wait_context = tail->wait_context; tail->wait_context.script = NULL;
    candidate->program_wait_source = tail->wait_source;
    candidate->program_wait_source.script = candidate->wait_context.script;
    candidate->program_wait_pending = candidate->wait != 0;
    candidate->alias_count = tail->alias_count;
    free(candidate->startup); candidate->startup = tail->startup; tail->startup = NULL;
    candidate->options.startup_commands = candidate->startup;
    candidate->pending_program = program; candidate->program_unpublished = true;
    qac_console_program_touch(candidate, false);
    --program->source->program_leases; program->source = NULL; program->sealed = true;
    return true;
}

static bool publish_context(qa_console_program *program, const qa_command_context *source,
    const qa_command_context *prepared, qa_command_context *published, qa_error *error)
{
    if (empty_context(source)) { *published = *prepared; return true; }
    qa_command_context value = *prepared;
    if (!program->resolve.published_context(program->resolve.context, source, prepared, &value, error)) return false;
    if (value.session != prepared->session || value.owner != prepared->owner || value.client != prepared->client ||
        value.dialect != prepared->dialect || value.origin != prepared->origin || value.seat != prepared->seat ||
        value.direct != prepared->direct || value.console_text != prepared->console_text ||
        !text_equal(value.script, prepared->script) ||
        (value.actor.registry == 0) != (source->actor.registry == 0))
        return qac_fail(error, QA_ERROR_ARGUMENT, "published command context changed its qualified scope");
    value.script = published->script; *published = value; return true;
}

static bool publish_candidate_context(qa_console_program *program, const qa_command_context *captured,
    qa_command_context *published, qa_error *error)
{
    *published = *captured;
    if (empty_context(captured)) return true;
    if (!program->resolve.published_candidate_context(program->resolve.context, captured, published, error)) return false;
    if (published->session != captured->session || published->owner != captured->owner ||
        published->client != captured->client || published->seat != captured->seat ||
        published->dialect != captured->dialect || published->origin != captured->origin ||
        published->direct != captured->direct || published->console_text != captured->console_text ||
        !text_equal(published->script, captured->script))
        return qac_fail(error, QA_ERROR_ARGUMENT, "published candidate command changed its actual scope");
    published->script = captured->script; return true;
}

static bool publish_chunks(qa_console_program *program, command_chunk *head, qa_error *error)
{
    for (command_chunk *chunk = head; chunk; chunk = chunk->next) {
        chunk->program_bound = chunk->context; chunk->program_bound_caller = chunk->caller;
        bool ok = chunk->program_pending
            ? publish_context(program, &chunk->program_source, &chunk->context, &chunk->program_bound, error) &&
              publish_context(program, &chunk->program_caller, &chunk->caller, &chunk->program_bound_caller, error)
            : publish_candidate_context(program, &chunk->context, &chunk->program_bound, error) &&
              publish_candidate_context(program, &chunk->caller, &chunk->program_bound_caller, error);
        if (!ok || !context_live(program->candidate, &chunk->program_bound, true, error) ||
            (chunk->completion && !context_live(program->candidate, &chunk->program_bound_caller, true, error))) return false;
    }
    return true;
}

static void bind_chunks(command_chunk *head)
{
    for (; head; head = head->next) {
        head->context = head->program_bound; head->caller = head->program_bound_caller;
        head->program_pending = false;
        head->program_source = head->program_caller = head->program_bound = head->program_bound_caller = (qa_command_context){0};
    }
}

bool qa_console_program_adopt(qa_console_program *program, qa_error *error)
{
    if (!qa_console_program_ready(program, error)) return false;
    if (!program->sealed) return qac_fail(error, QA_ERROR_ARGUMENT, "command program adoption requires its committed seal");
    qa_console *candidate = program->candidate;
    if (candidate->program_revision_exhausted)
        return qac_fail(error, QA_ERROR_ARGUMENT, "candidate command program exhausted its mutation history");
    uint64_t revision = candidate->program_revision;
    program->busy = true;
    bool ok = program->resolve.published(program->resolve.context, &program->original.context, program->candidate, error) &&
        publish_chunks(program, candidate->head, error) && publish_chunks(program, candidate->deferred, error);
    candidate->program_wait_bound = candidate->wait_context;
    if (ok && candidate->wait) ok = candidate->program_wait_pending
        ? publish_context(program, &candidate->program_wait_source, &candidate->wait_context,
            &candidate->program_wait_bound, error)
        : publish_candidate_context(program, &candidate->wait_context, &candidate->program_wait_bound, error);
    if (ok && candidate->wait) ok = context_live(candidate, &candidate->program_wait_bound, true, error);
    if (ok && (!qa_console_idle(candidate) || candidate->program_revision_exhausted ||
        candidate->program_revision != revision || candidate->pending_program != program))
        ok = qac_fail(error, QA_ERROR_ARGUMENT, "command publication callback changed the actual candidate program");
    program->busy = false; if (!ok) return false;
    bind_chunks(candidate->head); bind_chunks(candidate->deferred);
    if (candidate->wait) candidate->wait_context = candidate->program_wait_bound;
    candidate->program_wait_pending = false;
    candidate->program_wait_source = candidate->program_wait_bound = (qa_command_context){0};
    candidate->program_unpublished = false;
    qac_console_program_touch(candidate, false);
    program_free(program); return true;
}

bool qa_console_program_abort(qa_console_program *program, qa_error *error)
{
    if (!program || program->busy || (program->source && !qa_console_idle(program->source)) || !qa_console_idle(program->candidate))
        return qac_fail(error, QA_ERROR_ARGUMENT, "command program abort requires both retained idle owners");
    if (program->sealed) program->candidate->program_aborted = true;
    program_free(program); return true;
}

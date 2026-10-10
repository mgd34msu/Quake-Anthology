#include "commands_private.h"
#include "save_fields.h"
#include "qa/console_save.h"
#include "qa/console_release.h"
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

static bool context_fields(qa_source_save_io *io, qa_command_context *context,
                            const qa_console_save_resolvers *resolve, uint64_t captured_registry)
{
    qa_command_context captured = *context;
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        context = &captured;
        context->registry = qa_console_save_context_registry(io->session, context->registry, captured_registry);
    }
    /* Runtime view receipts are reconstructed by the actual caller resolver. */
    context->cvar_view = 0;
    uint32_t dialect = context->dialect, origin = context->origin;
    if (!qa_source_save_u64(io, &context->session) ||
        !qa_source_save_u64(io, &context->owner) || !qa_source_save_u64(io, &context->client) ||
        !qa_source_save_u32(io, &context->seat) || !qa_source_save_u32(io, &dialect) ||
        !qac_dialect_valid((qa_ruleset_id)dialect) ||
        !qa_source_save_u32(io, &origin) || origin > QA_COMMAND_REMOTE ||
        !qa_source_save_bool(io, &context->direct) || !qa_source_save_bool(io, &context->console_text) ||
        !qac_save_text(io, &context->script) || !qa_source_save_u64(io, &context->registry) ||
        !qa_source_save_u64(io, &context->generation) || !qa_source_save_actor(io, &context->actor)) return false;
    context->dialect = (qa_ruleset_id)dialect; context->origin = (qa_command_origin)origin;
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
    largest.dialect = QA_RULESET_Q3;
    size_t maximum_command = 0, maximum_buffer = 0;
    if (!qa_console_limits((qa_console *)state, &largest, &maximum_command, &maximum_buffer, error) || total > maximum_buffer)
        return invalid(error, "Retained console queue exceeds source buffer limits");
    for (const command_chunk *chunk = head; chunk; chunk = chunk->next) {
        const qa_command_context *context = &chunk->context;
        if (context->session != state->options.context.session ||
            (chunk->completion && chunk->caller.session != state->options.context.session) ||
            is_retired(state->owners, context->owner) || (context->client && is_retired(state->clients, context->client)))
            return invalid(error, "Console queue has an incompatible lifetime");
        if (!qac_console_context_view_current(state, context, false, error) ||
            (chunk->completion && !qac_console_context_view_current(state, &chunk->caller, false, error))) return false;
    }
    return true;
}

static bool state_valid(const qa_console *state, const qa_console *candidate, qa_error *error)
{
    if (state->wait && (state->wait_context.session != candidate->options.context.session ||
        is_retired(candidate->owners, state->wait_context.owner) ||
        (state->wait_context.client && is_retired(candidate->clients, state->wait_context.client))))
        return invalid(error, "Console wait names a retired context");
    if (state->wait && !qac_console_context_view_current(candidate, &state->wait_context, false, error)) return false;
    return queue_valid(candidate, state->head, state->queued_bytes, error) &&
        queue_valid(candidate, state->deferred, state->deferred_bytes, error);
}

static bool fields(qa_source_save_io *io, qa_console *state, qa_console *candidate,
                     const qa_console_save_resolvers *resolve, uint64_t captured_registry)
{
    char magic[8] = {'Q','A','C','O','N','S','L',0};
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QACONSL", 8) &&
        qa_source_save_u64(io, &captured_registry) && captured_registry &&
        qa_source_save_i32(io, &state->wait) &&
        (!state->wait || context_fields(io, &state->wait_context, resolve, captured_registry)) &&
        qa_source_save_count(io, &state->alias_count, SIZE_MAX) &&
        chunks_fields(io, &state->head, &state->tail, &state->queued_bytes, resolve, captured_registry) &&
        chunks_fields(io, &state->deferred, &state->deferred_tail, &state->deferred_bytes, resolve, captured_registry) &&
        state_valid(state, candidate, io->error);
}

/* Keep current aliases, callbacks and options. Only queued Source execution
 * and its wait/recursion state belong to this continuation. */
static void state_exchange(qa_console *console, qa_console *state)
{
    qa_console old = *console;
    console->head = state->head; state->head = old.head;
    console->tail = state->tail; state->tail = old.tail;
    console->deferred = state->deferred; state->deferred = old.deferred;
    console->deferred_tail = state->deferred_tail; state->deferred_tail = old.deferred_tail;
    console->queued_bytes = state->queued_bytes; state->queued_bytes = old.queued_bytes;
    console->deferred_bytes = state->deferred_bytes; state->deferred_bytes = old.deferred_bytes;
    console->wait = state->wait; state->wait = old.wait;
    console->wait_context = state->wait_context; state->wait_context = old.wait_context;
    console->alias_count = state->alias_count; state->alias_count = old.alias_count;
    console->drain_yielded = state->drain_yielded; state->drain_yielded = old.drain_yielded;
    console->release_first = state->release_first; state->release_first = old.release_first;
    console->release_owner = state->release_owner; state->release_owner = old.release_owner;
    console->release_leases = state->release_leases; state->release_leases = old.release_leases;
    for (qa_console_release *at = console->release_first; at; at = at->next) at->console = console;
    qac_console_program_touch(console, true);
}

uint64_t qa_console_save_context_registry(qa_session *session, uint64_t registry, uint64_t captured_registry)
{
    return registry && registry == qa_actors_identity(qa_session_actors(session)) ? captured_registry : registry;
}
bool qa_console_save_capture(const qa_console *console, qa_session *session, qa_buffer *out, qa_error *error)
{
    return qa_console_save_capture_in_registry(console, session,
        qa_actors_identity(qa_session_actors(session)), false, out, error);
}

bool qa_console_save_restore(qa_console *console, qa_session *session,
    const qa_console_save_resolvers *resolve, qa_bytes bytes, qa_error *error)
{
    if (!console || !session || !resolve || !qa_console_idle(console) || console->program_leases || console->release_leases ||
        console->program_unpublished)
        return qac_fail(error, QA_ERROR_ARGUMENT, "Console restore requires an idle candidate owner");
    qa_console *scratch = calloc(1, sizeof(*scratch));
    if (!scratch) return qac_fail(error, QA_ERROR_MEMORY, "Allocating console restore candidate");
    scratch->options = console->options;
    scratch->options.context.script = NULL;
    scratch->options.startup_commands = NULL;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, session, bytes, error) && fields(&io, scratch, console, resolve, 0) &&
        qa_source_save_finish(&io, NULL) && qa_console_idle(console);
    if (ok) state_exchange(console, scratch);
    qa_console_destroy(scratch);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code == QA_OK)) invalid(error, "Invalid saved console continuation");
    return ok;
}

static bool fault_fields(qa_source_save_io *io,qa_error *fault)
{
    uint32_t code=(uint32_t)fault->code;
    size_t length=io->direction==QA_SOURCE_SAVE_READ?0:strlen(fault->message);
    if (!qa_source_save_u32(io,&code) || code>QA_ERROR_NOT_FOUND ||
        !qa_source_save_count(io,&fault->offset,SIZE_MAX) ||
        !qa_source_save_count(io,&length,sizeof(fault->message)-1) ||
        !qa_source_save_bytes(io,fault->message,length) || memchr(fault->message,0,length)) return false;
    fault->message[length]=0; fault->code=(qa_status)code;
    return code!=QA_OK || (!fault->offset && !length);
}
static bool release_context_equal(const qa_command_context *a,const qa_command_context *b)
{
    return a->session==b->session && a->owner==b->owner && a->client==b->client && a->seat==b->seat &&
        a->dialect==b->dialect && a->origin==b->origin && a->direct==b->direct &&
        a->console_text==b->console_text && a->cvar_view==b->cvar_view &&
        a->registry==b->registry && a->generation==b->generation &&
        qa_actor_id_equal(a->actor,b->actor) && ((!a->script && !b->script) ||
            (a->script && b->script && !strcmp(a->script,b->script)));
}
static bool releases_fields(qa_source_save_io *io,qa_console *state,const qa_console *candidate,
    const qa_console_save_resolvers *resolve,uint64_t registry)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    size_t count=state->release_leases,active=0;
    if (!qa_source_save_u64(io,&registry) || !registry ||
        !qa_source_save_count(io,&count,reading?io->input.size/16:SIZE_MAX) || !count) return false;
    qa_console_release **link=&state->release_first;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            *link=calloc(1,sizeof(**link));
            if (!*link) return qac_fail(io->error,QA_ERROR_MEMORY,"Retaining imported release programme");
            (*link)->console=state; (*link)->imported=true; ++state->release_leases;
        }
        qa_console_release *owner=*link;
        if (!owner || (reading?owner->console!=state:
                owner->console->release_first!=state->release_first) ||
            !context_fields(io,&owner->context,resolve,registry) ||
            !qa_source_save_bool(io,&owner->started) || !qa_source_save_bool(io,&owner->entered) ||
            !qa_source_save_bool(io,&owner->complete) || !fault_fields(io,&owner->fault) ||
            owner->context.session!=state->options.context.session ||
            is_retired(state->owners,owner->context.owner) ||
            (owner->context.client && is_retired(state->clients,owner->context.client)) ||
            !qac_console_context_view_current(candidate,&owner->context,false,io->error) ||
            (owner->entered && !owner->started) || (owner->complete && !owner->started) ||
            (!owner->started && owner->fault.code!=QA_OK)) return false;
        size_t prepared_bytes=owner->prepared?owner->prepared->length-owner->prepared->offset:0;
        command_chunk *last=NULL;
        if (!chunks_fields(io,&owner->prepared,&last,&prepared_bytes,resolve,registry) ||
            (owner->started && owner->prepared) || (owner->prepared && owner->prepared->next) ||
            !queue_valid(candidate,owner->prepared,prepared_bytes,io->error)) return false;
        if (owner->prepared) {
            qa_command_context empty={0};
            if (owner->prepared->offset || owner->prepared->completion || owner->prepared->success ||
                !release_context_equal(&owner->prepared->context,&owner->context) ||
                !release_context_equal(&owner->prepared->caller,&empty)) return false;
        }
        if (owner->started && !owner->complete) {
            if (++active!=1 || !qa_console_pending(state) ||
                !chunks_fields(io,&owner->head,&owner->tail,&owner->queued_bytes,resolve,registry) ||
                !chunks_fields(io,&owner->deferred,&owner->deferred_tail,&owner->deferred_bytes,resolve,registry) ||
                !qa_source_save_i32(io,&owner->wait) ||
                (owner->wait && !context_fields(io,&owner->wait_context,resolve,registry)) ||
                !qa_source_save_count(io,&owner->alias_count,SIZE_MAX) ||
                !qa_source_save_bool(io,&owner->drain_yielded) ||
                !queue_valid(candidate,owner->head,owner->queued_bytes,io->error) ||
                !queue_valid(candidate,owner->deferred,owner->deferred_bytes,io->error) ||
                (owner->wait && !qac_console_context_view_current(candidate,&owner->wait_context,false,io->error)) ||
                (owner->wait && (owner->wait_context.session!=state->options.context.session ||
                    is_retired(state->owners,owner->wait_context.owner) ||
                    (owner->wait_context.client && is_retired(state->clients,owner->wait_context.client))))) return false;
            if (reading) state->release_owner=owner;
            else if (state->release_owner!=owner) return false;
        }
        link=&owner->next;
    }
    return !*link && (active!=0)==(state->release_owner!=NULL);
}
static void release_chunks_free(command_chunk *chunk)
{
    while (chunk) {
        command_chunk *next=chunk->next;
        qac_console_chunk_free(chunk); chunk=next;
    }
}
static void imported_storage_free(qa_console *console)
{
    while (console->release_first) {
        qa_console_release *owner=console->release_first;
        console->release_first=owner->next;
        release_chunks_free(owner->prepared);
        release_chunks_free(owner->head); release_chunks_free(owner->deferred);
        free((char *)owner->context.script); free((char *)owner->wait_context.script); free(owner);
    }
    console->release_leases=0; console->release_owner=NULL;
}
bool qa_console_save_capture_in_registry(const qa_console *console,qa_session *session,
    uint64_t captured_registry,bool releases,qa_buffer *out,qa_error *error)
{
    bool returned=console && session && captured_registry && out && qa_console_idle(console) &&
        !console->program_leases && !console->program_unpublished;
    if (!releases && (!returned || console->release_leases))
        return qac_fail(error,QA_ERROR_ARGUMENT,"Console capture requires its idle owner and session");
    if (releases && (!returned || !console->release_leases || console->release_advancing || console->pending_program))
        return qac_fail(error,QA_ERROR_ARGUMENT,"Release capture requires the returned actual programme roster");
    qa_source_save_io io={0}; qa_console copy=*console;
    char magic[4]={'Q','A','C','R'};
    bool ok=qa_source_save_writer(&io,session,error) && (!releases || qa_source_save_bytes(&io,magic,4)) &&
        fields(&io,&copy,(qa_console *)console,NULL,captured_registry) &&
        (!releases || (qa_source_save_bool(&io,&copy.drain_yielded) &&
            releases_fields(&io,&copy,console,NULL,captured_registry))) && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code==QA_OK))
        invalid(error,releases?"Invalid retained console release roster":"Invalid console continuation");
    return ok;
}
bool qa_console_release_save_capture(const qa_console *console,qa_session *session,qa_buffer *out,qa_error *error)
{
    return qa_console_save_capture_in_registry(console,session,
        qa_actors_identity(qa_session_actors(session)),true,out,error);
}
bool qa_console_release_save_present(const qa_console *console)
{ return console && console->release_first && console->release_leases; }
bool qa_console_release_save_restore(qa_console *console,qa_session *session,
    const qa_console_save_resolvers *resolve,qa_bytes bytes,qa_error *error)
{
    if (!console || !session || !resolve || !qa_console_idle(console) || console->program_leases ||
        console->release_leases || console->program_unpublished || console->pending_program)
        return qac_fail(error,QA_ERROR_ARGUMENT,"Release import requires the unleased candidate console");
    qa_console *scratch=calloc(1,sizeof(*scratch));
    if (!scratch) return qac_fail(error,QA_ERROR_MEMORY,"Allocating release import");
    scratch->options=console->options; scratch->options.context.script=NULL;
    scratch->options.startup_commands=NULL;
    qa_source_save_io io={0}; char magic[4];
    bool ok=qa_source_save_reader(&io,session,bytes,error) && qa_source_save_bytes(&io,magic,4) &&
        !memcmp(magic,"QACR",4) &&
        fields(&io,scratch,console,resolve,0) && qa_source_save_bool(&io,&scratch->drain_yielded) &&
        releases_fields(&io,scratch,console,resolve,0) && qa_source_save_finish(&io,NULL) && qa_console_idle(console);
    if (ok) state_exchange(console,scratch);
    imported_storage_free(scratch); qa_console_destroy(scratch);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code==QA_OK)) invalid(error,"Invalid saved console release");
    return ok;
}
bool qa_console_release_save_reference(const qa_console *console,const qa_console_release *owner,uint64_t *out)
{
    if (!console || !owner || !out || !qa_console_idle(console) || console->release_advancing) return false;
    uint64_t id=1;
    for (const qa_console_release *at=console->release_first;at;at=at->next,++id)
        if (at==owner && at->console==console) { *out=id; return true; }
    return false;
}
qa_console_release *qa_console_release_restore_reference(qa_console *console,uint64_t id)
{
    if (!console || !id || !qa_console_idle(console) || console->release_advancing) return NULL;
    qa_console_release *at=console->release_first;
    while (at && --id) at=at->next;
    return at && at->imported && !at->claimed?at:NULL;
}
bool qa_console_release_restore_claim(qa_console_release *owner,qa_error *error)
{
    if (!owner || !owner->imported || owner->claimed || !qa_console_idle(owner->console))
        return qac_fail(error,QA_ERROR_ARGUMENT,"Release import has no unclaimed actual programme");
    owner->claimed=true; return true;
}
bool qa_console_release_restore_finish(const qa_console *console,qa_error *error)
{
    if (!console || !qa_console_idle(console)) return qac_fail(error,QA_ERROR_ARGUMENT,"Release import has not returned");
    for (const qa_console_release *at=console->release_first;at;at=at->next)
        if (at->imported && !at->claimed) return invalid(error,"Imported programme has no physical input owner");
    return true;
}
bool qa_console_release_restore_unclaimed(const qa_console *console)
{
    if (!console || !console->release_first || !console->release_leases ||
        !qa_console_idle(console) || console->release_advancing) return false;
    size_t count=0;
    for (const qa_console_release *at=console->release_first;at;at=at->next) {
        if (at->console!=console || !at->imported || at->claimed) return false;
        ++count;
    }
    return count==console->release_leases;
}
bool qa_console_release_restore_abort(qa_console *console,qa_error *error)
{
    if (!console || !qa_console_idle(console) || console->release_advancing)
        return qac_fail(error,QA_ERROR_ARGUMENT,"Release import cleanup has not returned");
    for (const qa_console_release *at=console->release_first;at;at=at->next)
        if (!at->imported || at->claimed) return qac_fail(error,QA_ERROR_ARGUMENT,"Release cleanup belongs to its retained input owner");
    if (console->release_owner) qa_console_release_retirement_publish(console->release_owner);
    while (console->release_first) qa_console_release_retirement_publish(console->release_first);
    return true;
}

#include "commands_private.h"
#include "qa/console_release.h"
#include <stdlib.h>
#include <string.h>

static void chunk_free(command_chunk *chunk)
{
    if (!chunk) return;
    free((char *)chunk->context.script); free((char *)chunk->caller.script);
    free(chunk->text); free(chunk);
}
static void release_free(qa_console_release *owner)
{
    qa_console_release **link=&owner->console->release_first;
    while (*link && *link!=owner) link=&(*link)->next;
    if (*link) *link=owner->next;
    chunk_free(owner->prepared);
    free((char *)owner->context.script);
    --owner->console->release_leases; free(owner);
}
bool qac_console_release_access(const qa_console *console,qa_error *error)
{
    if (console && console->release_leases && !console->release_advancing)
        return qac_fail(error,QA_ERROR_ARGUMENT,"console program is retained by a source release");
    return true;
}
void qac_console_release_enter(qa_console *console)
{
    if (console->release_advancing && console->release_owner)
        console->release_owner->entered=true;
}
bool qa_console_release_parent_ready(const qa_console_release *parent,const qa_console *console,qa_error *error)
{
    if (!parent || !console || parent->console!=console || !console->release_leases ||
        !qa_console_idle(console) || console->release_advancing || console->program_unpublished ||
        console->pending_program)
        return qac_fail(error,QA_ERROR_ARGUMENT,"sibling capture lacks its returned same-console programme lease");
    return true;
}
static bool prepare(qa_console *console,const qa_console_release *parent,
    const qa_command_context *context,const char *text,qa_console_release **out,qa_error *error)
{
    if (!console || !context || !text || !out || *out || !qa_console_idle(console) ||
        console->release_advancing || (console->release_owner && !parent) ||
        (parent && !qa_console_release_parent_ready(parent,console,error)) ||
        console->program_unpublished || console->pending_program || console->release_leases==SIZE_MAX)
        return qac_fail(error,QA_ERROR_ARGUMENT,"source release requires its returned actual console and empty output");
    size_t maximum=0,limit=0,length=strlen(text);
    if (!qa_console_limits(console,context,&maximum,&limit,error)) return false;
    (void)maximum;
    if (length>=limit) return qac_fail(error,QA_ERROR_FORMAT,"source release exceeds its actual command buffer");
    qa_console_release *owner=calloc(1,sizeof(*owner));
    if (!owner) return qac_fail(error,QA_ERROR_MEMORY,"allocating source release program");
    owner->console=console;
    bool ok=qac_console_context_capture(console,context,&owner->context,error);
    if (ok && length) {
        owner->prepared=calloc(1,sizeof(*owner->prepared));
        if (!owner->prepared) ok=qac_fail(error,QA_ERROR_MEMORY,"allocating retained release chunk");
        if (ok) {
            command_chunk *chunk=owner->prepared;
            chunk->context=owner->context; chunk->context.script=NULL;
            if (owner->context.script) {
                chunk->context.script=qac_copy(owner->context.script,error);
                if (!chunk->context.script) ok=false;
            }
            chunk->text=qac_copy(text,error); chunk->length=length;
            if (!chunk->text) ok=false;
        }
    }
    if (!ok) { chunk_free(owner->prepared); free((char *)owner->context.script); free(owner); return false; }
    qa_console_release **tail=&console->release_first;
    while (*tail) tail=&(*tail)->next;
    *tail=owner; ++console->release_leases; *out=owner; return true;
}
bool qa_console_release_prepare(qa_console *console,const qa_command_context *context,
    const char *text,qa_console_release **out,qa_error *error)
{ return prepare(console,NULL,context,text,out,error); }
bool qa_console_release_prepare_sibling(qa_console *console,const qa_console_release *parent,
    const qa_command_context *context,const char *text,qa_console_release **out,qa_error *error)
{
    if (!parent) return qac_fail(error,QA_ERROR_ARGUMENT,"sibling capture requires its retained actual release parent");
    return prepare(console,parent,context,text,out,error);
}
static void restore_program(qa_console_release *owner)
{
    qa_console *console=owner->console;
    free((char *)console->wait_context.script);
    console->head=owner->head; console->tail=owner->tail;
    console->deferred=owner->deferred; console->deferred_tail=owner->deferred_tail;
    console->queued_bytes=owner->queued_bytes; console->deferred_bytes=owner->deferred_bytes;
    owner->head=owner->tail=owner->deferred=owner->deferred_tail=NULL;
    owner->queued_bytes=owner->deferred_bytes=0;
    console->wait=owner->wait; console->wait_context=owner->wait_context;
    owner->wait_context=(qa_command_context){0};
    console->alias_count=owner->alias_count; console->drain_yielded=owner->drain_yielded;
    console->release_owner=NULL; owner->complete=true;
}
bool qa_console_release_advance(qa_console_release *owner,qa_console_release_outcome *out,qa_error *error)
{
    if (!owner || !out || !qa_console_idle(owner->console))
        return qac_fail(error,QA_ERROR_ARGUMENT,"source release advance requires its returned owner");
    qa_console *console=owner->console;
    *out=owner->entered?QA_CONSOLE_RELEASE_WAITING:QA_CONSOLE_RELEASE_UNENTERED;
    if (owner->complete) {
        *out=owner->fault.code==QA_OK?QA_CONSOLE_RELEASE_COMPLETED:QA_CONSOLE_RELEASE_FAILED;
        if (owner->fault.code!=QA_OK) { if (error && error->code==QA_OK) *error=owner->fault; return false; }
        return qac_console_context_current(console,&owner->context,error);
    }
    if (console->release_owner && console->release_owner!=owner) {
        *out=QA_CONSOLE_RELEASE_WAITING; return true;
    }
    if (!qac_console_context_current(console,&owner->context,error)) return false;
    if (!owner->started) {
        owner->head=console->head; owner->tail=console->tail;
        owner->deferred=console->deferred; owner->deferred_tail=console->deferred_tail;
        owner->queued_bytes=console->queued_bytes; owner->deferred_bytes=console->deferred_bytes;
        owner->wait=console->wait; owner->wait_context=console->wait_context;
        owner->alias_count=console->alias_count; owner->drain_yielded=console->drain_yielded;
        console->head=console->tail=owner->prepared;
        console->queued_bytes=owner->prepared?owner->prepared->length:0;
        owner->prepared=NULL;
        console->deferred=console->deferred_tail=NULL; console->deferred_bytes=0;
        console->wait=0; console->wait_context=(qa_command_context){0};
        console->release_owner=owner; owner->started=true;
    }
    console->release_advancing=true;
    qa_error fault={0}; bool ok=qa_console_drain(console,0,NULL,&fault);
    console->release_advancing=false;
    if (!ok) {
        if (fault.code==QA_OK) qac_fail(&fault,QA_ERROR_ARGUMENT,"entered source release failed");
        if (owner->fault.code==QA_OK) owner->fault=fault;
    }
    if (!qa_console_pending(console)) restore_program(owner);
    *out=owner->complete?(owner->fault.code==QA_OK?QA_CONSOLE_RELEASE_COMPLETED:QA_CONSOLE_RELEASE_FAILED):
        (owner->entered?QA_CONSOLE_RELEASE_WAITING:QA_CONSOLE_RELEASE_UNENTERED);
    if (!ok || owner->fault.code!=QA_OK) {
        if (error && error->code==QA_OK) *error=owner->fault;
        return false;
    }
    return true;
}
bool qa_console_release_completed_is(const qa_console_release *owner,const qa_console *console)
{
    return owner && console && owner->console==console && owner->started && owner->complete &&
        owner->fault.code==QA_OK && !owner->prepared && console->release_leases &&
        !console->release_owner && !console->release_advancing && qa_console_idle(console);
}
bool qa_console_release_ready(const qa_console_release *owner,const qa_console *console,qa_error *error)
{
    if (!qa_console_release_completed_is(owner,console))
        return qac_fail(error,QA_ERROR_ARGUMENT,"source release has no completed successful actual program");
    return qac_console_context_current(console,&owner->context,error);
}
bool qa_console_release_active(const qa_console_release *owner)
{ return owner && owner->console->release_owner==owner &&
    owner->console->release_advancing && owner->console->frame; }
static bool same_source(const qa_command_context *a,const qa_command_context *b)
{
    return a->session==b->session && a->owner==b->owner && a->client==b->client &&
        a->seat==b->seat && a->origin==b->origin && a->registry==b->registry &&
        a->generation==b->generation && a->actor.registry==b->actor.registry &&
        a->actor.generation==b->actor.generation && a->actor.slot==b->actor.slot;
}
bool qa_console_release_context_current(const qa_console_release *owner,const qa_console *console,
    const qa_command_context *command)
{
    if (!owner || !console || !command || owner->console!=console ||
        console->release_owner!=owner || !console->release_leases || !console->release_advancing ||
        !console->release_dispatch_context) return false;
    const qa_command_context *actual=console->release_dispatch_context;
    return same_source(&owner->context,command) && same_source(actual,command) &&
        actual->dialect==command->dialect && actual->direct==command->direct &&
        actual->console_text==command->console_text &&
        ((!actual->script && !command->script) || (actual->script && command->script &&
            !strcmp(actual->script,command->script)));
}
bool qa_console_release_entered(const qa_console_release *owner)
{ return owner && owner->entered; }
bool qa_console_release_state_read(const qa_console_release *owner,const qa_console *console,
    const qa_command_context *command,bool *complete,bool *entered,qa_status *fault)
{
    if (!owner || !console || !command || !complete || !entered || !fault || owner->console!=console ||
        !console->release_leases || !qa_console_idle(console) || console->release_advancing ||
        !same_source(&owner->context,command) || owner->context.dialect!=command->dialect ||
        owner->context.direct!=command->direct || owner->context.console_text!=command->console_text ||
        ((!owner->context.script)!=(!command->script)) ||
        (command->script && strcmp(command->script,owner->context.script))) return false;
    *complete=owner->complete; *entered=owner->entered; *fault=owner->fault.code; return true;
}
bool qa_console_release_abort(qa_console_release *owner,qa_console_release_outcome *out,qa_error *error)
{
    if (!owner || !out || !qa_console_idle(owner->console) ||
        (owner->entered && (!owner->complete || owner->fault.code!=QA_OK)))
        return qac_fail(error,QA_ERROR_ARGUMENT,"source release abort requires unentered or terminal continuation");
    *out=!owner->entered?QA_CONSOLE_RELEASE_UNENTERED:
        owner->fault.code==QA_OK?QA_CONSOLE_RELEASE_COMPLETED:QA_CONSOLE_RELEASE_FAILED;
    if (owner->started && !owner->complete) {
        command_chunk *chunk=owner->console->head;
        while (chunk) { command_chunk *next=chunk->next; chunk_free(chunk); chunk=next; }
        chunk=owner->console->deferred;
        while (chunk) { command_chunk *next=chunk->next; chunk_free(chunk); chunk=next; }
        restore_program(owner);
    }
    release_free(owner); return true;
}
bool qa_console_release_retirement_ready(const qa_console_release *owner,
    qa_console_release_disposition disposition,qa_console_release_retirement_fn qualify,
    void *context,qa_error *error)
{
    if (!owner || !qualify || !owner->console->release_leases ||
        !qa_console_idle(owner->console) || owner->console->release_advancing ||
        disposition<QA_CONSOLE_RELEASE_RETIRED_ACTOR || disposition>QA_CONSOLE_RELEASE_DETACHED_SOURCE ||
        (disposition==QA_CONSOLE_RELEASE_RETIRED_ACTOR &&
            (!owner->context.actor.registry || owner->context.registry!=owner->context.actor.registry)))
        return qac_fail(error,QA_ERROR_ARGUMENT,"source release lacks its actual retirement qualifier");
    return qualify(context,owner->console,&owner->context,disposition,error);
}
void qa_console_release_retirement_publish(qa_console_release *owner)
{
    if (owner->started && !owner->complete) {
        command_chunk *chunk=owner->console->head;
        while (chunk) { command_chunk *next=chunk->next; chunk_free(chunk); chunk=next; }
        chunk=owner->console->deferred;
        while (chunk) { command_chunk *next=chunk->next; chunk_free(chunk); chunk=next; }
        restore_program(owner);
    }
    release_free(owner);
}
void qa_console_release_publish(qa_console_release *owner)
{ release_free(owner); }

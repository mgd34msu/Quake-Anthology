#include "commands_private.h"
#include "qa/cvars_alias.h"
#include "qa/text.h"
#include "qa/console_cvars_prepare.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

static const qa_console_options *options_for(const qa_console *console,
    const qa_command_context *context)
{
    context=context_for(console,context);
    for (const console_source *source=console->sources; source; source=source->next)
        if (source->options.context.cvar_view==context->cvar_view) return &source->options;
    return !context->cvar_view || context->cvar_view==console->options.context.cvar_view
        ? &console->options : NULL;
}

static bool capture_context(const qa_console *console,const qa_command_context *source,
    qa_command_context *out,qa_error *error)
{
    const qa_console_options *options=options_for(console,source);
    if (!options) return qac_fail(error,QA_ERROR_ARGUMENT,"command cvar view has retired");
    uint64_t source_view=source->cvar_view;
    *out=*source;
    if (options->capture_context && !options->capture_context(options->user,source,out,error)) return false;
    if (!source_view && options==&console->options && !out->cvar_view)
        out->cvar_view=console->options.context.cvar_view;
    return out->cvar_view==source_view ||
        (!source_view && options==&console->options &&
         out->cvar_view==console->options.context.cvar_view) ||
        qac_fail(error,QA_ERROR_ARGUMENT,"command capture changed its actual cvar view");
}

static bool valid_context_base(const qa_console *console, const qa_command_context *context,
    qa_error *error)
{
    if (context->session != console->options.context.session || !qac_dialect_valid(context->dialect) ||
        context->origin < QA_COMMAND_LOCAL || context->origin > QA_COMMAND_REMOTE)
        return qac_fail(error, QA_ERROR_ARGUMENT, "command context does not belong to this session");
    if (retired(console->owners, context->owner) || (context->client != 0 && retired(console->clients, context->client)))
        return qac_fail(error, QA_ERROR_ARGUMENT, "command owner or client has retired");
    return true;
}
bool qac_console_context_view_current(const qa_console *console,const qa_command_context *context,
    bool publication,qa_error *error)
{
    if (!valid_context_base(console,context,error)) return false;
    if (!context->cvar_view && (console->options.context.cvar_view || console->sources))
        return qac_fail(error,QA_ERROR_ARGUMENT,"command lacks its actual cvar view");
    const qa_console_options *options=options_for(console,context);
    if (!options || ((options!=&console->options || options->context.cvar_view) &&
        context->owner!=options->context.owner) || (options!=&console->options &&
        ((options->context.origin!=QA_COMMAND_SERVER && context->seat!=options->context.seat) ||
         context->dialect!=options->context.dialect)))
        return qac_fail(error,QA_ERROR_ARGUMENT,"command lost its actual Source cvar view");
    if (publication && options->context_active != NULL &&
        !options->context_active(options->user, context))
        return qac_fail(error, QA_ERROR_ARGUMENT, "command publication or actor has retired");
    return true;
}
static bool valid_context(const qa_console *console,const qa_command_context *context,qa_error *error)
{ return qac_console_context_view_current(console,context,true,error); }
static bool source_context_current(const qa_console *console,const qa_command_context *source,
    qa_command_context *out,qa_error *error)
{
    if (!source->cvar_view || source->cvar_view==console->options.context.cvar_view)
        return qac_fail(error,QA_ERROR_ARGUMENT,"Source callback requires its actual bound constructor view");
    return capture_context(console,source,out,error) && valid_context(console,out,error);
}
bool qa_console_context_bound(const qa_console *console,const qa_command_context *context)
{
    qa_error error={0};
    return console && context && qac_console_context_view_current(console,context,false,&error);
}

static bool same_context(const qa_command_context *a, const qa_command_context *b,
                          bool ignore_direct)
{
    return a->session == b->session && a->owner == b->owner && a->client == b->client &&
           a->registry == b->registry && a->generation == b->generation && a->cvar_view == b->cvar_view &&
           a->actor.registry == b->actor.registry &&
           a->actor.generation == b->actor.generation && a->actor.slot == b->actor.slot &&
           a->seat == b->seat && a->dialect == b->dialect && a->origin == b->origin &&
           a->console_text == b->console_text && (ignore_direct || a->direct == b->direct) &&
           ((a->script == NULL && b->script == NULL) ||
            (a->script != NULL && b->script != NULL && strcmp(a->script, b->script) == 0));
}
typedef struct qac_cvar_scope {
    qa_command_context source,constructor;
    qa_console_cvar_entered_fn qualifier;
    void *user;
    struct qac_cvar_scope *parent;
} qac_cvar_scope;
static bool cvar_scope_current(const qa_console *console,const qa_command_context *context,
    qa_error *error)
{
    const qac_cvar_scope *scope=console->cvar_scope;
    return scope && same_context(&scope->source,context,false) &&
        (scope->qualifier?
            valid_context_base(console,context,error) &&
                scope->qualifier(scope->user,console,&scope->source,error):
            valid_context(console,context,error));
}
bool qa_console_cvar_entered(const qa_console *console,const qa_command_context *context)
{
    qa_error error={0};
    return console && context && console->cvar_scope && console->cvar_scope->qualifier &&
        cvar_scope_current(console,context,&error);
}
static bool valid_cvar_context(const qa_console *console,const qa_command_context *context,
    qa_error *error)
{
    if (console->cvar_scope && same_context(&console->cvar_scope->source,context,false))
        return cvar_scope_current(console,context,error);
    return valid_context(console,context,error);
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
bool qac_console_context_current(const qa_console *console,const qa_command_context *context,
    qa_error *error)
{ return console && context?valid_context(console,context,error):
    qac_fail(error,QA_ERROR_ARGUMENT,"source release lacks its actual command context"); }
bool qac_console_context_capture(qa_console *console,const qa_command_context *context,
    qa_command_context *out,qa_error *error)
{
    qa_command_context captured;
    if (!capture_context(console,context,&captured,error)) return false;
    return valid_context(console,&captured,error) && copy_context(out,&captured,error);
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
    const qa_console_options *options=options_for(console,context);
    if (!options) return;
    void (*print)(void *, const qa_command_context *, const char *) =
        console->redirect == NULL ? options->print : console->redirect->print;
    void *user = console->redirect == NULL ? options->user : console->redirect->user;
    if (print != NULL) {
        ++console->output_calls;
        print(user, context, text);
        --console->output_calls;
    }
}

void qa_console_emit(qa_console *console, const qa_command_context *context, const char *text)
{
    if (console != NULL && text != NULL)
        output(console, context_for(console, context), text);
}

static bool returned(const qa_console *console)
{
    return console == NULL || (console->frame == NULL && console->redirect == NULL &&
        console->output_calls == 0 && !console->draining);
}
bool qa_console_idle(const qa_console *console)
{ return returned(console) && (!console || !console->cvar_scope); }
bool qa_console_cvar_returned(const qa_console *console)
{
    return returned(console) && (!console || !console->cvar_scope ||
        qa_console_cvar_entered(console, &console->cvar_scope->source));
}
bool qa_console_invocation_current(const qa_console *console,const qa_command_invocation *command)
{
    return console && command && command->console==console && console->frame &&
        console->frame->invocation==command;
}
bool qa_console_invocation_delivered(const qa_command_invocation *command,uint64_t receiver,
    uint64_t lifetime_owner)
{
    if (!command || !qa_console_invocation_current(command->console,command)) return false;
    for (const command_call *call=command->console->frame->contributions; call; call=call->parent)
        if (call->receiver==receiver && call->lifetime_owner==lifetime_owner) return true;
    return false;
}
bool qa_console_invocation_delivered_view(const qa_command_invocation *command,uint64_t view,
    uint64_t receiver,uint64_t lifetime_owner)
{
    if (!command || !view || !qa_console_invocation_current(command->console,command)) return false;
    for (const command_call *call=command->console->frame->contributions; call; call=call->parent)
        if (call->cvar_view==view && call->receiver==receiver && call->lifetime_owner==lifetime_owner) return true;
    return false;
}
bool qa_console_context_delivered_view(const qa_console *console,const qa_command_context *context,
    uint64_t view,uint64_t receiver,uint64_t lifetime_owner)
{
    return console && console->frame && context==&console->frame->invocation->context &&
        qa_console_invocation_delivered_view(console->frame->invocation,view,receiver,lifetime_owner);
}
qa_command_result qa_console_forward_source(qa_console *console,const qa_command_invocation *command,
    const qa_command_context *target,uint64_t lifetime_owner,qa_error *error)
{
    if (!console || !command || !target || !lifetime_owner || !target->cvar_view ||
        target->cvar_view==console->options.context.cvar_view ||
        !qa_console_invocation_current(console,command) || retired(console->owners,lifetime_owner)) {
        qac_fail(error,QA_ERROR_ARGUMENT,"Source forwarding requires its entered invocation and actual live callback lifetime");
        return QA_COMMAND_FAILED;
    }
    qa_command_context captured;
    if (!valid_context(console,&command->context,error) ||
        !source_context_current(console,target,&captured,error))
        return QA_COMMAND_FAILED;
    const qa_console_options *options=options_for(console,&captured);
    if (!options->forward) return QA_COMMAND_UNHANDLED;
    command_frame *frame=console->frame;
    command_call call={.receiver=captured.owner,.lifetime_owner=lifetime_owner,
        .cvar_view=captured.cvar_view,.parent=frame->contributions};
    frame->contributions=&call;
    qa_command_result result=options->forward(options->user,command,error);
    frame->contributions=call.parent;
    return result;
}
bool qa_console_forward_text(const qa_command_invocation *command,const char **text,
    bool *explicit_command,qa_error *error)
{
    if(!command||!text||!explicit_command||
        !qa_console_invocation_current(command->console,command)||!command->argc||
        !command->argv||!command->argv[0]||!command->raw||!command->args_text)
        return qac_fail(error,QA_ERROR_ARGUMENT,"Forwarded text requires its actual entered console invocation");
    *explicit_command=qac_equal(command->argv[0],"cmd");
    *text=*explicit_command?command->args_text:command->argc>1?command->raw:command->argv[0];
    return true;
}

bool qa_console_output_redirected(const qa_console *console)
{
    return console != NULL && console->redirect != NULL;
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
    const qa_console_options *options=options_for(console,context);
    return !options ? NULL : options->cvar_owner == NULL ? options->cvars :
        options->cvar_owner(options->user, context, name);
}

static qa_cvars *visible_cvars(qa_console *console, const qa_command_context *context,
                                size_t index)
{
    const qa_console_options *options=options_for(console,context);
    if (!options) return NULL;
    if (options->visible_cvars != NULL)
        return options->visible_cvars(options->user, context, index);
    return index == 0 ? cvar_owner(console, context, "") : NULL;
}

qa_cvars *qa_console_visible_cvars(qa_console *console, const qa_command_context *context, size_t index)
{
    if (!console) return NULL;
    return visible_cvars(console, context_for(console, context), index);
}

qa_cvars *qa_console_cvar_owner(qa_console *console, const qa_command_context *context, const char *name)
{
    return console && name ? cvar_owner(console, context_for(console, context), name) : NULL;
}

typedef struct cvar_access {
    qa_cvars *registry;
    qa_cvars_edit *edit;
} cvar_access;
static bool cvar_access_read(qa_console *console,const qa_command_context *context,
    qa_cvars *registry,cvar_access *out,qa_error *error)
{
    *out=(cvar_access){.registry=registry};
    const qa_console_options *options=options_for(console,context);
    if (!options) return qac_fail(error,QA_ERROR_ARGUMENT,"prepared cvar view has retired");
    if (!registry || !options->cvar_edit) return true;
    if (!options->cvar_edit(options->user,context,registry,&out->edit,error)) return false;
    if (out->edit && !qa_cvars_same_store(qa_cvars_edit_registry(out->edit),registry))
        return qac_fail(error,QA_ERROR_ARGUMENT,"prepared cvar view belongs to another registry");
    return true;
}
static bool cvar_enter(cvar_access access,qa_error *error)
{ return !access.edit || qa_cvars_edit_enter(access.edit,access.registry,error); }
static bool cvar_leave(cvar_access access,qa_error *error)
{ return !access.edit || qa_cvars_edit_leave(access.edit,access.registry,error); }
static bool cvar_find(cvar_access access,const char *name,const qa_cvar_view **out,qa_error *error)
{
    if (!cvar_enter(access,error)) return false;
    *out=qa_cvars_find(access.registry,name); return cvar_leave(access,error);
}
static bool cvar_at(cvar_access access,size_t ordinal,const qa_cvar_view **out,qa_error *error)
{
    if (!cvar_enter(access,error)) return false;
    *out=qa_cvars_visible_at(access.registry,ordinal); return cvar_leave(access,error);
}
static bool cvar_count(cvar_access access,size_t *out,qa_error *error)
{
    if (!cvar_enter(access,error)) return false;
    *out=qa_cvars_visible_count(access.registry); return cvar_leave(access,error);
}
static bool cvar_handles(cvar_access access,size_t *out,qa_error *error)
{
    if (!cvar_enter(access,error)) return false;
    *out=qa_cvars_handle_count(access.registry); return cvar_leave(access,error);
}
static bool cvar_apply(cvar_access access,const qa_cvars_edit_command *command,qa_error *error)
{
    if (!access.registry) return qac_fail(error,QA_ERROR_NOT_FOUND,"command has no cvar owner");
    if (!cvar_enter(access,error)) return false;
    bool ok=qa_cvars_apply(access.registry,command,error);
    bool left=cvar_leave(access,error); return ok && left;
}
bool qa_console_cvar_read(qa_console *console,const qa_command_context *context,const char *name,
    const qa_cvar_view **out,qa_error *error)
{
    if (!console || !name || !out)
        return qac_fail(error,QA_ERROR_ARGUMENT,"routed cvar read requires its console, name and output");
    context=context_for(console,context);
    if (!valid_cvar_context(console,context,error)) return false;
    cvar_access access;
    if (!cvar_access_read(console,context,cvar_owner(console,context,name),&access,error)) return false;
    return cvar_find(access,name,out,error);
}
bool qa_console_cvar_context(qa_console *console,const qa_command_context *source,
    qa_command_context *out,qa_error *error)
{
    if (!console || !source || !out)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar context requires its actual console and constructor context");
    if (console->cvar_scope &&
        (same_context(&console->cvar_scope->constructor,source,false) ||
         same_context(&console->cvar_scope->source,source,false))) {
        if (!cvar_scope_current(console,&console->cvar_scope->source,error)) return false;
        *out=console->cvar_scope->source; return true;
    }
    qa_command_context captured;
    if (!capture_context(console,source,&captured,error)) return false;
    if (!valid_context(console,&captured,error)) return false;
    *out=captured; return true;
}
bool qa_console_context_read(qa_console *console,qa_command_context *out,qa_error *error)
{
    if (!console || !out)
        return qac_fail(error,QA_ERROR_ARGUMENT,"Console context requires its actual constructor");
    qa_command_context source=console->options.context;
    if (source.cvar_view && console->options.cvars)
        source.dialect=qa_cvars_dialect(console->options.cvars);
    return qa_console_cvar_context(console,&source,out,error);
}
bool qa_console_cvar_enter(qa_console *console,const qa_command_context *source,
    qa_console_cvar_entered_fn qualifier,void *qualifier_user,
    qa_console_cvar_operation_fn operation,void *operation_user,qa_error *error)
{
    if (!console || !source || !operation)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar operation requires its physical console and constructor context");
    qa_command_context captured; qa_error ordinary={0};
    bool current=qa_console_cvar_context(console,source,&captured,&ordinary);
    if (!current) {
        if (!qualifier) { if (error) *error=ordinary; return false; }
        if (!valid_context_base(console,source,error)) return false;
        if (!qualifier(qualifier_user,console,source,error)) {
            if (!error || error->code==QA_OK)
                qac_fail(error,QA_ERROR_ARGUMENT,"cvar operation has no actual entered host tuple");
            return false;
        }
        captured=*source;
    }
    /* An inherited entered loan keeps its real qualifier on nested use. */
    if (current && console->cvar_scope && console->cvar_scope->qualifier &&
        same_context(&console->cvar_scope->source,&captured,false)) {
        qualifier=console->cvar_scope->qualifier; qualifier_user=console->cvar_scope->user;
    } else if (current) qualifier=NULL;
    qac_cvar_scope scope={.source=captured,.constructor=*source,.qualifier=qualifier,.user=qualifier_user,
        .parent=console->cvar_scope};
    console->cvar_scope=&scope;
    bool ok=operation(operation_user,&scope.source,error);
    if (ok) ok=cvar_scope_current(console,&scope.source,error);
    console->cvar_scope=scope.parent;
    return ok;
}
bool qa_console_cvar_access(qa_console *console,const qa_command_context *context,const char *name,
    qa_cvars **registry,qa_cvars_edit **edit,qa_error *error)
{
    if (!console || !name || !registry || !edit)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar access requires its actual routed name and outputs");
    context=context_for(console,context);
    if (!valid_cvar_context(console,context,error)) return false;
    cvar_access access;
    if (!cvar_access_read(console,context,cvar_owner(console,context,name),&access,error)) return false;
    if (!access.registry) return qac_fail(error,QA_ERROR_NOT_FOUND,"cvar access has no actual name owner");
    *registry=access.registry; *edit=access.edit; return true;
}
static bool cvar_snapshot_access(qa_console *console,const qa_command_context *context,
    qa_cvars *registry,cvar_access *out,qa_error *error)
{
    if (!console || !registry || !out)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar snapshot requires its console and actual visible registry");
    context=context_for(console,context);
    if (!valid_cvar_context(console,context,error)) return false;
    bool visible=false;
    for (size_t i=0;;++i) {
        qa_cvars *actual=qa_console_visible_cvars(console,context,i);
        if (!actual) break;
        if (actual==registry) { visible=true; break; }
    }
    if (!visible) return qac_fail(error,QA_ERROR_ARGUMENT,"cvar snapshot registry is not an admitted visible owner");
    return cvar_access_read(console,context,registry,out,error);
}
bool qa_console_cvar_snapshot_at(qa_console *console,const qa_command_context *context,
    qa_cvars *registry,size_t ordinal,const qa_cvar_view **out,qa_error *error)
{
    cvar_access access;
    if (!out || !cvar_snapshot_access(console,context,registry,&access,error)) return false;
    return cvar_at(access,ordinal,out,error);
}
bool qa_console_cvar_handle(qa_console *console,const qa_command_context *context,
    qa_cvars *registry,size_t handle,const qa_cvar_view **out,qa_error *error)
{
    cvar_access access;
    if (!out || !cvar_snapshot_access(console,context,registry,&access,error)) return false;
    size_t count;
    if (!cvar_handles(access,&count,error)) return false;
    if (handle>=count)
        return qac_fail(error,QA_ERROR_ARGUMENT,"cvar handle is outside its admitted registry extent");
    if (!cvar_enter(access,error)) return false;
    *out=qa_cvars_handle(registry,handle); return cvar_leave(access,error);
}
bool qa_console_cvar_apply(qa_console *console,const qa_command_context *context,
    const qa_cvars_edit_command *command,qa_error *error)
{
    if (!console || !command)
        return qac_fail(error,QA_ERROR_ARGUMENT,"routed cvar mutation requires its console and operation");
    context=context_for(console,context);
    if (!valid_cvar_context(console,context,error)) return false;
    cvar_access access;
    if (!cvar_access_read(console,context,cvar_owner(console,context,command->name?command->name:""),&access,error)) return false;
    return cvar_apply(access,command,error);
}
bool qa_console_cvar_startup_set(qa_console *console,const qa_command_context *context,
    const char *name,const char *value,qa_error *error)
{
    if (!console || !name || !value)
        return qac_fail(error,QA_ERROR_ARGUMENT,"startup cvar publication requires its console and scalar");
    context=context_for(console,context);
    if (!valid_cvar_context(console,context,error)) return false;
    cvar_access access;
    if (!cvar_access_read(console,context,cvar_owner(console,context,name),&access,error) ||
        !cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET,.name=name,.value=value,.force=true},error)) return false;
    if (qa_cvars_dialect(access.registry)!=QA_CONSOLE_Q3) return true;
    const qa_cvar_view *actual=NULL;
    if (!cvar_find(access,name,&actual,error)) return false;
    if (!actual) return qac_fail(error,QA_ERROR_NOT_FOUND,"startup value has no admitted physical cvar");
    uint64_t owner=actual->owner;
    return cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_REGISTER,
            .name=name,.value="",.owner=owner},error) &&
        cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_ADD_FLAGS,
            .name=name,.flags=QA_CVAR_USER_CREATED},error);
}

static size_t buffer_limit(const qa_console *console, const qa_command_context *context)
{
    const qa_console_options *options=options_for(console,context);
    return options && options->maximum_buffer != 0 ? options->maximum_buffer :
        context->dialect == QA_CONSOLE_Q3 ? 16384 : 8192;
}

bool qa_console_limits(qa_console *console, const qa_command_context *context,
                        size_t *maximum_command, size_t *maximum_buffer,
                        qa_error *error)
{
    if (console == NULL || maximum_command == NULL || maximum_buffer == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid console limit request");
    const qa_command_context *active = context_for(console, context);
    const qa_console_options *options=options_for(console,active);
    if (!options || !qac_dialect_valid(active->dialect))
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid console limit dialect");
    *maximum_command = options->maximum_command == 0 ? 1024 : options->maximum_command;
    *maximum_buffer = buffer_limit(console, active);
    return true;
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
    if (!qac_console_release_access(console,error)) return false;
    if (console == NULL || text == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid command buffer arguments");
    qa_command_context inherited = *context_for(console, context);
    if (!capture_context(console,&inherited,&inherited,error)) return false;
    if (context == NULL && console->frame != NULL) inherited.direct = false;
    context = &inherited;
    if (!valid_context(console, context, error)) return false;
    bool newline = insert && (context->dialect == QA_CONSOLE_QW || context->dialect == QA_CONSOLE_Q3);
    size_t length = strlen(text);
    size_t limit = buffer_limit(console, context);
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
    qac_console_program_touch(console, false);
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
        (options->context.cvar_view && options->context.cvar_view!=qa_cvars_view_identity(options->cvars)) ||
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

bool qa_console_bind_source(qa_console *console,const qa_console_options *options,qa_error *error)
{
    if (!console || !options || !qa_console_idle(console) || !options->cvars ||
        !options->context.cvar_view || options->context.script ||
        options->context.session!=console->options.context.session ||
        !qac_dialect_valid(options->context.dialect) ||
        options->context.origin<QA_COMMAND_LOCAL || options->context.origin>QA_COMMAND_REMOTE || options->maximum_command==1 ||
        options->context.cvar_view!=qa_cvars_view_identity(options->cvars) ||
        !qa_cvars_same_store(console->options.cvars,options->cvars) ||
        options->context.cvar_view==console->options.context.cvar_view)
        return qac_fail(error,QA_ERROR_ARGUMENT,"Source callbacks require their returned common console and exact cvar view");
    for (console_source *source=console->sources; source; source=source->next)
        if (source->options.context.cvar_view==options->context.cvar_view)
            return qac_fail(error,QA_ERROR_ARGUMENT,"Source callbacks are already bound to this cvar view");
    console_source *source=calloc(1,sizeof(*source));
    if (!source) return qac_fail(error,QA_ERROR_MEMORY,"retaining Source console callbacks");
    source->options=*options;
    if (options->startup_commands) {
        source->startup=qac_copy(options->startup_commands,error);
        if (!source->startup) { free(source); return false; }
    }
    source->options.startup_commands=source->startup;
    qac_console_program_touch(console,true);
    source->next=console->sources; console->sources=source;
    return true;
}

static void free_contribution(command_contribution *part)
{
    free((char *)part->view.description);
    qac_document_free(part->view.documentation);
    free(part);
}

static void free_command(command_entry *entry)
{
    free(entry->name);
    while (entry->contributions) {
        command_contribution *next = entry->contributions->next;
        free_contribution(entry->contributions); entry->contributions = next;
    }
    free(entry);
}

static command_entry *find_command(const qa_console *console, const char *name)
{
    for (command_entry *entry = console->commands; entry; entry = entry->next)
        if (qac_equal(entry->name, name)) return entry;
    return NULL;
}

static command_contribution *ordinary_command(const command_entry *entry, uint64_t receiver,
    const qa_command_context *context)
{
    for (command_contribution *part = entry ? entry->contributions : NULL; part; part = part->next)
        if (!part->retired && part->ordinary && part->view.owner == receiver &&
            (!context || part->cvar_view==context->cvar_view)) return part;
    return NULL;
}

static bool contribution_visible(const qa_console *console,const command_contribution *part, const qa_command_context *context)
{
    if (part->retired || (context->owner && part->view.owner && part->view.owner!=context->owner) ||
        (part->scoped && part->seat!=context->seat)) return false;
    if (!part->view.owner && part->view.engine_command &&
        (!part->cvar_view || part->cvar_view==console->options.context.cvar_view)) return true;
    if (!part->cvar_view || part->cvar_view==context->cvar_view) return true;
    if (context->owner) return false;
    const qa_console_options *options=options_for(console,&(qa_command_context){.cvar_view=part->cvar_view});
    if (!options) return false;
    qa_command_context receiver;
    qa_error error={0};
    return source_context_current(console,&options->context,&receiver,&error);
}

static command_contribution *select_contribution(const qa_console *console,const command_entry *entry,
    const qa_command_context *context)
{
    if (context->cvar_view && context->cvar_view!=console->options.context.cvar_view)
        for (command_contribution *part=entry ? entry->contributions : NULL; part; part=part->next)
            if (part->ordinary && part->view.engine_command && part->cvar_view==context->cvar_view &&
                contribution_visible(console,part,context)) return part;
    for (command_contribution *part=entry ? entry->contributions : NULL; part; part=part->next)
        if (part->ordinary && !part->view.owner && part->view.engine_command &&
            part->cvar_view==console->options.context.cvar_view &&
            contribution_visible(console,part,context)) return part;
    command_contribution *global=ordinary_command(entry,0,NULL);
    if (global && global->view.engine_command && contribution_visible(console,global,context)) return global;
    if (context->owner)
        for (command_contribution *part=entry ? entry->contributions : NULL; part; part=part->next)
            if (part->view.owner==context->owner && contribution_visible(console,part,context) &&
                (part->handler || part->callback)) return part;
    if (global && contribution_visible(console,global,context)) return global;
    for (command_contribution *part=entry ? entry->contributions : NULL; part; part=part->next)
        if (contribution_visible(console,part,context) && (part->handler || part->callback)) return part;
    for (command_contribution *part=entry ? entry->contributions : NULL; part; part=part->next)
        if (contribution_visible(console,part,context)) return part;
    return NULL;
}

/* An entered callback may retire declarations. Keep its name/list alive until
 * the invocation returns; module teardown already requires an idle console. */
static void collect_command(qa_console *console, command_entry *entry)
{
    if (entry->calls) return;
    command_contribution **part = &entry->contributions;
    while (*part) {
        if (!(*part)->retired) { part = &(*part)->next; continue; }
        command_contribution *removed = *part; *part = removed->next;
        free_contribution(removed);
    }
    if (entry->contributions) return;
    command_entry **link = &console->commands;
    while (*link && *link != entry) link = &(*link)->next;
    if (*link) { *link = entry->next; free_command(entry); }
}

static void free_alias(alias_entry *alias)
{
    free((char *)alias->view.name);
    free((char *)alias->view.alias_text);
    free(alias);
}

bool qa_console_destroy_ready(const qa_console *console)
{
    return console == NULL || (qa_console_idle(console) &&
        !console->program_leases && !console->release_leases);
}

void qa_console_destroy(qa_console *console)
{
    if (console == NULL || !qa_console_destroy_ready(console)) return;
    while (console->sources) {
        console_source *source=console->sources;
        console->sources=source->next;
        free(source->startup); free(source);
    }
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

qa_cvars *qa_console_cvars(const qa_console *console)
{
    return console ? console->options.cvars : NULL;
}

#define COMMAND(n, d, u) {.name = n, .description = d, .engine_command = true, \
    .documentation = &(const qa_console_documentation){.usage = u}}
static const qa_console_entry builtin_entries[] = {
    COMMAND("stuffcmds", "Execute startup commands", "stuffcmds"),
    COMMAND("exec", "Execute a content script", "exec <filename>"),
    COMMAND("echo", "Print console text", "echo <text>"),
    COMMAND("alias", "Define a command alias", "alias [name [commands]]"),
    COMMAND("cmd", "Forward a command to the server", "cmd <command>"),
    COMMAND("wait", "Pause queued commands", "wait [frames]"),
    COMMAND("cmdlist", "List console commands", "cmdlist [filter]"),
    COMMAND("set", "Set a console variable", "set <name> <value>"),
    COMMAND("cvarlist", "List visible console variables", "cvarlist [filter]"),
    COMMAND("toggle", "Toggle or cycle a variable", "toggle <name> [values...]"),
    COMMAND("sets", "Set a server-info variable", "sets <name> <value>"),
    COMMAND("setu", "Set a user-info variable", "setu <name> <value>"),
    COMMAND("seta", "Set an archived variable", "seta <name> <value>"),
    COMMAND("reset", "Restore a variable default", "reset <name>"),
    COMMAND("cvar_restart", "Restart the Q3 cvar registry", "cvar_restart"),
    COMMAND("vstr", "Execute a variable as commands", "vstr <name>"),
    COMMAND("inc", "Increase a numeric variable", "inc <name> [amount]"),
    COMMAND("dec", "Decrease a numeric variable", "dec <name> [amount]"),
    COMMAND("resetall", "Restore variable defaults", "resetall")
};
#undef COMMAND

static bool builtin_allowed(qa_console_dialect dialect, const char *name)
{
    if (strcmp(name, "alias") == 0) return dialect != QA_CONSOLE_Q3;
    if (strcmp(name, "stuffcmds") == 0) return qac_q1(dialect);
    if (strcmp(name, "cvar_restart") == 0) return dialect == QA_CONSOLE_Q3;
    return true;
}

static bool is_builtin(const qa_console *console,const qa_command_context *context,const char *name)
{
    context=context_for(console,context);
    const qa_console_options *options=options_for(console,context);
    if (!options || options->disable_builtins) return false;
    for (size_t i = 0; i < sizeof(builtin_entries) / sizeof(builtin_entries[0]); ++i)
        if (qac_equal(name, builtin_entries[i].name) &&
            builtin_allowed(context->dialect, builtin_entries[i].name)) return true;
    return false;
}

static bool register_command(qa_console *console,const qa_command_context *constructor,
    const char *name,const char *description,uint64_t receiver,uint64_t lifetime_owner,bool engine_command,
    qa_command_handler handler, void *user, qa_error *error)
{
    if (!qac_console_release_access(console,error)) return false;
    if (!console || !name || !*name || strpbrk(name, " \t\r\n;\"") ||
        retired(console->owners, receiver) || retired(console->owners, lifetime_owner))
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid command declaration");
    const qa_console_options *declaration_options=options_for(console,constructor);
    if (!declaration_options) return qac_fail(error,QA_ERROR_ARGUMENT,"command Source callbacks have retired");
    command_entry *entry = find_command(console, name);
    qa_command_context context;
    if (!constructor) constructor=console->cvar_scope ? &console->cvar_scope->source : context_for(console,NULL);
    if (!qa_console_cvar_context(console,constructor,&context,error)) return false;
    if (is_builtin(console,&context,name)) return qac_fail(error,QA_ERROR_ARGUMENT,"command name is a builtin");
    for (command_contribution *part=entry ? entry->contributions : NULL; part; part=part->next)
        if (!part->retired && part->ordinary && part->view.owner==receiver && part->cvar_view==context.cvar_view)
            return qac_fail(error,QA_ERROR_ARGUMENT,"command is already registered by this Source view");
    qa_cvars *registry = cvar_owner(console, &context, name);
    cvar_access access;
    if (!cvar_access_read(console,&context,registry,&access,error)) return false;
    const qa_cvar_view *variable=NULL;
    if (!cvar_find(access,name,&variable,error)) return false;
    if (context.dialect != QA_CONSOLE_Q3 && variable && *variable->value)
        return qac_fail(error, QA_ERROR_ARGUMENT, "command name is already a cvar");
    command_contribution *part = calloc(1, sizeof(*part));
    if (!part) return qac_fail(error, QA_ERROR_MEMORY, "retaining console command owner");
    part->view.description = qac_copy(description ? description : "", error);
    if (!part->view.description) { free(part); return false; }
    if (!entry) {
        entry = calloc(1, sizeof(*entry));
        if (!entry) { free_contribution(part); return qac_fail(error, QA_ERROR_MEMORY, "allocating console command name"); }
        entry->name = qac_copy(name, error);
        if (!entry->name) { free(entry); free_contribution(part); return false; }
        entry->next = console->commands; console->commands = entry;
    }
    part->view.name = entry->name;
    part->view.owner = receiver; part->view.engine_command = engine_command;
    part->lifetime_owner = lifetime_owner; part->cvar_view=context.cvar_view;
    part->handler = handler; part->user = user;
    part->ordinary = true; part->next = entry->contributions;
    qac_console_program_touch(console, true);
    entry->contributions = part;
    return true;
}

bool qa_console_register(qa_console *console, const char *name, const char *description,
    uint64_t owner, bool engine_command, qa_command_handler handler, void *user, qa_error *error)
{
    return register_command(console,NULL,name,description,owner,owner,engine_command,handler,user,error);
}

bool qa_console_register_owned(qa_console *console, const char *name, const char *description,
    uint64_t dispatch_owner, uint64_t lifetime_owner, bool engine_command,
    qa_command_handler handler, void *user, qa_error *error)
{
    return register_command(console,NULL,name,description,dispatch_owner,lifetime_owner,
        engine_command,handler,user,error);
}

bool qa_console_register_context(qa_console *console,const qa_command_context *context,
    const char *name,const char *description,uint64_t receiver,uint64_t lifetime_owner,
    bool engine_command,qa_command_handler handler,void *user,qa_error *error)
{
    if (!context) return qac_fail(error,QA_ERROR_ARGUMENT,"command registration requires its actual Source context");
    return register_command(console,context,name,description,receiver,lifetime_owner,engine_command,handler,user,error);
}

bool qa_console_registration_owner(const qa_console *console, const char *name,
    uint64_t dispatch_owner, uint64_t *out)
{
    if (!console || !name || !out) return false;
    const command_contribution *part = ordinary_command(find_command(console,name),dispatch_owner,
        &console->options.context);
    if (!part || !part->handler) return false;
    *out = part->lifetime_owner;
    return true;
}

bool qa_console_registration_read_context(const qa_console *console,const qa_command_context *context,
    const char *name,uint64_t dispatch_owner,uint64_t *lifetime_owner,qa_command_handler *handler,void **user)
{
    if (!console || !context || !name || !lifetime_owner || !handler || !user) return false;
    const command_contribution *part = ordinary_command(find_command(console,name),dispatch_owner,context);
    if (!part) return false;
    *lifetime_owner = part->lifetime_owner; *handler = part->handler; *user = part->user;
    return true;
}

bool qa_console_registration_read(const qa_console *console, const char *name,
    uint64_t dispatch_owner, uint64_t *lifetime_owner, qa_command_handler *handler, void **user)
{
    return console && qa_console_registration_read_context(console,&console->options.context,name,
        dispatch_owner,lifetime_owner,handler,user);
}

bool qa_console_unregister(qa_console *console, const char *name, uint64_t owner)
{
    return console && qa_console_unregister_context(console,&console->options.context,name,owner);
}

bool qa_console_unregister_context(qa_console *console,const qa_command_context *context,
    const char *name,uint64_t receiver)
{
    if (!console || !context || !name || !qac_console_release_access(console,NULL)) return false;
    command_entry *entry=find_command(console,name);
    for (command_contribution *part=entry ? entry->contributions : NULL; part; part=part->next)
        if (!part->retired && part->ordinary && part->view.owner==receiver &&
            part->cvar_view==context->cvar_view) {
            qac_console_program_touch(console,true); part->retired=true;
            collect_command(console,entry); return true;
        }
    return false;
}

bool qa_console_contribute(qa_console *console, const char *name,
    const qa_console_contribution *owner, qa_error *error)
{
    if (!qac_console_release_access(console,error)) return false;
    if (!console || !name || !*name || !owner || !owner->lifetime_owner ||
        strpbrk(name, " \t\r\n;\"") || retired(console->owners, owner->receiver) ||
        retired(console->owners, owner->lifetime_owner))
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid command contribution");
    const qa_console_options *options=options_for(console,&(qa_command_context){.cvar_view=owner->cvar_view});
    if (owner->cvar_view && !options)
        return qac_fail(error,QA_ERROR_ARGUMENT,"command contribution lost its actual Source view");
    if (is_builtin(console,owner->cvar_view ? &options->context : NULL,name))
        return qac_fail(error, QA_ERROR_ARGUMENT, "command name is a builtin");
    command_entry *entry = find_command(console,name);
    for (command_contribution *part = entry ? entry->contributions : NULL; part; part = part->next)
        if (!part->retired && !part->ordinary && part->view.owner == owner->receiver &&
            part->lifetime_owner == owner->lifetime_owner)
            return (part->seat == owner->seat && part->cvar_view==owner->cvar_view &&
                part->callback == owner->callback && part->user == owner->user) ||
                qac_fail(error,QA_ERROR_ARGUMENT,"command contribution changed its actual callable owner");
    command_contribution *part = calloc(1,sizeof(*part));
    if (!part) return qac_fail(error,QA_ERROR_MEMORY,"retaining command contribution");
    part->view.description = qac_copy("",error);
    if (!part->view.description) { free(part); return false; }
    if (!entry) {
        entry = calloc(1,sizeof(*entry));
        if (!entry) { free_contribution(part); return qac_fail(error,QA_ERROR_MEMORY,"allocating console command name"); }
        entry->name = qac_copy(name,error);
        if (!entry->name) { free(entry); free_contribution(part); return false; }
        entry->next = console->commands; console->commands = entry;
    }
    part->view.name = entry->name; part->view.owner = owner->receiver;
    part->lifetime_owner = owner->lifetime_owner; part->seat = owner->seat;
    part->cvar_view=owner->cvar_view;
    part->callback = owner->callback; part->user = owner->user; part->scoped = owner->callback != NULL;
    part->next = entry->contributions;
    qac_console_program_touch(console,true); entry->contributions = part;
    return true;
}

bool qa_console_uncontribute(qa_console *console, const char *name,
    uint64_t dispatch_owner, uint64_t lifetime_owner)
{
    if (!qac_console_release_access(console,NULL) || !console || !name) return false;
    command_entry *entry = find_command(console,name);
    for (command_contribution *part = entry ? entry->contributions : NULL; part; part = part->next)
        if (!part->retired && !part->ordinary && part->view.owner == dispatch_owner && part->lifetime_owner == lifetime_owner) {
            qac_console_program_touch(console,true); part->retired = true;
            collect_command(console,entry); return true;
        }
    return false;
}

bool qa_console_document(qa_console *console, const char *name, uint64_t owner,
    const qa_console_documentation *doc, qa_error *error)
{
    if (!qac_console_release_access(console,error)) return false;
    command_contribution *part = console && name ? ordinary_command(find_command(console,name),owner,NULL) : NULL;
    return part ? qac_document_replace(&part->view.documentation,doc,error) :
        qac_fail(error,QA_ERROR_NOT_FOUND,"command documentation owner not found");
}

const qa_console_entry *qa_console_entry_at(const qa_console *console, size_t ordinal)
{
    return qa_console_context_entry_at(console,NULL,ordinal);
}

const qa_console_entry *qa_console_context_entry_at(const qa_console *console,
    const qa_command_context *context, size_t ordinal)
{
    if (!console) return NULL;
    context = context_for(console,context);
    for (command_entry *entry = console->commands; entry; entry = entry->next) {
        command_contribution *part = select_contribution(console,entry,context);
        if (part && ordinal-- == 0) return &part->view;
    }
    const qa_console_options *options=options_for(console,context);
    if (options && !options->disable_builtins)
        for (size_t i=0; i<sizeof(builtin_entries)/sizeof(builtin_entries[0]); ++i)
            if (builtin_allowed(context->dialect,builtin_entries[i].name) && ordinal-- == 0) return &builtin_entries[i];
    return NULL;
}

bool qa_console_alias(qa_console *console, const qa_command_context *context,
                        const char *name, const char *text, qa_error *error)
{
    if (!qac_console_release_access(console,error)) return false;
    if (console == NULL || name == NULL || text == NULL || *name == '\0' || strlen(name) >= 32)
        return qac_fail(error, QA_ERROR_ARGUMENT, "invalid console alias name");
    context = context_for(console, context);
    if (!valid_context(console, context, error)) return false;
    if (context->dialect == QA_CONSOLE_Q3)
        return qac_fail(error, QA_ERROR_UNSUPPORTED, "Q3 uses vstr rather than command aliases");
    char *copy = qac_copy(text, error);
    if (copy == NULL) return false;
    for (alias_entry *alias = console->aliases; alias != NULL; alias = alias->next) {
        if (!qac_equal(alias->view.name,name)) continue;
        qac_console_program_touch(console, true);
        free((char *)alias->view.alias_text);
        alias->view.alias_text = copy;
        alias->view.owner=context->direct && (context->origin==QA_COMMAND_LOCAL || context->origin==QA_COMMAND_SEAT)
            ? 0 : context->owner;
        alias->dialect = context->dialect;
        alias->console_text = context->console_text;
        return true;
    }
    alias_entry *alias = calloc(1, sizeof(*alias));
    if (alias == NULL) { free(copy); return qac_fail(error, QA_ERROR_MEMORY, "allocating command alias"); }
    alias->view.name = qac_copy(name, error);
    if (alias->view.name == NULL) { free(copy); free(alias); return false; }
    alias->view.alias_text = copy;
    alias->view.owner=context->direct && (context->origin==QA_COMMAND_LOCAL || context->origin==QA_COMMAND_SEAT)
        ? 0 : context->owner;
    alias->dialect = context->dialect;
    alias->console_text = context->console_text;
    alias->next = console->aliases;
    qac_console_program_touch(console, true);
    console->aliases = alias;
    return true;
}

const qa_console_entry *qa_console_alias_at(const qa_console *console, uint64_t owner,
                                            size_t ordinal)
{
    if (console == NULL) return NULL;
    (void)owner;
    for (alias_entry *alias = console->aliases; alias != NULL; alias = alias->next)
        if (ordinal-- == 0) return &alias->view;
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
        cvar_access access;
        if (!cvar_access_read(console,context,registry,&access,error)) { free(name); free(text); return false; }
        const qa_cvar_view *variable=NULL;
        if (!cvar_find(access,name,&variable,error)) { free(name); free(text); return false; }
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

bool qa_console_expand_command(qa_console *console, const qa_command_context *context,
    const char *input, qa_buffer *out, qa_error *error)
{
    if (!console || !input || !out || out->data || out->size ||
        !qac_console_release_access(console, error))
        return qac_fail(error, QA_ERROR_ARGUMENT, "Source command expansion requires an empty owned output");
    qa_command_context source = *context_for(console, context);
    if (!capture_context(console,&source,&source,error)) return false;
    if (!valid_context(console, &source, error)) return false;
    char *expanded = NULL;
    if (qac_q2(source.dialect)) {
        qa_error expansion = {0};
        if (!expand_macros(console, &source, input, &expanded, &expansion)) {
            if (expansion.code == QA_ERROR_FORMAT) {
                output(console, &source, expansion.message);
                output(console, &source, ", discarded.\n");
                if (!console->release_owner) return true;
            }
            if (error) *error = expansion;
            return false;
        }
    } else expanded = qac_copy(input, error);
    if (!expanded) return false;
    *out = (qa_buffer){.data = (uint8_t *)expanded, .size = strlen(expanded) + 1};
    return true;
}

static qa_command_result fallback_call(qa_console *console, qa_command_fallback handler,
                                        const qa_command_invocation *command, qa_error *error)
{
    const qa_console_options *options=options_for(console,&command->context);
    return !options || handler == NULL ? QA_COMMAND_UNHANDLED : handler(options->user, command, error);
}

static bool cvar_command(qa_console *console, const qa_command_invocation *command,
                         bool *handled, qa_error *error)
{
    *handled = false;
    qa_cvars *registry = cvar_owner(console, &command->context, command->argv[0]);
    cvar_access access;
    if (!cvar_access_read(console,&command->context,registry,&access,error)) return false;
    const qa_cvar_view *variable=NULL;
    if (!cvar_find(access,command->argv[0],&variable,error)) return false;
    if (variable != NULL) {
        *handled = true;
        if (command->argc > 1) return cvar_apply(access,&(qa_cvars_edit_command){
            .kind=QA_CVARS_EDIT_SET,.name=variable->name,.value=command->argv[1]},error);
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
    return true;
}

static bool fallback(qa_console *console, const qa_command_invocation *command, qa_error *error)
{
    const qa_console_options *options=options_for(console,&command->context);
    if (!options) return qac_fail(error,QA_ERROR_ARGUMENT,"command Source callbacks have retired");
    bool handled;
    if (!cvar_command(console, command, &handled, error)) return false;
    if (handled) return true;
    if (command->context.dialect == QA_CONSOLE_Q3) {
        qa_command_fallback callbacks[] = {options->client_game, options->server_game, options->ui};
        for (size_t i = 0; i < sizeof(callbacks) / sizeof(callbacks[0]); ++i) {
            qa_command_result result = fallback_call(console, callbacks[i], command, error);
            if (result != QA_COMMAND_UNHANDLED) return result == QA_COMMAND_HANDLED;
        }
    }
    bool local = command->context.direct &&
        (command->context.origin == QA_COMMAND_LOCAL || command->context.origin == QA_COMMAND_SEAT);
    if (!qac_q1(command->context.dialect)) {
        qa_command_result result = fallback_call(console, options->forward, command, error);
        if (result != QA_COMMAND_UNHANDLED) return result == QA_COMMAND_HANDLED;
        if (!local) return true;
    }
    bool warn = local || command->context.dialect != QA_CONSOLE_QW;
    if (!warn) {
        const qa_cvar_view *warncmd=NULL,*developer=NULL;
        if (!qa_console_cvar_read(console,&command->context,"cl_warncmd",&warncmd,error) ||
            !qa_console_cvar_read(console,&command->context,"developer",&developer,error)) return false;
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
    command_entry *entry = find_command(console,name);
    return select_contribution(console,entry,context) ? entry : NULL;
}

const qa_console_entry *qa_console_find(const qa_console *console,
                                         const qa_command_context *context,
                                         const char *name)
{
    if (console == NULL || name == NULL) return NULL;
    context = context_for(console, context);
    const qa_console_options *options=options_for(console,context);
    if (options && !options->disable_builtins)
        for (size_t i = 0; i < sizeof(builtin_entries) / sizeof(builtin_entries[0]); ++i)
            if (qac_equal(name, builtin_entries[i].name) && builtin_allowed(context->dialect, builtin_entries[i].name))
                return &builtin_entries[i];
    const command_entry *entry = select_command(console, context, name);
    return entry == NULL ? NULL : &select_contribution(console,entry,context)->view;
}

static bool dispatch_continuation(qa_console *console,const qa_command_context *context,
    const qa_command_invocation *command,qa_error *error)
{
    const qa_console_options *options=options_for(console,context);
    if (!options) return qac_fail(error,QA_ERROR_ARGUMENT,"command Source callbacks have retired");
    qa_command_result result = fallback_call(console,options->source_command,command,error);
    if (result != QA_COMMAND_UNHANDLED) return result==QA_COMMAND_HANDLED;
    if (!valid_context(console,context,error)) return false;
    command_entry *entry=select_command(console,context,command->argv[0]);
    command_contribution *part=select_contribution(console,entry,context);
    bool success;
    if (part && qac_q1(context->dialect)) success=qac_fail(error,QA_ERROR_ARGUMENT,"Q1 command has no callback");
    else if (part && qac_q2(context->dialect)) {
        result=fallback_call(console,options->forward,command,error);
        success=result!=QA_COMMAND_FAILED;
    } else success=fallback(console,command,error);

    return success;
}

/* Keep actual module calls entered until their common continuation returns.
 * The stack follows the pinned owner list, including retirement and nested
 * console calls, without allocating another dispatch history. */
static bool dispatch_contributions(qa_console *console,const qa_command_context *context,
    qa_command_invocation *command,command_entry *entry,command_contribution *part,
    unsigned pass,qa_error *error)
{
    for (;;) {
        while (part && (!contribution_visible(console,part,context) || (!part->handler && !part->callback) ||
            (context->owner && part->view.owner!=(pass ? 0 : context->owner)))) part=part->next;
        if (part) break;
        if (!context->owner || pass) return dispatch_continuation(console,context,command,error);
        ++pass; part=entry->contributions;
    }
    command->receiver=part->view.owner; command->registration_owner=part->lifetime_owner;
    command_frame *frame=console->frame;
    command_call call={.receiver=part->view.owner,.lifetime_owner=part->lifetime_owner,
        .cvar_view=part->cvar_view,.parent=frame->contributions};
    frame->contributions=&call;
    bool success;
    if (part->handler) success=part->handler(part->user,command,error);
    else {
        qa_command_result result=part->callback(part->user,command,error);
        success=result==QA_COMMAND_HANDLED;
        if (result==QA_COMMAND_UNHANDLED) success=valid_context(console,context,error) &&
            dispatch_contributions(console,context,command,entry,part->next,pass,error);
    }
    frame->contributions=call.parent;
    return success;
}

static bool dispatch_inner(qa_console *console, const qa_command_context *context,
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
                if (console->release_owner) {
                    if (error && error->code==QA_OK) *error=expansion_error;
                    return false;
                }
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
        (const char *const *)tokens.values, tokens.args_text, raw, 0, 0};
    command.context.direct = context->direct && context->script == NULL &&
        (context->origin == QA_COMMAND_LOCAL || context->origin == QA_COMMAND_SEAT);
    command_frame frame = {.invocation=&command, .parent=console->frame};
    console->frame = &frame;
    if (console->release_owner && console->release_advancing)
        console->release_dispatch_context=&command.context;
    bool success = true, dispatched = false;
    const qa_console_options *options=options_for(console,context);
    if (!options) { success=qac_fail(error,QA_ERROR_ARGUMENT,"command Source callbacks have retired"); goto done; }
    if (options->allow_command != NULL && !options->allow_command(options->user, &command)) {
        if (console->release_owner)
            success=qac_fail(error,QA_ERROR_ARGUMENT,"source release command was refused");
        goto done;
    }
    if (!valid_context(console, context, error)) { success = false; goto done; }
    dispatched = true;
    qac_console_program_touch(console, false);
    qac_console_release_enter(console);
    bool handled = false;
    if (!options->disable_builtins) {
        success = builtin(console, &command, &handled, error);
        if (!success || handled) goto done;
    }
    command_entry *entry = select_command(console, context, tokens.values[0]);
    if (entry == NULL && context->dialect != QA_CONSOLE_Q3) {
        for (alias_entry *alias = console->aliases; alias != NULL; alias = alias->next) {
            if (!qac_equal(alias->view.name,tokens.values[0])) continue;
            if (qac_q2(context->dialect) && ++console->alias_count == 16) {
                output(console, context, "ALIAS_LOOP_COUNT\n");
                goto done;
            }
            qa_command_context derived = *context;
            derived.direct = false;
            success = qa_console_insert(console, &derived, alias->view.alias_text, error);
            goto done;
        }
    }
    command_contribution *part = select_contribution(console,entry,context);
    if (!part || (context->dialect == QA_CONSOLE_Q3 && !part->handler)) {
        success = cvar_command(console, &command, &handled, error);
        if (!success || handled) goto done;
    }
    if (entry && part) {
        if (context->dialect==QA_CONSOLE_Q3 && entry!=console->commands) {
            command_entry **link=&console->commands;
            while (*link!=entry) link=&(*link)->next;
            *link=entry->next; entry->next=console->commands; console->commands=entry;
        }
        ++entry->calls;
        if (part->handler) {
            command.receiver=part->view.owner; command.registration_owner=part->lifetime_owner;
            command_call call={.receiver=part->view.owner,.lifetime_owner=part->lifetime_owner,
                .cvar_view=part->cvar_view,.parent=frame.contributions};
            frame.contributions=&call;
            success=part->handler(part->user,&command,error);
            frame.contributions=call.parent;
        } else success=dispatch_contributions(console,context,&command,entry,entry->contributions,0,error);
        --entry->calls; collect_command(console,entry);
    } else success=dispatch_continuation(console,context,&command,error);

done:
    if (dispatched && options->post_dispatch) {
        qa_error observation={0};
        bool observed=options->post_dispatch(options->user,&command,success,&observation);
        if (!observed && success) {
            if (observation.code==QA_OK)
                qac_fail(&observation,QA_ERROR_ARGUMENT,"post-dispatch observation refused its invocation");
            if (error && error->code==QA_OK) *error=observation;
            success=false;
        }
    }
    console->frame = frame.parent;
    qa_command_tokens_free(&tokens);
    return success;
}

static bool dispatch(qa_console *console,const qa_command_context *context,
    const char *raw,qa_error *error)
{
    const qa_command_context *previous=console->release_dispatch_context;
    if (console->release_owner && console->release_advancing)
        console->release_dispatch_context=context;
    bool ok=dispatch_inner(console,context,raw,error);
    console->release_dispatch_context=previous;
    return ok;
}

bool qa_console_execute_now(qa_console *console, const qa_command_context *context,
                              const char *text, qa_error *error)
{
    if (!qac_console_release_access(console,error)) return false;
    if (console == NULL) return qac_fail(error, QA_ERROR_ARGUMENT, "console is NULL");
    if (!qac_console_program_immediate_allowed(console,error)) return false;
    if (text == NULL || *text == '\0') return qa_console_drain(console, 0, NULL, error);
    qa_command_context source = *context_for(console, context);
    if (!capture_context(console,&source,&source,error)) return false;
    qa_command_context inherited;
    if (!copy_context(&inherited, &source, error)) return false;
    if (console->frame != NULL) inherited.direct = false;
    char *raw = qac_copy(text, error);
    bool ok = raw != NULL && dispatch(console, &inherited, raw, error);
    free(raw);
    free((char *)inherited.script);
    return ok;
}

bool qa_console_execute_capture(qa_console *console, const qa_command_context *context,
                                  const char *text,
                                  void (*print)(void *, const qa_command_context *, const char *),
                                  void *user, qa_error *error)
{
    if (console == NULL || print == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "console output capture requires a sink");
    output_frame frame = {.print = print, .user = user, .parent = console->redirect};
    console->redirect = &frame;
    bool ok = qa_console_execute_now(console, context, text, error);
    console->redirect = frame.parent;
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
    if (bytes) qac_console_program_touch(console, false);
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
    if (!qac_console_release_access(console,error)) return false;
    if (executed != NULL) *executed = 0;
    if (console == NULL || console->draining || console->frame != NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "command buffer is already executing");
    if (!qac_console_program_drain_allowed(console,error)) return false;
    console->draining = true;
    if (console->alias_count) qac_console_program_touch(console, false);
    console->drain_yielded = false;
    console->alias_count = 0;
    size_t count = 0;
    bool success = true;
    if (console->head == NULL && console->wait != 0) {
        qac_console_program_touch(console, false);
        console->drain_yielded = true;
        console->wait = console->wait > 0 ? console->wait - 1 : console->wait == INT32_MIN ? INT32_MAX : console->wait - 1;
    }
    while (console->head != NULL && (budget == 0 || count < budget)) {
        if (console->wait_context.dialect == QA_CONSOLE_Q3 && console->wait != 0) {
            qac_console_program_touch(console, false);
            console->drain_yielded = true;
            if (console->wait > INT32_MIN) --console->wait;
            else console->wait = INT32_MAX;
            break;
        }
        command_chunk *first = console->head;
        if (first->completion) {
            qac_console_program_touch(console, false);
            console->head = first->next;
            if (console->tail == first) console->tail = NULL;
            qa_command_invocation invocation = {console, first->context, 0, NULL, "", "", 0, 0};
            command_frame frame = {.invocation=&invocation, .parent=console->frame};
            console->frame = &frame;
            const qa_console_options *options=options_for(console,&first->context);
            if (options && options->script_complete != NULL)
                options->script_complete(options->user, &first->context, first->context.script, first->success);
            console->frame = frame.parent;
            free_chunk(first);
            continue;
        }
        qa_command_context context;
        if (!copy_context(&context, &first->context, error)) { success = false; break; }
        qac_text text = {0};
        if (!command_text(first, &text, error)) { free((char *)context.script); free(text.data); success = false; break; }
        size_t offset = qa_command_separator(text.data, text.size, context.dialect);
        const qa_console_options *options=options_for(console,&context);
        if (!options) { success=qac_fail(error,QA_ERROR_ARGUMENT,"queued command cvar view has retired");
            free((char *)context.script); free(text.data); break; }
        size_t maximum = options->maximum_command == 0 ? 1024 : options->maximum_command;
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
            qac_console_program_touch(console, false);
            console->drain_yielded = true;
            console->wait = 0;
            break;
        }
    }
    console->draining = false;
    if (executed != NULL) *executed = count;
    return success;
}

bool qa_console_drain_yielded(const qa_console *console)
{
    return console != NULL && console->drain_yielded;
}

bool qa_console_defer(qa_console *console, qa_error *error)
{
    if (!qac_console_release_access(console,error)) return false;
    if (console == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "deferred command buffer requires a console");
    qac_console_program_touch(console, false);
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
    if (!qac_console_release_access(console,error)) return false;
    if (console == NULL)
        return qac_fail(error, QA_ERROR_ARGUMENT, "deferred command buffer requires a console");
    size_t limit = buffer_limit(console, context_for(console, NULL));
    if (console->queued_bytes > limit || console->deferred_bytes > limit - console->queued_bytes)
        return qac_fail(error, QA_ERROR_FORMAT, "resuming deferred commands overflows buffer");
    if (console->deferred == NULL) return true;
    qac_console_program_touch(console, false);
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

bool qa_console_unbind_source(qa_console *console,uint64_t view,qa_error *error)
{
    if (!console || !view || !qa_console_idle(console))
        return qac_fail(error,QA_ERROR_ARGUMENT,"Source callback retirement requires its returned common console");
    console_source **link=&console->sources;
    while (*link && (*link)->options.context.cvar_view!=view) link=&(*link)->next;
    if (!*link) return true;
    if (!qac_console_release_access(console,error)) return false;
    qac_console_program_touch(console,true);
    command_entry *entry=console->commands;
    while (entry) {
        command_entry *next=entry->next;
        for (command_contribution *part=entry->contributions; part; part=part->next)
            if (part->cvar_view==view) part->retired=true;
        collect_command(console,entry); entry=next;
    }
    console_source *source=*link; *link=source->next;
    free(source->startup); free(source);
    return true;
}

static bool remove_id(qa_console *console, uint64_t id, bool owner, qa_error *error)
{
    if (console && console->release_leases)
        return qac_fail(error,QA_ERROR_ARGUMENT,"source release retains its actual console lifetime");
    if (console == NULL || id == 0) return qac_fail(error, QA_ERROR_ARGUMENT, "cannot retire the engine or absent client");
    retired_id **list = owner ? &console->owners : &console->clients;
    if (retired(*list, id)) return true;
    retired_id *record = malloc(sizeof(*record));
    if (record == NULL) return qac_fail(error, QA_ERROR_MEMORY, "recording retired command owner");
    qac_console_program_touch(console, true);
    *record = (retired_id){id, *list};
    *list = record;
    discard_chunks(&console->head, &console->tail, &console->queued_bytes, id, owner);
    discard_chunks(&console->deferred, &console->deferred_tail, &console->deferred_bytes, id, owner);
    if ((owner ? console->wait_context.owner : console->wait_context.client) == id) {
        console->wait = 0;
        console->program_wait_pending = false;
        free((char *)console->wait_context.script);
        console->wait_context.script = NULL;
    }
    if (owner) {
        command_entry *current = console->commands;
        while (current) {
            command_entry *next = current->next;
            for (command_contribution *part=current->contributions; part; part=part->next)
                if (part->view.owner==id || part->lifetime_owner==id) part->retired=true;
            collect_command(console,current); current=next;
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
    const qa_console_options *options=options_for(console,&command->context);
    if (!options) { free(filename.data); return qac_fail(error,QA_ERROR_ARGUMENT,"script Source callbacks have retired"); }
    qa_bytes bytes = {0};
    void *lease = NULL;
    qa_error read_error = {0};
    bool found = options->read_script != NULL &&
        options->read_script(options->user, &command->context, filename.data, &bytes, &lease, &read_error);
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
        size_t limit = buffer_limit(console, &command->context);
        if (console->queued_bytes > limit || text->length > limit - console->queued_bytes) {
            qac_fail(error, QA_ERROR_FORMAT, "exec script overflows command buffer");
            goto fail;
        }
    }
    if (options->release_script != NULL) options->release_script(options->user, lease);
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
    size_t limit = buffer_limit(console, &command->context);
    if (text != NULL && (console->queued_bytes > limit || text->length > limit - console->queued_bytes)) {
        free_chunk(text);
        free_chunk(completion);
        return qac_fail(error, QA_ERROR_FORMAT, "exec script overflows command buffer after content callback");
    }
    completion->next = console->head;
    qac_console_program_touch(console, false);
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
    if (options->release_script != NULL) options->release_script(options->user, lease);
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
        cvar_access access;
        if (!cvar_access_read(console,context,registry,&access,error)) return false;
        size_t count;
        if (!cvar_count(access,&count,error)) return false;
        for (size_t n=0;n<count;++n) {
            const qa_cvar_view *variable=NULL;
            if (!cvar_at(access,n,&variable,error)) return false;
            if (cvar_owner(console, context, variable->name) != registry ||
                strcmp(variable->name, "game") == 0 || strcmp(variable->name, "fs_game") == 0) continue;
            qa_console_dialect dialect = qa_cvars_dialect(registry);
            uint32_t protected = qac_q2(dialect) ? QA_Q2_CVAR_NOSET | QA_Q2_CVAR_READONLY :
                dialect == QA_CONSOLE_Q3 ? QA_CVAR_READONLY | QA_CVAR_INIT | QA_CVAR_NO_RESTART : 0;
            if ((variable->flags & protected) != 0) continue;
            if (!cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_CONSOLE,
                .name=variable->name,.value=variable->reset_value},error)) return false;
        }
    }
    return true;
}

static bool builtin(qa_console *console, const qa_command_invocation *command,
                     bool *handled, qa_error *error)
{
    const qa_console_options *options=options_for(console,&command->context);
    if (!options) return qac_fail(error,QA_ERROR_ARGUMENT,"builtin Source callbacks have retired");
    const char *name = command->argv[0];
    const qa_command_context *context = &command->context;
    *handled = true;
    if (qac_equal(name, "wait")) {
        qa_command_context saved;
        if (!copy_context(&saved, context, error)) return false;
        qac_console_program_touch(console, false);
        free((char *)console->wait_context.script);
        console->wait_context = saved;
        console->program_wait_pending = false;
        console->wait = context->dialect == QA_CONSOLE_Q3 && command->argc == 2 ? qac_integer(command->argv[1]) : 1;
        return true;
    }
    if (qac_equal(name, "echo")) {
        for (size_t i = 1; i < command->argc; ++i) { output(console, context, command->argv[i]); output(console, context, " "); }
        output(console, context, "\n");
        return true;
    }
    if (qac_equal(name, "cmd")) return fallback_call(console, options->forward, command, error) != QA_COMMAND_FAILED;
    if (qac_equal(name, "exec")) return execute_script(console, command, error);
    if (qac_equal(name, "stuffcmds") && qac_q1(context->dialect))
        return options->startup_commands == NULL || qa_console_insert(console, context, options->startup_commands, error);
    if (qac_equal(name, "alias") && context->dialect != QA_CONSOLE_Q3) {
        if (command->argc == 1) {
            for (alias_entry *alias = console->aliases; alias != NULL; alias = alias->next)
                output_value(console,context,alias->view.name,alias->view.alias_text);
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
        const qa_cvar_view *variable=NULL;
        if (!qa_console_cvar_read(console,context,command->argv[1],&variable,error)) return false;
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
            cvar_access access;
            if (!cvar_access_read(console,context,registry,&access,error)) return false;
            if (qa_cvars_dialect(registry) == QA_CONSOLE_Q3 &&
                !cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_RESTART},error)) return false;
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
            if (!select_contribution(console,entry,context)) continue;
            if (pattern != NULL && !qa_command_filter(pattern, entry->name, false)) continue;
            output(console, context, entry->name); output(console, context, "\n");
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
            cvar_access access;
            if (!cvar_access_read(console,context,registry,&access,error)) return false;
            size_t extent,visible_count;
            if (!cvar_handles(access,&extent,error) || !cvar_count(access,&visible_count,error)) return false;
            handles+=extent;
            for (size_t n=0;n<visible_count;++n) {
                const qa_cvar_view *variable=NULL;
                if (!cvar_at(access,n,&variable,error)) return false;
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
    cvar_access access;
    if (!cvar_access_read(console,context,registry,&access,error)) return false;
    if (set || flagged) {
        if (command->argc < 3 || (set && qac_q2(context->dialect) && command->argc > 4) ||
            (flagged && context->dialect == QA_CONSOLE_Q3 && command->argc != 3)) {
            output(console, context, "set <variable> <value>\n"); return true;
        }
        if (set && qac_q2(context->dialect) && command->argc == 4) {
            if (strcmp(command->argv[3], "u") != 0 && strcmp(command->argv[3], "s") != 0) {
                output(console, context, "flags can only be 'u' or 's'\n"); return true;
            }
            return cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_FULL_SET,
                .name=variable_name,.value=command->argv[2],
                .flags=strcmp(command->argv[3], "u") == 0 ? QA_CVAR_USERINFO : QA_CVAR_SERVERINFO},error);
        }
        qac_text value = {0};
        bool ok = context->dialect == QA_CONSOLE_Q3 || flagged ? join_arguments(command, 2, &value, error) :
            qac_text_string(&value, command->argv[2], error);
        if (ok) {
            if (flagged) {
                uint32_t flag = qac_equal(name, "seta") ? QA_CVAR_ARCHIVE : qac_equal(name, "setu") ? QA_CVAR_USERINFO : QA_CVAR_SERVERINFO;
                ok = cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_FLAGS,
                    .name=variable_name,.value=value.data,.flags=flag},error);
            } else ok = cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET,
                .name=variable_name,.value=value.data},error);
        }
        free(value.data);
        return ok;
    }
    const qa_cvar_view *variable=NULL;
    if (!cvar_find(access,variable_name,&variable,error)) return false;
    if (toggle && context->dialect == QA_CONSOLE_Q3 && command->argc == 2) {
        float value = variable == NULL ? 0 : variable->number;
        return cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET,
            .name=variable_name,.value=truncf(value) == 0 ? "1" : "0"},error);
    }
    if (variable == NULL) return qac_fail(error, QA_ERROR_NOT_FOUND, "cvar is not registered");
    if (reset) {
        if (context->dialect == QA_CONSOLE_Q3 && command->argc != 2) { output(console, context, "reset <variable>\n"); return true; }
        return cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_CONSOLE,
            .name=variable_name,.value=variable->reset_value},error);
    }
    if (toggle) {
        if (command->argc == 2) {
            if (strcmp(variable->value, "0") != 0 && strcmp(variable->value, "1") != 0) {
                output(console, context, "toggle requires 0/1 or an explicit value list\n"); return true;
            }
            return cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_CONSOLE,
                .name=variable_name,.value=strcmp(variable->value, "0") == 0 ? "1" : "0"},error);
        }
        for (size_t i = 2; i < command->argc; ++i)
            if (qac_equal(command->argv[i], variable->value))
                return cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_CONSOLE,
                    .name=variable_name,.value=command->argv[i + 1 < command->argc ? i + 1 : 2]},error);
        output(console, context, "current value is outside toggle cycle\n");
        return true;
    }
    if (!decimal_text(variable->value)) { output(console, context, "increment requires a decimal cvar\n"); return true; }
    float amount = command->argc > 2 ? qac_number(command->argv[2], QA_CONSOLE_Q2) : 1;
    float value = variable->number + (qac_equal(name, "dec") ? -amount : amount);
    if (value == variable->number) return cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_CONSOLE,
        .name=variable_name,.value=variable->value},error);
    char formatted[64];
    unsigned digits = isfinite(value) && value - floorf(value) < 0.000001f ? 0 : 6;
    if (!qa_format_fixed(value, digits, formatted, sizeof(formatted), error)) return false;
    formatted[31] = '\0';
    return cvar_apply(access,&(qa_cvars_edit_command){.kind=QA_CVARS_EDIT_SET_CONSOLE,
        .name=variable_name,.value=formatted},error);
}

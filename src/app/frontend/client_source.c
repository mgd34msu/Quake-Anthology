#include "client_source.h"
#include "client_registry.h"
#include "tools_restore.h"
#include "save_commands.h"
#include "qa/console_cvar_observer.h"
#include "qa/source_save.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct frontend_client_source {
    frontend_client_source *next;
    qa_frontend *frontend;
    frontend_client_source_options options;
    qa_launch_instance_lease *metadata;
    frontend_client_registry *registry;
    qa_cvars *pending_cvars;
    qa_source_frame_time_binding frame_time;
    qa_console *console;
    qa_application_client_source application;
    qa_command_context command;
    qa_actor_owner receiver;
    uint64_t configuration_generation;
    size_t references;
    unsigned calls;
    bool constructing, ready, closing, app_attached, programme_retired, console_bound;
    bool retiring, retirement_entered;
    bool restore_finished;
    bool restored_constructor, restored_retiring;
    const frontend_client_source_state *constructor_state;
};
static bool linked(const frontend_client_source *s)
{
    for (const frontend_client_source *row = s && s->frontend ? s->frontend->client_sources : NULL;
        row; row = row->next) if (row == s) return true;
    return false;
}
static const qa_launch_instance *descriptor(const frontend_client_source *s)
{ return s ? qa_launch_instance_lease_view(s->metadata) : NULL; }
bool frontend_client_source_descriptor_equal(const qa_launch_instance *a, const qa_launch_instance *b)
{
    return a && b && a->storage == b->storage && a->state == b->state &&
        a->content == b->content && a->roles == b->roles && a->identity == b->identity;
}
static qa_cvars *registry(const frontend_client_source *s)
{ return s && s->registry ? frontend_client_registry_cvars(s->registry) : s ? s->pending_cvars : NULL; }
static bool console_tuple(const qa_command_context *command,const qa_command_context *physical)
{
    if(!command||!physical) return false;
    qa_command_context expected=*physical;
    if(command->origin==QA_COMMAND_REMOTE) {
        expected.origin=QA_COMMAND_REMOTE; expected.direct=false; expected.console_text=true;
    } else if(command->origin==QA_COMMAND_SEAT&&command->script) {
        expected.direct=false;
    }
    return qa_command_context_equal(command, &expected, QA_COMMAND_CONTEXT_IGNORE_SCRIPT);
}
static bool physical_current(void *,const qa_launch_instance *,qa_console *,qa_cvars *,const qa_command_context *);
bool frontend_client_source_retain(frontend_client_source *s, qa_error *error)
{
    if (!linked(s) || s->closing || s->retiring || s->frontend->resource_inventory || s->frontend->capture || s->references == SIZE_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT source callback owner is retiring");
    ++s->references; return true;
}
bool frontend_client_source_checkpoint_retain(frontend_client_source *s,
    const qa_application_client_source *source,qa_error *error)
{
    if(!linked(s)||s->closing||s->constructing||!s->app_attached||!source||
        (!s->frontend->capture&&!s->frontend->source_restoring)||s->frontend->resource_inventory||
        s->references==SIZE_MAX||!frontend_client_source_idle(s)||
        source->context.lifetime!=s->application.context.lifetime||
        !qa_application_client_associated(s->frontend->application,source)||
        !physical_current(s,source->descriptor,source->context.console,source->context.cvars,&source->context.command)||
        (!qa_application_client_current(s->frontend->application,source)&&
         !qa_application_client_retirement_current(s->frontend->application,source)))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT checkpoint borrow lost its actual physical custody");
    ++s->references; return true;
}
bool frontend_client_source_release(frontend_client_source *s, qa_error *error)
{
    if (!linked(s) || s->calls || s->frontend->resource_inventory || s->frontend->capture || s->references < 2)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT source callback lease is still entered");
    --s->references; return true;
}
static bool retain(void *context, qa_error *error)
{ return frontend_client_source_retain(context, error); }
static bool release(void *context, qa_error *error)
{ return frontend_client_source_release(context, error); }
static bool app_release(void *context, qa_error *error)
{
    frontend_client_source *s = context;
    if (!release(s, error)) return false;
    s->app_attached = false; return true;
}
static bool children_idle(const frontend_client_source *s)
{
    return linked(s) && !s->constructing && qa_console_idle(s->console) &&
        (!registry(s) || qa_cvars_observer_idle(registry(s)));
}
bool frontend_client_source_idle(const frontend_client_source *s)
{ return s && !s->calls && children_idle(s); }
static bool physical_current(void *context, const qa_launch_instance *d, qa_console *console,
    qa_cvars *cvars, const qa_command_context *command)
{
    frontend_client_source *s = context;
    const qa_launch_instance *actual = descriptor(s);
    return linked(s) && !s->closing && actual && d && d->storage == actual->storage &&
        d->content == actual->content && d->identity == actual->identity &&
        s->registry && frontend_client_registry_matches(s->registry, actual, s->options.metadata.seat) &&
        cvars == registry(s) && console == s->console && qa_command_context_equal(command, &s->command, 0);
}
static bool physical_idle(void *context)
{ return frontend_client_source_idle(context); }
static bool connection_current(void *context, const qa_application_client_source *source)
{
    frontend_client_source *s = context;
    if (!linked(s) || s->closing || s->calls == UINT_MAX) return false;
    ++s->calls;
    bool ok = s->options.connection_current(s->options.context, source);
    --s->calls; return ok;
}
static bool retirement_custody(void *context,const qa_application_client_source *source)
{
    frontend_client_source *s=context;
    if(!linked(s)||s->closing||s->calls==UINT_MAX||!s->options.retirement_current) return false;
    ++s->calls;
    bool held=s->options.retirement_current(s->options.context,source);
    --s->calls; return held;
}
static bool entity_current(void *context, const qa_application_client_source *source,
    uint32_t number, uint64_t *generation)
{
    frontend_client_source *s = context;
    if (!linked(s) || s->closing || s->calls == UINT_MAX || !s->options.entity_current) return false;
    ++s->calls;
    bool ok = s->options.entity_current(s->options.context, source, number, generation);
    --s->calls; return ok;
}
static bool active(void *context, const qa_command_context *command)
{
    frontend_client_source *s = context;
    return linked(s) && !s->closing && (!s->retiring||s->retirement_entered) && !s->frontend->resource_inventory && !s->frontend->capture &&
        !s->frontend->source_restoring &&
        s->calls != UINT_MAX && console_tuple(command, &s->command) &&
        qa_application_command_context_active(s->frontend->application, command);
}
static bool capture(void *context, const qa_command_context *command,
    qa_command_context *out, qa_error *error)
{
    frontend_client_source *s = context;
    if (!out || !active(s, command))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT console caller left its physical namespace");
    return qa_application_capture_command_context(s->frontend->application, command, out, error);
}
static void print(void *context, const qa_command_context *command, const char *text)
{
    frontend_client_source *s = context;
    if (!linked(s) || s->closing || (s->retiring&&!s->retirement_entered) || !console_tuple(command, &s->command) || s->calls == UINT_MAX) return;
    ++s->calls; s->options.print(s->options.context, command, text); --s->calls;
}
static void cvar_print(void *context, const char *text)
{ frontend_client_source *s = context; print(s, &s->command, text); }
static bool script(void *context, const qa_command_context *command, const char *path,
    qa_bytes *out, void **lease, qa_error *error)
{
    frontend_client_source *s = context;
    if (!out || !lease || !active(s, command)) return false;
    if (s->options.read_script) {
        ++s->calls;
        bool ok = s->options.read_script(s->options.context, command, path, out, lease, error);
        --s->calls; return ok;
    }
    qa_resource *resource = NULL;
    ++s->calls;
    bool ok = qa_vfs_acquire(descriptor(s)->content, path, &resource, NULL, error);
    --s->calls;
    if (!ok) return false;
    *out = qa_resource_bytes(resource); *lease = resource; return true;
}
static void script_release(void *context, void *lease)
{
    frontend_client_source *s = context;
    if (!s->options.read_script) { qa_resource_release(lease); return; }
    ++s->calls; s->options.release_script(s->options.context, lease); --s->calls;
}
static void script_complete(void *context, const qa_command_context *command, const char *path, bool success)
{
    frontend_client_source *s = context;
    ++s->calls; s->options.script_complete(s->options.context, command, path, success); --s->calls;
}
static bool allow(void *context, const qa_command_invocation *invocation)
{
    frontend_client_source *s = context;
    if (!active(s, &invocation->context)) return false;
    ++s->calls; bool ok = s->options.allow_command(s->options.context, invocation); --s->calls; return ok;
}
static qa_command_result command(void *context, const qa_command_invocation *invocation, qa_error *error)
{
    frontend_client_source *s = context;
    if (!active(s, &invocation->context)) return QA_COMMAND_FAILED;
    if (!s->options.command) return QA_COMMAND_UNHANDLED;
    ++s->calls; qa_command_result result = s->options.command(s->options.context, invocation, error); --s->calls;
    return result;
}
static qa_command_result forward(void *context, const qa_command_invocation *invocation, qa_error *error)
{
    frontend_client_source *s = context;
    bool engine=invocation && linked(s) && !s->closing && !s->retiring && !s->constructing && s->ready &&
        s->app_attached && !s->frontend->resource_inventory && !s->frontend->capture &&
        !s->frontend->source_restoring && s->calls!=UINT_MAX && !invocation->context.owner &&
        (invocation->context.origin==QA_COMMAND_LOCAL || invocation->context.origin==QA_COMMAND_SEAT) &&
        invocation->context.cvar_view==qa_cvars_view_identity(qa_application_cvars(s->frontend->application)) &&
        qa_console_invocation_current(s->console,invocation) &&
        qa_console_invocation_delivered_view(invocation,s->command.cvar_view,s->receiver,s->receiver) &&
        qa_application_command_context_active(s->frontend->application,&invocation->context) &&
        qa_application_client_current(s->frontend->application,&s->application);
    if (!invocation || (!active(s, &invocation->context) && !engine)) return QA_COMMAND_FAILED;
    if (!s->options.forward || !s->application.client.owner) return QA_COMMAND_UNHANDLED;
    if (!connection_current(s, &s->application))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT forwarding lost its actual connection"), QA_COMMAND_FAILED;
    ++s->calls; qa_command_result result = s->options.forward(s->options.context, invocation, error); --s->calls;
    return result;
}
static qa_application_client_options app_options(frontend_client_source *s)
{
    return (qa_application_client_options){.descriptor = descriptor(s), .receiver = s->receiver,
        .seat = s->options.metadata.seat, .physical_seat = s->options.physical_seat,
        .configuration_generation = s->configuration_generation, .runtime = s->options.runtime,
        .console = s->console, .cvars = registry(s), .command = s->command,
        .owner = {.context = s, .retain = retain, .release = app_release,
            .current = physical_current, .idle = physical_idle, .connection_current = connection_current,
            .entity_current = s->options.entity_current ? entity_current : NULL,
            .retirement_current=s->options.retirement_current?retirement_custody:NULL}};
}
static uint32_t capabilities(const frontend_client_source_options *o)
{
    return (o->initialize ? 1u : 0u) | (o->configure ? 2u : 0u) | (o->print ? 4u : 0u) |
        (o->connection_current ? 8u : 0u) | (o->entity_current ? 16u : 0u) |
        (o->command ? 32u : 0u) | (o->forward ? 64u : 0u) | (o->allow_command ? 128u : 0u) |
        (o->install ? 2048u : 0u) | (o->read_script ? 4096u : 0u) |
        (o->release_script ? 8192u : 0u) | (o->script_complete ? 16384u : 0u) |
        (o->retire ? 32768u : 0u) | (o->released ? 65536u : 0u) |
        (o->configuration_advance ? 131072u : 0u) | (o->retirement_current ? 262144u : 0u);
}
static bool capabilities_match(const frontend_client_source_options *options, uint32_t saved)
{
    return (saved & ~(256u | 512u | 1024u)) == capabilities(options);
}
static bool install_current(void *context, const qa_console *console,
    const qa_command_context *command, qa_error *error)
{
    frontend_client_source *s = context;
    if (!linked(s) || !s->constructing || !s->app_attached ||
        !s->calls || s->calls == UINT_MAX ||
        s->frontend->capture || s->frontend->resource_inventory || console != s->console ||
        !physical_current(s, s->application.descriptor, s->console, registry(s), command) ||
        !qa_application_client_associated(s->frontend->application, &s->application) ||
        !qa_application_command_context_active(s->frontend->application, command) ||
        (!qa_application_client_current(s->frontend->application, &s->application) &&
            !qa_application_client_retirement_current(s->frontend->application, &s->application)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT handler install lost its entered physical constructor");
    return true;
}
static bool install_handlers(void *context, const qa_command_context *command, qa_error *error)
{
    frontend_client_source *s = context;
    (void)command;
    return s->options.install(s->options.context, &s->application, s->restored_constructor, error) &&
        (!s->restored_constructor || !s->frontend->tools || frontend_tools_attach_restored(s->frontend, error));
}
static bool construct(qa_frontend *f, const frontend_client_source_options *options,
    const qa_launch_restored_instance *metadata, const frontend_client_source_state *state,
    const qa_console_save_resolvers *resolvers, frontend_client_source **out, qa_error *error)
{
    if (!f || !f->application || f->capture || f->resource_inventory ||
        (state ? !f->source_restoring : f->source_restoring) || !options || !out || *out ||
        !options->runtime || options->physical_seat >= f->options.seats ||
        !options->print || !options->connection_current ||
        (!state && (!options->initialize || !options->configure)) ||
        ((options->read_script != NULL) != (options->release_script != NULL)) ||
        (state && (!metadata || !resolvers || !capabilities_match(options, state->capabilities))) ||
        options->input_origin.script || options->input_origin.actor.registry ||
        options->input_origin.actor.generation || options->input_origin.actor.slot)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT source requires its actual metadata, input and transport owners");
    frontend_client_source *s = calloc(1, sizeof(*s));
    if (!s) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating physical CLIENT source");
    s->frontend = f; s->options = *options; s->references = 1; s->constructing = true;
    frontend_client_source **link = &f->client_sources;
    while (*link) link = &(*link)->next;
    *link = s; *out = s;
    bool success = false;
    bool ok = state ? qa_launch_instance_restore_client_profile(&options->metadata, metadata, &s->metadata, error) :
        qa_launch_instance_prepare_client_profile(&options->metadata, &s->metadata, error);
    if (!ok) goto done;
    s->options.metadata.instance = descriptor(s)->selection.instance;
    s->options.metadata.prepared = descriptor(s)->content;
    if (!qa_application_client_provider_prepare(f->application, descriptor(s), &s->receiver, error)) goto done;
    if (state) {
        s->command = state->command; s->configuration_generation = state->application.configuration_generation;
        if (resolvers->command_context && !resolvers->command_context(resolvers->context,
            state->command.registry, &state->command, &s->command, error)) goto done;
        if (s->receiver != state->application.receiver || s->command.owner != s->receiver ||
            s->command.seat != options->metadata.seat || s->command.script || s->command.actor.registry ||
            s->command.actor.generation || s->command.actor.slot) {
            frontend_fail(error, QA_ERROR_FORMAT, "Restored CLIENT command differs from its imported physical namespace"); goto done;
        }
    } else if (!qa_application_client_provider_command(f->application, s->receiver, options->metadata.seat,
        &options->input_origin, &s->command, &s->configuration_generation, error)) goto done;
    qa_cvar_options variables = {.dialect = s->command.dialect, .side = QA_CVAR_SIDE_CLIENT,
        .role = QA_CVAR_ROLE_CGAME, .seat = options->metadata.seat, .user = s, .print = cvar_print,
        .default_save_policy = QA_CVAR_SAVE_SETTING};
    s->pending_cvars = qa_cvars_create_view(qa_application_cvars(f->application), &variables, error);
    if (!s->pending_cvars) goto done;
    s->command.cvar_view = qa_cvars_view_identity(s->pending_cvars);
    if (state && !f->client_registry_import) {
        frontend_fail(error, QA_ERROR_FORMAT, "Restored CLIENT requires its genuine QFCR roster"); goto done;
    }
    frontend_client_registry_context callback = {s, retain, release, false};
    if (!frontend_client_registry_create(f, descriptor(s), options->metadata.seat,
        &s->pending_cvars, &callback, &s->registry, error)) goto done;
    qa_console_options console = {.context = s->command, .cvars = registry(s), .user = s,
        .print = print, .read_script = script, .release_script = script_release,
        .script_complete = options->script_complete ? script_complete : NULL,
        .allow_command = options->allow_command ? allow : NULL, .source_command = command, .forward = forward,
        .capture_context = capture, .context_active = active};
    s->console = qa_application_console(f->application);
    if (!s->console || !qa_console_bind_source(s->console, &console, error)) goto done;
    s->console_bound = true;
    if (!qa_application_command_context_active(f->application, &s->command)) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT constructor lost its actual bound view"); goto done;
    }
    if (options->initialize) {
        ++s->calls;
        ok = options->initialize(options->context, descriptor(s), registry(s), &s->command, error);
        --s->calls;
        if (!ok) goto done;
    }
    qa_source_frame_time_bind(registry(s),&s->frame_time);
    qa_application_client_options actual = app_options(s);
    s->constructor_state=state;
    ok = state ? qa_application_client_create_restored(f->application, &actual, &state->application, &s->application, error) :
        qa_application_client_create(f->application, &actual, &s->application, error);
    s->constructor_state=NULL;
    if (!ok) goto done;
    s->app_attached = true;
    s->restored_constructor=state!=NULL;
    s->restored_retiring=state&&state->retiring;
    if(state&&state->retiring&&!qa_application_client_retirement_current(f->application,&s->application)) {
        frontend_fail(error,QA_ERROR_ARGUMENT,"Restored CLIENT retirement lacks its actual disconnect custody"); goto done;
    }
    if (options->install) {
        ++s->calls;
        ok = qa_console_cvar_enter(s->console, &s->command, install_current, s,
            install_handlers, s, error);
        --s->calls;
        if (!ok) goto done;
    }
    if (state) {
        s->ready=state->ready; s->retiring=state->retiring;
        s->programme_retired=state->programme_retired;
        success = true;
    } else {
        s->constructing = false;
        success = frontend_client_source_advance(s, &s->ready, error);
    }
done:
    s->constructing = false;
    return success;
}
bool frontend_client_source_create(qa_frontend *f, const frontend_client_source_options *options,
    frontend_client_source **out, qa_error *error)
{ return construct(f, options, NULL, NULL, NULL, out, error); }
bool frontend_client_source_restore(qa_frontend *f, const frontend_client_source_options *options,
    const qa_launch_restored_instance *metadata, const frontend_client_source_state *state,
    const qa_console_save_resolvers *resolvers, frontend_client_source **out, qa_error *error)
{
    if (!state) return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT restore requires its real private prefix");
    return construct(f, options, metadata, state, resolvers, out, error);
}
bool frontend_client_source_advance(frontend_client_source *s, bool *ready, qa_error *error)
{
    if (!ready || !frontend_client_source_idle(s) || s->closing || s->retiring || !s->app_attached ||
        s->frontend->resource_inventory || s->frontend->capture || s->frontend->source_restoring)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT configuration requires its returned physical constructor");
    if (!s->ready) {
        if (s->frontend->stepping) { *ready = false; return true; }
        if (!s->options.configure) return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT programme has no retained configuration owner");
        bool complete = false;
        ++s->calls; bool ok = s->options.configure(s->options.context, &s->application, &complete, error); --s->calls;
        if (!ok) return false;
        if (!qa_application_client_current(s->frontend->application, &s->application))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT configuration changed its physical receiver");
        s->ready = complete;
    }
    *ready = s->ready; return true;
}
bool frontend_client_source_configuration_advance(frontend_client_source *s,
    qa_application_client_preparation *token,bool *complete,qa_error *e)
{
    const qa_application_client_source *held=qa_application_client_prepare_source(token);
    if(!s||!complete||!held||!frontend_client_source_idle(s)||s->closing||s->retiring||!s->app_attached||
        s->frontend->resource_inventory||s->frontend->capture||s->frontend->source_restoring||
        qa_application_client_prepare_application(token)!=s->frontend->application||
        !qa_application_client_prepare_entered(token,QA_CLIENT_PREPARE_CONFIGURATION)||
        held->context.lifetime!=s->application.context.lifetime||held->context.receiver!=s->application.context.receiver||
        held->context.seat!=s->application.context.seat||
        !physical_current(s,held->descriptor,held->context.console,held->context.cvars,&held->context.command)||
        !qa_application_client_current(s->frontend->application,&s->application)||
        !qa_application_client_current(s->frontend->application,held))
        return frontend_fail(e,QA_ERROR_ARGUMENT,"CLIENT programme leaves its entered physical configuration token");
    if(!s->ready) {
        if(!s->options.configuration_advance) return frontend_fail(e,QA_ERROR_ARGUMENT,"CLIENT programme has no retained configuration phase driver");
        bool ready=false; ++s->calls;
        bool ok=s->options.configuration_advance(s->options.context,&s->application,token,&ready,e);
        --s->calls;
        if(!ok) return false;
        if(!qa_application_client_prepare_entered(token,QA_CLIENT_PREPARE_CONFIGURATION)||
            !qa_application_client_current(s->frontend->application,&s->application)||
            !qa_application_client_current(s->frontend->application,held))
            return frontend_fail(e,QA_ERROR_ARGUMENT,"CLIENT programme changed its retained configuration token");
        s->ready=ready;
    }
    *complete=s->ready; return true;
}
bool frontend_client_source_read(const frontend_client_source *s, frontend_client_source_view *out, qa_error *error)
{
    if (!out || !linked(s) || s->closing || s->constructing || !s->app_attached ||
        !qa_application_client_current(s->frontend->application, &s->application))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT source no longer owns its physical graph");
    *out = (frontend_client_source_view){s, s->application, s->ready, &s->frame_time}; return true;
}
bool frontend_client_source_metadata_read(const frontend_client_source *s,
    frontend_client_source_view *out, qa_error *error)
{
    if (!out || !linked(s) || s->constructing || !s->app_attached ||
        !physical_current((void *)s, s->application.descriptor, s->application.context.console,
            s->application.context.cvars, &s->application.context.command))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT topology leaves its retained physical constructor");
    *out = (frontend_client_source_view){s, s->application, s->ready, &s->frame_time}; return true;
}
bool frontend_client_source_preinstall_current(const frontend_client_source *s,
    const qa_application_client_source *source,qa_error *error)
{
    const frontend_client_source_state *state=s?s->constructor_state:NULL;
    if(!linked(s)||!s->constructing||s->app_attached||!s->calls||!state||!source||
        !source->context.lifetime||source->context.session!=qa_application_session(s->frontend->application)||
        source->context.receiver!=s->receiver||source->context.entity_owner!=state->application.entity_owner||
        !source->context.entity_definition||source->context.seat!=s->options.metadata.seat||
        source->context.physical_seat!=s->options.physical_seat||source->runtime!=s->options.runtime||
        source->configuration_generation!=s->configuration_generation||
        !qa_net_client_id_equal(source->client,state->application.client)||
        source->network_seat.owner!=state->application.network_seat.owner||
        source->network_seat.index!=state->application.network_seat.index||
        source->connection_epoch!=state->application.connection_epoch||
        !physical_current((void *)s,source->descriptor,source->context.console,
            source->context.cvars,&source->context.command))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT candidate leaves its entered restored physical constructor");
    return true;
}
bool frontend_client_source_current(const frontend_client_source_view *view)
{
    frontend_client_source_view actual; qa_error error = {0};
    return view && frontend_client_source_read(view->owner, &actual, &error) && actual.ready == view->ready &&
        qa_application_client_current(view->owner->frontend->application, &view->source);
}
bool frontend_client_source_bind(frontend_client_source *s, qa_net_client_id client,
    qa_net_seat_id seat, uint64_t epoch, qa_error *error)
{
    if (!frontend_client_source_idle(s) || s->closing || s->retiring || !s->ready || !s->app_attached || s->application.client.owner ||
        s->frontend->resource_inventory || s->frontend->capture || s->frontend->source_restoring)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT bind requires its completed pending constructor");
    qa_application_client_source actual;
    if (!qa_application_client_bind(s->frontend->application, &s->application, client, seat, epoch, &actual, error)) return false;
    s->application = actual; return true;
}
bool frontend_client_source_epoch_adopt(frontend_client_source *s,uint64_t epoch,qa_error *error)
{
    if(!frontend_client_source_idle(s)||s->closing||s->retiring||!s->app_attached||
        s->frontend->resource_inventory||s->frontend->capture||s->frontend->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT epoch adoption requires its returned installed source");
    qa_application_client_source actual;
    if(!qa_application_client_epoch_adopt(s->frontend->application,&s->application,epoch,&actual,error)) return false;
    s->application=actual; return true;
}
bool frontend_client_source_drain(frontend_client_source *s, size_t budget, size_t *executed, qa_error *error)
{
    if (!frontend_client_source_idle(s) || s->closing || s->retiring || !s->app_attached ||
        s->frontend->resource_inventory || s->frontend->capture || s->frontend->source_restoring) return false;
    ++s->calls; bool ok = qa_console_drain(s->console, budget, executed, error); --s->calls; return ok;
}
bool frontend_client_source_remote(frontend_client_source *s,const qa_application_client_source *source,
    const char *text,qa_error *error)
{
    if(!linked(s)||s->closing||s->retiring||!s->app_attached||!s->ready||!source||!text||s->calls==UINT_MAX||
        s->frontend->resource_inventory||s->frontend->capture||s->frontend->source_restoring||
        source->context.receiver!=s->receiver||source->context.seat!=s->options.metadata.seat||
        source->context.console!=s->console||source->context.cvars!=registry(s)||
        !qa_application_client_current(s->frontend->application,source))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Remote command left its actual received CLIENT owner");
    qa_command_context command=s->command;
    command.origin=QA_COMMAND_REMOTE; command.direct=false; command.console_text=true; command.script=0;
    ++s->calls;
    bool ok=qa_console_append(s->console,&command,text,error)&&
        qa_application_client_current(s->frontend->application,source);
    --s->calls; return ok;
}
bool frontend_client_source_retirement_current(const frontend_client_source *s,
    const qa_application_client_source *source,const qa_console *console,
    const qa_command_context *command,qa_error *error)
{
    qa_command_context expected=s?s->command:(qa_command_context){0};
    if(command&&command->script&&!strcmp(command->script,"key-binding")&&!command->direct)
        expected.direct=false;
    if(!linked(s)||!s->retiring||!s->retirement_entered||!s->calls||s->closing||
        !s->app_attached||!source||console!=s->console||!console_tuple(command,&expected)||
        !qa_application_client_associated(s->frontend->application,source)||
        source->context.receiver!=s->receiver||source->context.seat!=s->options.metadata.seat||
        source->context.console!=s->console||source->context.cvars!=registry(s)||
        !physical_current((void *)s,source->descriptor,s->console,registry(s),&source->context.command))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT retirement left its entered physical source lifetime");
    return true;
}
bool frontend_client_sources_retirement_current(const qa_frontend *f,
    const qa_application_client_source *source,const qa_console *console,
    const qa_command_context *command,qa_error *error)
{
    if(f&&source) for(const frontend_client_source *s=f->client_sources;s;s=s->next)
        if(s->receiver==source->context.receiver&&s->options.metadata.seat==source->context.seat&&s->console==console)
            return frontend_client_source_retirement_current(s,source,console,command,error);
    return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT retirement has no retained physical source inventory row");
}
bool frontend_client_sources_restore_abort_ready(const qa_frontend *f,
    const qa_application_client_source *source,qa_error *error)
{
    if(f&&f->source_restoring&&source) for(const frontend_client_source *s=f->client_sources;s;s=s->next)
        if(s->receiver==source->context.receiver&&s->options.metadata.seat==source->context.seat&&
            !s->restore_finished&&s->restored_constructor&&s->restored_retiring)
            return frontend_client_source_retirement_current(s,source,source->context.console,
                &source->context.command,error);
    return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT candidate has no actual unclaimed import abort receipt");
}
bool frontend_client_source_destroy(frontend_client_source **owned, qa_error *error)
{
    frontend_client_source *s = owned ? *owned : NULL;
    if (!s) return true;
    size_t expected = 1u + (s->registry != NULL ? 1u : 0u) + (s->app_attached ? 1u : 0u);
    if (!frontend_client_source_idle(s) || s->frontend->capture || s->frontend->resource_inventory ||
        s->references != expected ||
        (s->app_attached && !qa_application_client_idle(s->frontend->application, &s->application)))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT source retains an entered or borrowed physical child");
    if(!s->programme_retired) {
        s->retiring=true;
        if(s->options.retire) {
            ++s->calls;s->retirement_entered=true;
            bool retired=s->options.retire(s->options.context,&s->application,error);
            s->retirement_entered=false;--s->calls;
            if(!retired) return false;
        }
        s->programme_retired=true;
    }
    if (!qa_console_idle(s->console) || !frontend_client_registry_release_ready(s->registry, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT programme retains a physical console or registry lease");
    if (s->console_bound && !qa_console_unbind_source(s->console, s->command.cvar_view, error)) return false;
    s->console_bound = false;
    s->closing = true;
    if (s->app_attached && !qa_application_client_retire(s->frontend->application, &s->application, error)) return false;
    s->console = NULL;
    if (!frontend_client_registry_retire(&s->registry, error)) return false;
    qa_cvars_destroy(s->pending_cvars); s->pending_cvars = NULL;
    if (s->receiver && !qa_application_client_provider_release(s->frontend->application, s->receiver, error)) return false;
    s->receiver = 0;
    qa_launch_instance_lease_release(s->metadata); s->metadata = NULL;
    if(s->options.released) s->options.released(s->options.context);
    frontend_client_source **link = &s->frontend->client_sources;
    while (*link != s) link = &(*link)->next;
    *link = s->next; free(s); *owned = NULL; return true;
}
static bool sources_returned(const qa_frontend *f,const qa_application_client_preparation *client)
{
    const qa_application_client_source *held=NULL;
    if (client) {
        if (!f || qa_application_client_prepare_application(client)!=f->application ||
            !qa_application_client_prepare_entered(client,QA_CLIENT_PREPARE_RESOURCES) ||
            !(held=qa_application_client_prepare_source(client))) return false;
    }
    bool found=!client;
    for (const frontend_client_source *s=f?f->client_sources:NULL;s;s=s->next) {
        if (held && s->application.context.lifetime==held->context.lifetime) {
            if (found || s->calls!=1 || s->closing || s->retiring || !s->app_attached ||
                !children_idle(s) ||
                !physical_current((void *)s,held->descriptor,held->context.console,
                    held->context.cvars,&held->context.command) ||
                !qa_application_client_current(f->application,&s->application) ||
                !qa_application_client_current(f->application,held)) return false;
            found=true;
        } else if (!frontend_client_source_idle(s)) return false;
    }
    return found;
}
bool frontend_client_sources_idle(const qa_frontend *f)
{ return sources_returned(f,NULL); }
bool frontend_client_sources_resources_returned(const qa_frontend *f,
    const qa_application_client_preparation *client)
{ return client && sources_returned(f,client); }
bool frontend_client_sources_destroy(qa_frontend *f, qa_error *error)
{
    if (!f) return true;
    while (f->client_sources) if (!frontend_client_source_destroy(&f->client_sources, error)) return false;
    return true;
}
size_t frontend_client_source_count(const qa_frontend *f)
{
    size_t count = 0;
    for (const frontend_client_source *s = f ? f->client_sources : NULL; s; s = s->next) ++count;
    return count;
}
frontend_client_source *frontend_client_source_at(const qa_frontend *f, size_t ordinal)
{
    frontend_client_source *s = f ? f->client_sources : NULL;
    while (s && ordinal--) s = s->next;
    return s;
}
bool frontend_client_sources_visit(const qa_frontend *f, const qa_application_content_visitor *visitor, qa_error *error)
{
    if (!visitor || !visitor->catalog || !visitor->view || !visitor->pool) return false;
    for (const frontend_client_source *s = f ? f->client_sources : NULL; s; s = s->next) {
        const qa_launch_instance *d = descriptor(s);
        if (!d || !visitor->catalog(visitor->context, qa_launch_instance_catalog(d), error) ||
            !visitor->view(visitor->context, d->content, error) ||
            !visitor->pool(visitor->context, qa_vfs_resources(d->content), error)) return false;
    }
    return true;
}
bool frontend_client_source_capture(frontend_client_source *s, frontend_client_source_state *out, qa_error *error)
{
    if (!out || out->application.actors ||
        !frontend_client_source_idle(s) || !s->app_attached || s->closing)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT prefix capture requires its returned actual owner");
    frontend_client_source_state state = {.command = s->command, .capabilities = capabilities(&s->options),
        .ready = s->ready,.retiring=s->retiring,.programme_retired=s->programme_retired};
    uint64_t captured_registry=frontend_save_commands_registry(s->frontend);
    state.command.registry=qa_console_save_context_registry(qa_application_session(s->frontend->application),
        state.command.registry,captured_registry);
    bool retired=qa_application_client_retirement_current(s->frontend->application,&s->application);
    if(s->retiring&&!retired)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT retirement capture lost its retained disconnect receipt");
    bool captured=retired?qa_application_client_capture_retired(s->frontend->application,&s->application,&state.application,error):
        qa_application_client_capture(s->frontend->application,&s->application,&state.application,error);
    if (!captured) {
        frontend_client_source_state_free(&state); return false;
    }
    *out = state; return true;
}
void frontend_client_source_state_free(frontend_client_source_state *state)
{
    if (!state) return;
    qa_application_client_state_free(&state->application);
    *state = (frontend_client_source_state){0};
}

typedef struct client_source_prefix {
    uint64_t catalog, content;
    qa_product_id profile, selected;
    qa_launch_provider selection;
    frontend_client_source_state state;
} client_source_prefix;
static bool clock_fields(qa_source_save_io *io, qa_clock_config *clock)
{
    uint32_t kind = clock->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_RULESET_Q3 ||
        !qa_source_save_u64(io, &clock->initial_time_ns) || !qa_source_save_u64(io, &clock->interval_ns) ||
        !qa_source_save_u64(io, &clock->minimum_frame_ns) || !qa_source_save_u64(io, &clock->maximum_frame_ns) ||
        !qa_source_save_u64(io, &clock->initial_lead_ns) || !qa_source_save_u32(io, &clock->maximum_steps)) return false;
    clock->kind = (qa_ruleset_id)kind; return true;
}
static bool command_fields(qa_source_save_io *io, qa_command_context *c)
{
    uint32_t dialect = c->dialect, origin = c->origin;
    if (!qa_source_save_u64(io, &c->session) || !qa_source_save_u64(io, &c->owner) ||
        !qa_source_save_u64(io, &c->client) || !qa_source_save_u32(io, &c->seat) ||
        !qa_source_save_u32(io, &dialect) || dialect > QA_RULESET_Q3 ||
        !qa_source_save_u32(io, &origin) || (origin != QA_COMMAND_LOCAL && origin != QA_COMMAND_SEAT) ||
        !qa_source_save_bool(io, &c->direct) || !qa_source_save_bool(io, &c->console_text) ||
        !qa_source_save_u64(io, &c->registry) || !c->registry ||
        !qa_source_save_u64(io, &c->generation) || !c->generation || c->script || c->actor.registry ||
        c->actor.generation || c->actor.slot) return false;
    c->dialect = (qa_ruleset_id)dialect; c->origin = (qa_command_origin)origin;
    return true;
}
static bool selection_text(qa_source_save_io *io, const char **text)
{
    char *owned = (char *)*text;
    bool ok = qa_source_save_owned_text(io, &owned);
    *text = owned; return ok;
}
static void decoded_selection_free(qa_launch_provider *selection)
{
    free((char *)selection->instance); free((char *)selection->implementation);
    selection->instance = NULL; selection->implementation = NULL;
}
static bool prefix_fields(qa_source_save_io *io, client_source_prefix *p)
{
    uint8_t magic[4] = {'Q','F','C','S'}; bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_application_client_state *a = &p->state.application;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QFCS", sizeof(magic)) ||
        !qa_source_save_u64(io, &p->catalog) || !p->catalog ||
        !qa_source_save_u64(io, &p->content) || !p->content ||
        !qa_source_save_u32(io, &p->profile) || !p->profile ||
        !qa_source_save_u32(io, &p->selected) || !p->selected ||
        !selection_text(io, &p->selection.instance) || !p->selection.instance || !*p->selection.instance ||
        !qa_source_save_u32(io, &p->selection.product) || !p->selection.product ||
        !selection_text(io, &p->selection.implementation) || !p->selection.implementation ||
        !clock_fields(io, &p->selection.clock) ||
        !qa_source_save_string(io, &a->receiver) || !a->receiver ||
        !qa_source_save_string(io, &a->entity_owner) || !a->entity_owner ||
        !qa_source_save_u32(io, &a->seat) || !qa_source_save_u32(io, &a->physical_seat) ||
        !qa_source_save_u64(io, &a->configuration_generation) ||
        !qa_source_save_u64(io, &a->connection_epoch) || !qa_source_save_u64(io, &a->entity_generation) ||
        !qa_source_save_u64(io, &a->client.owner) || !qa_source_save_u64(io, &a->client.generation) ||
        !qa_source_save_u32(io, &a->client.slot) || !qa_source_save_u64(io, &a->network_seat.owner) ||
        !qa_source_save_u32(io, &a->network_seat.index) ||
        !qa_source_save_count(io, &a->actor_count, qa_actors_capacity(qa_session_actors(io->session)))) return false;
    if (reading && a->actor_count) {
        a->actors = calloc(a->actor_count, sizeof(*a->actors));
        if (!a->actors) return frontend_fail(io->error, QA_ERROR_MEMORY, "Decoding CLIENT observer references");
    }
    for (size_t i = 0; i < a->actor_count; ++i)
        if (!qa_source_save_u64(io, &a->actors[i].generation) || !qa_source_save_u32(io, &a->actors[i].slot)) return false;
    if (!command_fields(io, &p->state.command) || p->state.command.seat != a->seat ||
        p->state.command.owner != a->receiver || !qa_source_save_u32(io, &p->state.capabilities) ||
        (p->state.capabilities & ~524287u) || (p->state.capabilities & 15u) != 15u ||
        !qa_source_save_bool(io, &p->state.ready) ||
        !qa_source_save_bool(io,&p->state.retiring)||!qa_source_save_bool(io,&p->state.programme_retired)||
        (p->state.programme_retired&&!p->state.retiring)||
        (p->state.retiring&&(!(p->state.capabilities&262144u)||!a->client.owner))||
        (!p->state.ready && a->client.owner)) return false;
    if (reading) {
        p->selection.runtime = QA_PROGRAM_BUILTIN; p->selection.artifact = ""; p->selection.component = "";
    }
    return true;
}
bool frontend_client_source_checkpoint(frontend_client_source *s, const qa_application_content_graph *graph,
    qa_buffer *out, qa_error *error)
{
    if (!graph || !out || out->data || out->size || !frontend_client_source_idle(s) ||
        !s->app_attached || !descriptor(s) || s->closing)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT checkpoint requires its retained graph and returned owner");
    const qa_launch_instance *d = descriptor(s);
    client_source_prefix p = {.catalog = qa_application_content_catalog_id(graph, qa_launch_instance_catalog(d)),
        .content = qa_application_content_view_id(graph, d->content), .profile = s->options.metadata.profile,
        .selected = s->options.metadata.selected, .selection = d->selection};
    qa_source_save_io io = {0};
    bool ok = frontend_client_source_capture(s, &p.state, error) &&
        qa_source_save_writer(&io, qa_application_session(s->frontend->application), error) &&
        prefix_fields(&io, &p) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); frontend_client_source_state_free(&p.state);
    if (!ok && error && error->code == QA_OK)
        frontend_fail(error, QA_ERROR_FORMAT, "CLIENT prefix leaves its genuine content graph");
    return ok;
}
bool frontend_client_source_restore_prefix(qa_frontend *f, const frontend_client_source_options *options,
    qa_application_content_graph *graph, const qa_console_save_resolvers *resolvers, qa_bytes bytes,
    frontend_client_source **out, qa_error *error)
{
    if (!f || !options || !graph || !resolvers || !out || *out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT prefix requires its isolated graph and actual transport callbacks");
    client_source_prefix p = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(f->application), bytes, error) &&
        prefix_fields(&io, &p) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    qa_catalog *catalog = qa_application_content_catalog(graph, p.catalog);
    qa_vfs *view = qa_application_content_view(graph, p.content);
    if (ok) ok = catalog && view && catalog == options->metadata.catalog &&
        p.profile == options->metadata.profile && p.selected == options->metadata.selected &&
        p.selection.clock.kind == options->metadata.clock && options->metadata.instance &&
        !strcmp(p.selection.instance, options->metadata.instance) &&
        p.state.application.seat == options->metadata.seat &&
        p.state.application.physical_seat == options->physical_seat &&
        capabilities_match(options, p.state.capabilities);
    qa_vfs *claimed = NULL;
    if (ok) ok = qa_application_content_claim_view(graph, p.content, &claimed, error);
    if (ok) {
        qa_launch_restored_instance restored = {.catalog = catalog, .selection = p.selection,
            .content = claimed};
        ok = frontend_client_source_restore(f, options, &restored, &p.state, resolvers, out, error);
        /* Metadata restoration takes the real claim even on partial failure
         * once its arguments are admitted. A rejected outer call owns it. */
        if (!*out) qa_vfs_destroy(claimed);
    }
    frontend_client_source_state_free(&p.state);
    decoded_selection_free(&p.selection);
    if (!ok && error && error->code == QA_OK)
        frontend_fail(error, QA_ERROR_FORMAT, "CLIENT prefix differs from its actual constructor recipe");
    return ok;
}

bool frontend_client_source_prefix_read(qa_frontend *f, qa_application_content_graph *graph, qa_bytes bytes,
    frontend_client_source_prefix *out, qa_error *error)
{
    if (!f || !f->application || !f->source_restoring || !graph || !out || out->state.application.actors ||
        out->descriptor.content || out->descriptor.selection.instance ||
        out->descriptor.selection.implementation || out->recipe.instance)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT recipe decode requires its actual isolated graph");
    client_source_prefix p = {0}; qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(f->application), bytes, error) &&
        prefix_fields(&io, &p) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    qa_catalog *catalog = qa_application_content_catalog(graph, p.catalog);
    qa_vfs *view = qa_application_content_view(graph, p.content);
    if (ok) ok = catalog && view && qa_catalog_product(catalog, p.profile) &&
        qa_catalog_product(catalog, p.selected) && p.selection.product == p.selected;
    if (!ok) {
        frontend_client_source_state_free(&p.state);
        decoded_selection_free(&p.selection);
        if (error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "CLIENT recipe leaves its imported graph");
        return false;
    }
    *out = (frontend_client_source_prefix){
        .recipe = {catalog, p.profile, p.selected, view, p.selection.instance,
            p.state.application.seat, p.selection.clock.kind},
        .descriptor = {.catalog = catalog, .selection = p.selection, .content = view},
        .state = p.state, .content_id = p.content};
    return true;
}
void frontend_client_source_prefix_free(frontend_client_source_prefix *prefix)
{
    if (!prefix) return;
    frontend_client_source_state_free(&prefix->state);
    decoded_selection_free(&prefix->descriptor.selection); *prefix = (frontend_client_source_prefix){0};
}
bool frontend_client_sources_finish_restore(qa_frontend *f, qa_error *error)
{
    if (!f || !f->source_restoring || f->capture || f->resource_inventory)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT source finish requires its actual isolated restoration");
    for (frontend_client_source *s = f->client_sources; s; s = s->next)
        if (!frontend_client_source_idle(s) || !s->app_attached || !s->restored_constructor ||
            (!qa_application_client_current(f->application, &s->application) &&
             !qa_application_client_retirement_current(f->application, &s->application)))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT source finish lost an imported physical owner");
    for (frontend_client_source *s = f->client_sources; s; s = s->next) s->restore_finished=true;
    return true;
}
bool frontend_client_source_restore_finished(const frontend_client_source *s)
{ return linked(s)&&s->restore_finished&&!s->closing; }

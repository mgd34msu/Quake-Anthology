#include "client_source.h"
#include "client_registry.h"
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
    qa_console *console;
    qa_application_client_source application;
    qa_command_context command;
    qa_actor_owner receiver;
    uint64_t configuration_generation;
    size_t references;
    unsigned calls;
    bool constructing, ready, closing, app_attached;
};
static bool linked(const frontend_client_source *s)
{
    for (const frontend_client_source *row = s && s->frontend ? s->frontend->client_sources : NULL;
        row; row = row->next) if (row == s) return true;
    return false;
}
static const qa_launch_instance *descriptor(const frontend_client_source *s)
{ return s ? qa_launch_instance_lease_view(s->metadata) : NULL; }
static qa_cvars *registry(const frontend_client_source *s)
{ return s && s->registry ? frontend_client_registry_cvars(s->registry) : s ? s->pending_cvars : NULL; }
static bool tuple(const qa_command_context *a, const qa_command_context *b, bool script)
{
    return a && b && a->session == b->session && a->owner == b->owner && a->client == b->client &&
        a->seat == b->seat && a->dialect == b->dialect && a->origin == b->origin && a->direct == b->direct &&
        a->console_text == b->console_text && a->registry == b->registry && a->generation == b->generation &&
        qa_actor_id_equal(a->actor, b->actor) && (!script || a->script == b->script);
}
bool frontend_client_source_retain(frontend_client_source *s, qa_error *error)
{
    if (!linked(s) || s->closing || s->frontend->resource_inventory || s->frontend->capture || s->references == SIZE_MAX)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT source callback owner is retiring");
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
bool frontend_client_source_idle(const frontend_client_source *s)
{
    return linked(s) && !s->calls && !s->constructing && qa_console_idle(s->console) &&
        (!registry(s) || qa_cvars_observer_idle(registry(s)));
}
static bool physical_current(void *context, const qa_launch_instance *d, qa_console *console,
    qa_cvars *cvars, const qa_command_context *command)
{
    frontend_client_source *s = context;
    const qa_launch_instance *actual = descriptor(s);
    return linked(s) && !s->closing && actual && d && d->storage == actual->storage &&
        d->content == actual->content && qa_sha256_equal(&d->identity, &actual->identity) &&
        s->registry && frontend_client_registry_matches(s->registry, actual, s->options.metadata.seat) &&
        cvars == registry(s) && console == s->console && tuple(command, &s->command, true);
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
    return linked(s) && !s->closing && !s->frontend->resource_inventory && !s->frontend->capture &&
        s->calls != UINT_MAX && tuple(command, &s->command, false) &&
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
    if (!linked(s) || s->closing || !tuple(command, &s->command, false) || s->calls == UINT_MAX) return;
    ++s->calls; s->options.print(s->options.context, command, text); --s->calls;
}
static void cvar_print(void *context, const char *text)
{ frontend_client_source *s = context; print(s, &s->command, text); }
static qa_cvars *cvars(void *context, const qa_command_context *command, const char *name)
{
    frontend_client_source *s = context;
    if (!active(s, command)) return NULL;
    if (!s->options.cvar_owner) return registry(s);
    ++s->calls; qa_cvars *out = s->options.cvar_owner(s->options.context, command, name); --s->calls;
    return out;
}
static qa_cvars *visible(void *context, const qa_command_context *command, size_t ordinal)
{
    frontend_client_source *s = context;
    if (!active(s, command)) return NULL;
    if (!s->options.visible_cvars) return ordinal ? NULL : registry(s);
    ++s->calls; qa_cvars *out = s->options.visible_cvars(s->options.context, command, ordinal); --s->calls;
    return out;
}
static bool edit(void *context, const qa_command_context *command, qa_cvars *variables,
    struct qa_cvars_edit **out, qa_error *error)
{
    frontend_client_source *s = context;
    if (!out || !active(s, command)) return false;
    ++s->calls;
    bool ok = s->options.cvar_edit(s->options.context, command, variables, out, error);
    --s->calls; return ok;
}
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
    if (!active(s, &invocation->context)) return QA_COMMAND_FAILED;
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
        .owner = {s, retain, app_release, physical_current, physical_idle, connection_current,
            s->options.entity_current ? entity_current : NULL}};
}
static uint32_t capabilities(const frontend_client_source_options *o)
{
    return (o->initialize ? 1u : 0u) | (o->configure ? 2u : 0u) | (o->print ? 4u : 0u) |
        (o->connection_current ? 8u : 0u) | (o->entity_current ? 16u : 0u) |
        (o->command ? 32u : 0u) | (o->forward ? 64u : 0u) | (o->allow_command ? 128u : 0u) |
        (o->cvar_owner ? 256u : 0u) | (o->visible_cvars ? 512u : 0u) | (o->cvar_edit ? 1024u : 0u) |
        (o->install ? 2048u : 0u) | (o->read_script ? 4096u : 0u) |
        (o->release_script ? 8192u : 0u) | (o->script_complete ? 16384u : 0u);
}
static bool construct(qa_frontend *f, const frontend_client_source_options *options,
    const qa_launch_restored_instance *metadata, const frontend_client_source_state *state,
    const qa_console_save_resolvers *resolvers, frontend_client_source **out, qa_error *error)
{
    if (!f || !f->application || f->capture || f->resource_inventory || !options || !out || *out ||
        !options->runtime || !options->print || !options->connection_current ||
        (!state && (!options->initialize || !options->configure)) ||
        ((options->read_script != NULL) != (options->release_script != NULL)) ||
        (state && (!metadata || !resolvers || !state->console.data || !state->console.size ||
            state->capabilities != capabilities(options))) ||
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
            s->command.actor.generation || s->command.actor.slot ||
            !qa_application_command_context_active(f->application, &s->command)) {
            frontend_fail(error, QA_ERROR_FORMAT, "Restored CLIENT command differs from its imported physical namespace"); goto done;
        }
    } else if (!qa_application_client_provider_command(f->application, s->receiver, options->metadata.seat,
        &options->input_origin, &s->command, &s->configuration_generation, error)) goto done;
    qa_cvar_options variables = {.dialect = s->command.dialect, .user = s, .print = cvar_print};
    s->pending_cvars = qa_cvars_create(&variables, error);
    if (!s->pending_cvars) goto done;
    if (!state) {
        ++s->calls;
        ok = options->initialize(options->context, descriptor(s), s->pending_cvars, &s->command, error);
        --s->calls;
        if (!ok) goto done;
    } else if (!f->client_registry_import) {
        frontend_fail(error, QA_ERROR_FORMAT, "Restored CLIENT requires its genuine QFCR heap prefix"); goto done;
    }
    frontend_client_registry_context callback = {s, retain, release};
    if (!frontend_client_registry_create(f, descriptor(s), options->metadata.seat,
        &s->pending_cvars, &callback, &s->registry, error)) goto done;
    qa_console_options console = {.context = s->command, .cvars = registry(s), .user = s,
        .print = print, .cvar_owner = cvars, .visible_cvars = visible,
        .cvar_edit = options->cvar_edit ? edit : NULL, .read_script = script, .release_script = script_release,
        .script_complete = options->script_complete ? script_complete : NULL,
        .allow_command = options->allow_command ? allow : NULL, .source_command = command, .forward = forward,
        .capture_context = capture, .context_active = active};
    s->console = qa_console_create(&console, error);
    if (!s->console) goto done;
    qa_application_client_options actual = app_options(s);
    ok = state ? qa_application_client_create_restored(f->application, &actual, &state->application, &s->application, error) :
        qa_application_client_create(f->application, &actual, &s->application, error);
    if (!ok) goto done;
    s->app_attached = true;
    if (options->install) {
        ++s->calls;
        ok = options->install(options->context, &s->application, state != NULL, error);
        --s->calls;
        if (!ok) goto done;
    }
    if (state) {
        if (!qa_console_save_restore(s->console, qa_application_session(f->application), resolvers,
            (qa_bytes){state->console.data, state->console.size}, error)) goto done;
        s->ready = state->ready; success = true;
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
    if (!ready || !frontend_client_source_idle(s) || s->closing || !s->app_attached ||
        s->frontend->resource_inventory || s->frontend->capture)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT configuration requires its returned physical constructor");
    if (!s->ready) {
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
bool frontend_client_source_read(const frontend_client_source *s, frontend_client_source_view *out, qa_error *error)
{
    if (!out || !linked(s) || s->closing || s->constructing || !s->app_attached ||
        !qa_application_client_current(s->frontend->application, &s->application))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT source no longer owns its physical graph");
    *out = (frontend_client_source_view){s, s->application, s->ready}; return true;
}
bool frontend_client_source_metadata_read(const frontend_client_source *s,
    frontend_client_source_view *out, qa_error *error)
{
    if (!out || !linked(s) || s->constructing || !s->app_attached ||
        !physical_current((void *)s, s->application.descriptor, s->application.context.console,
            s->application.context.cvars, &s->application.context.command))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT topology leaves its retained physical constructor");
    *out = (frontend_client_source_view){s, s->application, s->ready}; return true;
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
    if (!frontend_client_source_idle(s) || s->closing || !s->ready || !s->app_attached || s->application.client.owner ||
        s->frontend->resource_inventory || s->frontend->capture)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT bind requires its completed pending constructor");
    qa_application_client_source actual;
    if (!qa_application_client_bind(s->frontend->application, &s->application, client, seat, epoch, &actual, error)) return false;
    s->application = actual; return true;
}
bool frontend_client_source_drain(frontend_client_source *s, size_t budget, size_t *executed, qa_error *error)
{
    if (!frontend_client_source_idle(s) || s->closing || !s->app_attached ||
        s->frontend->resource_inventory || s->frontend->capture) return false;
    ++s->calls; bool ok = qa_console_drain(s->console, budget, executed, error); --s->calls; return ok;
}
bool frontend_client_source_destroy(frontend_client_source **owned, qa_error *error)
{
    frontend_client_source *s = owned ? *owned : NULL;
    if (!s) return true;
    size_t expected = 1 + (s->registry != NULL) + s->app_attached;
    if (!frontend_client_source_idle(s) || s->frontend->capture || s->frontend->resource_inventory ||
        s->references != expected || !qa_console_destroy_ready(s->console) ||
        (s->app_attached && !qa_application_client_idle(s->frontend->application, &s->application)) ||
        !frontend_client_registry_release_ready(s->registry, error))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT source retains an entered or borrowed physical child");
    if (s->app_attached && !qa_application_client_retire(s->frontend->application, &s->application, error)) return false;
    s->closing = true;
    qa_console_destroy(s->console); s->console = NULL;
    if (!frontend_client_registry_release(&s->registry, error)) return false;
    qa_cvars_destroy(s->pending_cvars); s->pending_cvars = NULL;
    if (s->receiver && !qa_application_client_provider_release(s->frontend->application, s->receiver, error)) return false;
    s->receiver = 0;
    qa_launch_instance_lease_release(s->metadata); s->metadata = NULL;
    frontend_client_source **link = &s->frontend->client_sources;
    while (*link != s) link = &(*link)->next;
    *link = s->next; free(s); *owned = NULL; return true;
}
bool frontend_client_sources_idle(const qa_frontend *f)
{
    for (const frontend_client_source *s = f ? f->client_sources : NULL; s; s = s->next)
        if (!frontend_client_source_idle(s)) return false;
    return true;
}
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
    if (!out || out->application.actors || out->console.data || out->console.size ||
        !frontend_client_source_idle(s) || !s->app_attached || s->closing)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "CLIENT prefix capture requires its returned actual owner");
    frontend_client_source_state state = {.command = s->command, .capabilities = capabilities(&s->options), .ready = s->ready};
    if (!qa_application_client_capture(s->frontend->application, &s->application, &state.application, error) ||
        !qa_console_save_capture(s->console, qa_application_session(s->frontend->application), &state.console, error)) {
        frontend_client_source_state_free(&state); return false;
    }
    *out = state; return true;
}
void frontend_client_source_state_free(frontend_client_source_state *state)
{
    if (!state) return;
    qa_application_client_state_free(&state->application); qa_buffer_free(&state->console);
    *state = (frontend_client_source_state){0};
}

typedef struct client_source_prefix {
    uint64_t catalog, content;
    qa_product_id profile, selected;
    qa_launch_provider selection;
    qa_sha256_digest identity;
    frontend_client_source_state state;
} client_source_prefix;
static bool clock_fields(qa_source_save_io *io, qa_clock_config *clock)
{
    uint32_t kind = clock->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_CLOCK_Q3 ||
        !qa_source_save_u64(io, &clock->initial_time_ns) || !qa_source_save_u64(io, &clock->interval_ns) ||
        !qa_source_save_u64(io, &clock->minimum_frame_ns) || !qa_source_save_u64(io, &clock->maximum_frame_ns) ||
        !qa_source_save_u64(io, &clock->initial_lead_ns) || !qa_source_save_u32(io, &clock->maximum_steps)) return false;
    clock->kind = (qa_clock_kind)kind; return true;
}
static bool command_fields(qa_source_save_io *io, qa_command_context *c)
{
    uint32_t dialect = c->dialect, origin = c->origin;
    if (!qa_source_save_u64(io, &c->session) || !qa_source_save_u64(io, &c->owner) ||
        !qa_source_save_u64(io, &c->client) || !qa_source_save_u32(io, &c->seat) ||
        !qa_source_save_u32(io, &dialect) || dialect > QA_CONSOLE_Q3 ||
        !qa_source_save_u32(io, &origin) || (origin != QA_COMMAND_LOCAL && origin != QA_COMMAND_SEAT) ||
        !qa_source_save_bool(io, &c->direct) || !qa_source_save_bool(io, &c->console_text) ||
        !qa_source_save_u64(io, &c->registry) || !c->registry ||
        !qa_source_save_u64(io, &c->generation) || !c->generation || c->script || c->actor.registry ||
        c->actor.generation || c->actor.slot) return false;
    c->dialect = (qa_console_dialect)dialect; c->origin = (qa_command_origin)origin;
    return true;
}
static bool buffer_fields(qa_source_save_io *io, qa_buffer *buffer)
{
    size_t size = buffer->size;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!qa_source_save_count(io, &size, reading ? io->input.size - io->offset : SIZE_MAX) || !size) return false;
    if (reading) {
        buffer->data = malloc(size);
        if (!buffer->data) return frontend_fail(io->error, QA_ERROR_MEMORY, "Decoding CLIENT console prefix");
        buffer->size = size;
    }
    return qa_source_save_bytes(io, buffer->data, size);
}
static bool prefix_fields(qa_source_save_io *io, client_source_prefix *p)
{
    uint8_t magic[4] = {'Q','F','C','S'}; uint32_t version = 1;
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_application_client_state *a = &p->state.application;
    if (!qa_source_save_bytes(io, magic, sizeof(magic)) || memcmp(magic, "QFCS", sizeof(magic)) ||
        !qa_source_save_u32(io, &version) || version != 1 ||
        !qa_source_save_u64(io, &p->catalog) || !p->catalog ||
        !qa_source_save_u64(io, &p->content) || !p->content ||
        !qa_source_save_u32(io, &p->profile) || !p->profile ||
        !qa_source_save_u32(io, &p->selected) || !p->selected ||
        !qa_source_save_text(io, &p->selection.instance) || !p->selection.instance || !*p->selection.instance ||
        !qa_source_save_u32(io, &p->selection.product) || !p->selection.product ||
        !qa_source_save_text(io, &p->selection.implementation) || !p->selection.implementation ||
        !clock_fields(io, &p->selection.clock) || !qa_source_save_bytes(io, p->identity.bytes, sizeof(p->identity.bytes)) ||
        !qa_source_save_bytes(io, a->descriptor_identity.bytes, sizeof(a->descriptor_identity.bytes)) ||
        !qa_sha256_equal(&p->identity, &a->descriptor_identity) ||
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
        (p->state.capabilities & ~32767u) || (p->state.capabilities & 15u) != 15u ||
        !qa_source_save_bool(io, &p->state.ready) ||
        (!p->state.ready && a->client.owner) || !buffer_fields(io, &p->state.console)) return false;
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
        .selected = s->options.metadata.selected, .selection = d->selection, .identity = d->identity};
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
        p.state.capabilities == capabilities(options);
    qa_vfs *claimed = NULL;
    if (ok) ok = qa_application_content_claim_view(graph, p.content, &claimed, error);
    if (ok) {
        qa_launch_restored_instance restored = {.catalog = catalog, .selection = p.selection,
            .content = claimed, .identity = p.identity};
        ok = frontend_client_source_restore(f, options, &restored, &p.state, resolvers, out, error);
        /* Metadata restoration takes the real claim even on partial failure
         * once its arguments are admitted. A rejected outer call owns it. */
        if (!*out) qa_vfs_destroy(claimed);
    }
    frontend_client_source_state_free(&p.state);
    if (!ok && error && error->code == QA_OK)
        frontend_fail(error, QA_ERROR_FORMAT, "CLIENT prefix differs from its actual constructor recipe");
    return ok;
}

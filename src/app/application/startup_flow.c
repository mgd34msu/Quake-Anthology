#include "startup_flow.h"
#include "q3_product.h"
#include "native_q2_console.h"
#include "native_q1_console.h"
#include "native_q3_console.h"
#include <stdlib.h>

typedef struct startup_source {
    application_provider *provider;
    qa_console *console;
    qa_cvars *cvars;
    qa_command_context command;
    void *phase;
} startup_source;

struct application_startup_flow {
    qa_application_startup_hooks hooks;
    application_q3_product_preparation product;
    qa_configuration_transaction *transaction;
    application_publication *publication;
    const qa_launch_snapshot *candidate, *previous;
    startup_source *sources;
    size_t count, index;
    uint64_t generation;
    bool advancing, configured, candidate_finished, cancelling;
};

bool qa_application_startup_pending(const qa_application *app)
{ return app && app->startup_flow; }

const qa_launch_snapshot *qa_application_startup_candidate(const qa_application *app)
{ return app && app->startup_flow ? app->startup_flow->candidate : NULL; }

application_provider *application_startup_flow_provider(const qa_application *app, uint64_t owner)
{
    const struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!app || !owner || app->destroy_requested)
        return NULL;
    if (!flow) {
        application_provider *provider = app->startup_preinit_provider;
        const qa_launch_instance *instance = provider && provider->launch && app->routing_snapshot
            ? qa_launch_snapshot_find(app->routing_snapshot, provider->launch->selection.instance) : NULL;
        return provider && instance && instance->state == provider && provider->owner == owner &&
            provider->application == app && !provider->attached && !provider->close_pending
            ? provider : NULL;
    }
    if (flow->generation != app->command_generation) return NULL;
    for (size_t i = 0; i < qa_launch_snapshot_instance_count(flow->candidate); ++i) {
        const qa_launch_instance *instance = qa_launch_snapshot_instance(flow->candidate, i);
        application_provider *provider = instance->state;
        if (provider && provider->owner == owner && provider->application == app &&
            !provider->close_pending && !provider->attached)
            return provider;
    }
    return NULL;
}

static startup_source *source_at(application_provider *provider)
{
    struct application_startup_flow *flow = provider && provider->application
        ? provider->application->startup_flow : NULL;
    if (!flow || application_startup_flow_provider(provider->application, provider->owner) != provider)
        return NULL;
    for (size_t i = 0; i < flow->count; ++i)
        if (flow->sources[i].provider == provider) return flow->sources + i;
    return NULL;
}

bool qa_application_startup_source_read(qa_application *app, const qa_launch_snapshot *snapshot,
    const qa_launch_instance *instance, qa_console **console, qa_cvars **cvars,
    qa_command_context *command, qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!app || !snapshot || !instance || !console || !cvars || !command ||
        (snapshot != qa_configuration_current(app->configuration) &&
         snapshot != app->routing_snapshot && (!flow || snapshot != flow->candidate)) ||
        qa_launch_snapshot_find(snapshot, instance->selection.instance) != instance)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup source read requires its retained physical candidate");
    application_provider *provider = instance->state;
    if (!provider || provider->application != app || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup source read lost its actual provider");
    startup_source *source = source_at(provider);
    if (source) {
        *console = source->console; *cvars = source->cvars; *command = source->command;
        return true;
    }
    if (!provider->constructed || !provider->attached)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup source has no prepared or installed physical console");
    bool found = provider->kind == APPLICATION_PROVIDER_Q1
        ? application_native_q1_console_at(provider, console, cvars, command)
        : provider->kind == APPLICATION_PROVIDER_Q2
            ? application_native_q2_console_at(provider, console, cvars, command)
            : provider->kind == APPLICATION_PROVIDER_Q3
                ? application_native_q3_console_at(provider, console, cvars, command) : false;
    if (!found)
        for (size_t i = 0; application_guest_console_at(provider, i, console, cvars, command); ++i) {
            qa_application_console_scope scope;
            if (application_guest_console_scope(provider, *console, &scope) &&
                (scope.kind == QA_APPLICATION_CONSOLE_Q3_GAME ||
                 scope.kind == QA_APPLICATION_CONSOLE_QC ||
                 scope.kind == QA_APPLICATION_CONSOLE_NATIVE_Q2)) { found = true; break; }
        }
    return found ? qa_application_capture_command_context(app, command, command, error)
        : application_fail(error, QA_ERROR_ARGUMENT, "Selected physical source has no genuine GAME console");
}

bool application_startup_source_active(const application_provider *provider)
{
    startup_source *source = source_at((application_provider *)provider);
    return source && source->phase;
}

bool application_startup_command_allowed(application_provider *provider, const qa_command_invocation *command)
{
    startup_source *source = source_at(provider);
    if (!source || !source->phase) return true;
    struct application_startup_flow *flow = provider->application->startup_flow;
    return !flow->hooks.allow_command || flow->hooks.allow_command(flow->hooks.context, source->phase, command);
}

bool qa_application_startup_replay_variables(qa_application *app, qa_console *console, qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow || !console) return application_fail(error, QA_ERROR_ARGUMENT, "Variable replay needs its retained source console");
    const qa_launch_snapshot *routing = app->routing_snapshot;
    application_provider **providers = app->routing_providers;
    size_t count = app->routing_provider_count;
    app->routing_snapshot = flow->candidate;
    app->routing_providers = flow->publication ? flow->publication->next : NULL;
    app->routing_provider_count = flow->publication ? flow->publication->next_count : 0;
    bool ok = false, found = false;
    for (size_t i = 0; i < flow->count; ++i)
        if (flow->sources[i].console == console) {
            found = true;
            ok = application_startup_seed_source(flow->sources[i].provider, flow->sources[i].cvars, error);
            break;
        }
    app->routing_snapshot = routing;
    app->routing_providers = providers;
    app->routing_provider_count = count;
    return found ? ok : application_fail(error, QA_ERROR_ARGUMENT,
        "Variable replay has another physical source console");
}

static bool q2_read(void *context, const qa_command_context *command, const char *name,
    qa_bytes *out, void **lease, qa_error *error)
{ return application_startup_script_read(context, command, name, out, lease, error); }
static void q2_release(void *context, void *lease)
{ application_startup_script_release(context, lease); }
static void q2_complete(void *context, const qa_command_context *command, const char *name, bool success)
{ application_startup_script_complete(context, command, name, success); }

bool application_startup_script_read(application_provider *provider,
    const qa_command_context *command, const char *name, qa_bytes *out, void **lease, qa_error *error)
{
    startup_source *source = source_at(provider);
    if (!source || !source->phase || !qa_application_command_context_active(provider->application, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup script lost its retained source phase");
    struct application_startup_flow *flow = provider->application->startup_flow;
    return flow->hooks.read_script(flow->hooks.context, source->phase, command, name, out, lease, error);
}

void application_startup_script_release(application_provider *provider, void *lease)
{
    startup_source *source = source_at(provider);
    if (source && source->phase) {
        struct application_startup_flow *flow = provider->application->startup_flow;
        flow->hooks.release_script(flow->hooks.context, source->phase, lease);
    }
}

void application_startup_script_complete(application_provider *provider,
    const qa_command_context *command, const char *name, bool success)
{
    startup_source *source = source_at(provider);
    if (source && source->phase) {
        struct application_startup_flow *flow = provider->application->startup_flow;
        flow->hooks.script_complete(flow->hooks.context, source->phase, command, name, success);
    }
}

application_publication *application_startup_flow_take_publication(qa_application *app,
    const qa_launch_snapshot *previous, const qa_launch_snapshot *candidate)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow || !flow->configured || !flow->publication || previous != flow->previous ||
        candidate != flow->candidate) return NULL;
    application_publication *publication = flow->publication;
    flow->publication = NULL;
    return publication;
}

static bool release_phases(struct application_startup_flow *flow, qa_error *error)
{
    for (size_t i = flow->count; i-- > 0;) {
        startup_source *source = flow->sources + i;
        if (source->phase && !flow->hooks.release_source(flow->hooks.context, source->phase, error))
            return false;
        source->phase = NULL;
    }
    return true;
}

static void finish_candidate(qa_application *app, struct application_startup_flow *flow,
    const qa_launch_snapshot *candidate, bool published)
{
    if (!flow->candidate_finished) {
        flow->candidate_finished = true;
        flow->hooks.finish_candidate(flow->hooks.context, app, candidate, published);
    }
}

void application_startup_flow_discard_candidate(qa_application *app,
    const qa_launch_snapshot *candidate)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (flow && flow->candidate == candidate) finish_candidate(app, flow, candidate, false);
}

bool qa_application_startup_abort(qa_application *app, qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow) return true;
    if (flow->advancing || !application_guests_idle(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup cancellation requires returned source callbacks");
    flow->advancing = true;
    flow->cancelling = true;
    if (!release_phases(flow, error)) goto fail;
    finish_candidate(app, flow, flow->candidate, false);
    /* Instances must release their hosts before the detached world is freed. */
    for (size_t i = 0; i < qa_launch_snapshot_instance_count(flow->candidate); ++i) {
        application_provider *provider = qa_launch_snapshot_instance(flow->candidate, i)->state;
        if (provider && !provider->attached && !application_provider_deconstruct(provider, error)) goto fail;
    }
    if (flow->publication) {
        application_publication_dispose(app, flow->publication);
        flow->publication = NULL;
    }
    if (flow->transaction && !qa_configuration_abort(flow->transaction, error)) goto fail;
    flow->transaction = NULL;
    application_q3_product_finish(app, &flow->product, false);
    app->startup_flow = NULL;
    free(flow->sources);
    free(flow);
    return true;
fail:
    flow->advancing = false;
    return false;
}

static void abort_failed_startup(qa_application *app, qa_error *error)
{
    qa_error cleanup = {0};
    if (!qa_application_startup_abort(app, &cleanup)) {
        application_fault(app, &cleanup);
        if (error && error->code == QA_OK) *error = cleanup;
    }
}

bool application_startup_flow_begin(qa_application *app, const qa_launch_draft *draft, qa_error *error)
{
    const qa_application_startup_hooks *hooks = app ? app->startup_hooks : NULL;
    if (!hooks || !hooks->prepare_source || !hooks->advance_source || !hooks->read_script ||
        !hooks->release_script || !hooks->script_complete || !hooks->release_source ||
        !hooks->prepare_candidate || !hooks->finish_candidate ||
        app->startup_flow)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup preparation requires complete actual source owners");
    struct application_startup_flow *flow = calloc(1, sizeof(*flow));
    if (!flow) return application_fail(error, QA_ERROR_MEMORY, "Retaining startup candidate");
    flow->hooks = *hooks;
    flow->generation = app->command_generation;
    flow->advancing = true;
    app->startup_flow = flow;
    flow->previous = qa_configuration_current(app->configuration);
    bool ok = application_q3_product_prepare_draft(app, draft, &flow->product, error) &&
        qa_configuration_prepare(app->configuration, flow->product.draft, &flow->transaction, error);
    if (ok) flow->candidate = qa_configuration_candidate(flow->transaction);
    if (ok) ok = application_publication_begin(app, flow->previous, flow->candidate, &flow->publication, error);
    size_t capacity = ok ? flow->publication->next_count : 0;
    if (ok && capacity > SIZE_MAX / sizeof(*flow->sources))
        ok = application_fail(error, QA_ERROR_MEMORY, "Startup source roster exceeds native storage");
    if (ok && capacity) {
        flow->sources = calloc(capacity, sizeof(*flow->sources));
        if (!flow->sources) ok = application_fail(error, QA_ERROR_MEMORY, "Retaining startup source phases");
    }
    const qa_launch_choices *choices = qa_launch_snapshot_choices(flow->candidate);
    const qa_launch_snapshot *routing = app->routing_snapshot;
    application_provider **providers = app->routing_providers;
    size_t count = app->routing_provider_count;
    if (ok) {
        app->routing_snapshot = flow->candidate;
        app->routing_providers = flow->publication->next;
        app->routing_provider_count = flow->publication->next_count;
    }
    application_provider *primary_provider = ok ? flow->publication->map_provider : NULL;
    if (ok && !primary_provider) {
        const qa_launch_binding *binding = qa_launch_binding_for(choices,
            (qa_launch_scope){.kind = QA_SCOPE_WORLD}, QA_ROLE_ENTITIES, "");
        const qa_launch_instance *instance = binding
            ? qa_launch_snapshot_find(flow->candidate, binding->instance) : NULL;
        primary_provider = instance ? instance->state : NULL;
    }
    size_t primary = 0;
    while (ok && primary < capacity &&
        flow->publication->next[primary] != primary_provider) ++primary;
    if (ok && primary == capacity)
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Startup candidate has no actual primary map source");
    for (size_t ordinal = 0; ok && ordinal < capacity; ++ordinal) {
        size_t i = ordinal == 0 ? primary : ordinal <= primary ? ordinal - 1 : ordinal;
        application_provider *provider = flow->publication->next[i];
        if (provider->constructed && provider->attached) continue;
        qa_catalog *catalog = qa_launch_instance_catalog(provider->launch);
        if (!catalog) catalog = qa_launch_snapshot_catalog(flow->candidate);
        const qa_product *product = qa_catalog_product(catalog, provider->launch->selection.product);
        startup_source *source = flow->sources + flow->count;
        source->provider = provider;
        ok = product && application_provider_console_prepare(app, provider,
            flow->publication->initial_world ? flow->publication->initial_world : app->world,
            catalog, product, choices,
            &source->console, &source->cvars, &source->command, error);
        if (!ok) break;
        if (!source->console) continue;
        ++flow->count;
        if (provider->kind == APPLICATION_PROVIDER_Q2) {
            application_q2_source_scripts scripts = {.context = provider,
                .read = q2_read, .release = q2_release, .complete = q2_complete};
            ok = application_native_q2_console_scripts(provider, &scripts, error);
        }
        ok = ok && qa_application_capture_command_context(app, &source->command, &source->command, error) &&
            flow->hooks.prepare_source(flow->hooks.context, app, flow->candidate,
                qa_launch_snapshot_find(flow->candidate, provider->launch->selection.instance),
                source->console, source->cvars, &source->command, &source->phase, error);
        if (ok && !source->phase) ok = application_fail(error, QA_ERROR_ARGUMENT, "Startup factory returned no retained phase");
    }
    app->routing_snapshot = routing;
    app->routing_providers = providers;
    app->routing_provider_count = count;
    flow->advancing = false;
    if (!ok) abort_failed_startup(app, error);
    return ok;
}

bool qa_application_startup_advance(qa_application *app, bool *complete, qa_error *error)
{
    if (!app || !complete || app->operation != APPLICATION_IDLE || app->frame_preparing ||
        app->q3_round_active || app->destroy_requested || qa_application_should_stop(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup frame requires its idle application driver");
    *complete = false;
    struct application_startup_flow *flow = app->startup_flow;
    if (!flow) { *complete = true; return true; }
    if (flow->advancing || flow->cancelling || flow->generation != app->command_generation || !application_guests_idle(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup frame lost its retained candidate lifetime");
    flow->advancing = true;
    app->operation = APPLICATION_CONFIGURING;
    bool ok = true;
    while (ok && flow->index < flow->count) {
        startup_source *source = flow->sources + flow->index;
        bool done = false;
        ok = flow->hooks.advance_source(flow->hooks.context, source->phase, source->console, &done, error);
        if (!ok || !done) break;
        if (!qa_console_idle(source->console) || qa_console_pending(source->console)) {
            ok = application_fail(error, QA_ERROR_ARGUMENT,
                "Startup phase completion requires its drained physical console");
            break;
        }
        ++flow->index;
    }
    if (ok && flow->index == flow->count) {
        ok = flow->hooks.prepare_candidate(flow->hooks.context, app, flow->candidate, error);
        if (ok) flow->configured = true;
        for (size_t i = 0; ok && i < flow->count; ++i)
            if (flow->sources[i].provider->kind == APPLICATION_PROVIDER_Q2)
                ok = application_native_q2_console_scripts(flow->sources[i].provider, NULL, error);
        if (ok) ok = release_phases(flow, error);
        ok = ok && qa_configuration_validate(flow->transaction, error) &&
            qa_configuration_commit(flow->transaction, error);
        if (ok) {
            flow->transaction = NULL;
            flow->candidate = NULL;
            finish_candidate(app, flow, qa_configuration_current(app->configuration), true);
            application_q3_product_finish(app, &flow->product, true);
            app->startup_flow = NULL;
            free(flow->sources); free(flow);
            if (app->state == QA_APPLICATION_READY) app->state = QA_APPLICATION_RUNNING;
            *complete = true;
        }
    }
    if (app->startup_flow) app->startup_flow->advancing = false;
    app->operation = APPLICATION_IDLE;
    if (ok && app->state == QA_APPLICATION_FAULTED) {
        if (error) *error = app->publication_error;
        ok = false;
    }
    if (!ok) abort_failed_startup(app, error);
    return ok;
}

#include "startup_flow.h"
#include "map_travel_private.h"
#include "q3_product.h"
#include "q3_campaign_launch.h"
#include "native_q2_console.h"
#include "native_q1_console.h"
#include "native_q3_console.h"
#include "startup_program.h"
#include <stdlib.h>

typedef struct startup_source {
    application_provider *provider;
    qa_application_startup_source owner;
    application_startup_program *program;
    void *phase;
} startup_source;

struct application_startup_flow {
    qa_application_startup_hooks hooks;
    application_q3_product_preparation product;
    qa_configuration_transaction *transaction;
    application_publication *publication;
    const qa_launch_snapshot *candidate, *previous;
    startup_source *sources;
    size_t count, index, capacity;
    uint64_t generation;
    bool advancing, configured, candidate_finished, cancelling, committed;
};

bool qa_application_startup_pending(const qa_application *app)
{ return app && app->startup_flow; }

void application_startup_flow_bound_client(qa_application *app, application_provider *provider,
    const qa_application_startup_source *source)
{
    struct application_startup_flow *flow = app->startup_flow;
    for (size_t i = 0; flow && i < flow->count; ++i) {
        startup_source *held = flow->sources + i;
        if (held->provider == provider && held->owner.console == source->console &&
            held->owner.descriptor->storage == source->descriptor->storage &&
            held->owner.scope.kind == source->scope.kind && held->owner.scope.seat == source->scope.seat)
            held->owner.cvars = source->cvars;
    }
}

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

static bool game_scope(qa_application_console_kind kind)
{
    return kind == QA_APPLICATION_CONSOLE_Q1_GAME || kind == QA_APPLICATION_CONSOLE_Q2_GAME ||
        kind == QA_APPLICATION_CONSOLE_Q3_GAME || kind == QA_APPLICATION_CONSOLE_QC ||
        kind == QA_APPLICATION_CONSOLE_NATIVE_Q2;
}

static startup_source *source_at(application_provider *provider, const qa_console *console)
{
    struct application_startup_flow *flow = provider && provider->application
        ? provider->application->startup_flow : NULL;
    if (!flow || application_startup_flow_provider(provider->application, provider->owner) != provider)
        return NULL;
    for (size_t i = 0; i < flow->count; ++i)
        if (flow->sources[i].provider == provider && flow->sources[i].owner.console) {
            startup_source *source = flow->sources + i;
            if (console ? source->owner.console == console : game_scope(source->owner.scope.kind))
                return source;
        }
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
    startup_source *source = source_at(provider, NULL);
    if (source) {
        *console = source->owner.console; *cvars = source->owner.cvars; *command = source->owner.command;
        return true;
    }
    application_publication *publication = app->startup_publication;
    bool prepared = false;
    if (publication && publication->candidate == snapshot && !publication->published)
        for (size_t i = 0; i < publication->next_count; ++i)
            if (publication->next[i] == provider) { prepared = true; break; }
    if (!provider->constructed || (!provider->attached && !prepared))
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
    if (!found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected physical source has no genuine GAME console");
    application_provider *prior = app->startup_preinit_provider;
    if (!provider->attached) app->startup_preinit_provider = provider;
    bool captured = qa_application_capture_command_context(app, command, command, error);
    app->startup_preinit_provider = prior;
    return captured;
}

bool application_startup_source_active(const application_provider *provider)
{ return application_startup_console_active(provider, NULL); }

bool application_startup_console_active(const application_provider *provider, const qa_console *console)
{
    startup_source *source = source_at((application_provider *)provider, console);
    return source && source->phase;
}

bool application_startup_command_allowed(application_provider *provider, const qa_command_invocation *command)
{ return application_startup_console_command_allowed(provider, NULL, command); }

bool application_startup_console_command_allowed(application_provider *provider,
    const qa_console *console, const qa_command_invocation *command)
{
    startup_source *source = source_at(provider, console);
    if (!source || !source->phase) return true;
    struct application_startup_flow *flow = provider->application->startup_flow;
    return !flow->hooks.allow_command || flow->hooks.allow_command(flow->hooks.context, source->phase, command);
}

bool qa_application_startup_replay_variables(qa_application *app, qa_console *console,
    const qa_command_context *command, qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow || !console || !command) return application_fail(error, QA_ERROR_ARGUMENT, "Variable replay needs its retained source console and command");
    const qa_launch_snapshot *routing = app->routing_snapshot;
    application_provider **providers = app->routing_providers;
    size_t count = app->routing_provider_count;
    app->routing_snapshot = flow->candidate;
    app->routing_providers = flow->publication ? flow->publication->next : NULL;
    app->routing_provider_count = flow->publication ? flow->publication->next_count : 0;
    bool ok = false, found = false;
    for (size_t i = 0; i < flow->count; ++i)
        if (flow->sources[i].owner.console == console) {
            found = true;
            ok = qa_application_command_context_active(app, command) &&
                command->owner == flow->sources[i].owner.command.owner &&
                application_startup_seed_console(flow->sources[i].provider, &flow->sources[i].owner, command, error);
            if (!ok && error && error->code == QA_OK)
                application_fail(error, QA_ERROR_ARGUMENT, "Variable replay lost its physical source command qualification");
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
{ return application_startup_console_script_read(provider, NULL, command, name, out, lease, error); }

bool application_startup_console_script_read(application_provider *provider, const qa_console *console,
    const qa_command_context *command, const char *name, qa_bytes *out, void **lease, qa_error *error)
{
    startup_source *source = source_at(provider, console);
    if (!source || !source->phase || !qa_application_command_context_active(provider->application, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup script lost its retained source phase");
    struct application_startup_flow *flow = provider->application->startup_flow;
    return flow->hooks.read_script(flow->hooks.context, source->phase, command, name, out, lease, error);
}

void application_startup_script_release(application_provider *provider, void *lease)
{ application_startup_console_script_release(provider, NULL, lease); }

void application_startup_console_script_release(application_provider *provider,
    const qa_console *console, void *lease)
{
    startup_source *source = source_at(provider, console);
    if (source && source->phase) {
        struct application_startup_flow *flow = provider->application->startup_flow;
        flow->hooks.release_script(flow->hooks.context, source->phase, lease);
    }
}

void application_startup_script_complete(application_provider *provider,
    const qa_command_context *command, const char *name, bool success)
{ application_startup_console_script_complete(provider, NULL, command, name, success); }

void application_startup_console_script_complete(application_provider *provider,
    const qa_console *console, const qa_command_context *command, const char *name, bool success)
{
    startup_source *source = source_at(provider, console);
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

static bool source_consoles_idle(const struct application_startup_flow *flow)
{
    for (size_t i = 0; i < flow->count; ++i)
        if (flow->sources[i].owner.console && !qa_console_idle(flow->sources[i].owner.console)) return false;
    return true;
}

bool application_startup_flow_release_provider(application_provider *provider, qa_error *error)
{
    struct application_startup_flow *flow = provider && provider->application
        ? provider->application->startup_flow : NULL;
    if (!flow) return true;
    for (size_t i = flow->count; i-- > 0;) {
        startup_source *source = flow->sources + i;
        if (source->provider != provider) continue;
        if (source->phase)
            return application_fail(error, QA_ERROR_ARGUMENT, "Source teardown requires its returned configuration phase");
        if (!application_startup_program_abort(&source->program, error)) return false;
    }
    for (size_t i = 0; i < flow->count; ++i)
        if (flow->sources[i].provider == provider) {
            flow->sources[i].owner.console = NULL;
            flow->sources[i].owner.cvars = NULL;
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

static bool candidate_callbacks_idle(qa_application *app, struct application_startup_flow *flow,
    qa_error *error)
{
    const qa_application_language_ticket *const *languages = NULL;
    size_t count = 0;
    if (!flow->committed && flow->candidate && flow->hooks.candidate_languages &&
        !flow->hooks.candidate_languages(flow->hooks.context, app, flow->candidate, &languages, &count, error))
        return false;
    return application_guests_languages_idle(app, languages, count) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Startup candidate still has unreturned source callbacks or unowned language admissions");
}

bool qa_application_startup_abort(qa_application *app, qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow) return true;
    if (flow->advancing || !source_consoles_idle(flow))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup cancellation requires returned source callbacks");
    if (!candidate_callbacks_idle(app, flow, error)) return false;
    flow->advancing = true;
    flow->cancelling = true;
    if (!flow->committed && flow->hooks.abort_candidate &&
        !flow->hooks.abort_candidate(flow->hooks.context, app, flow->candidate, error)) goto fail;
    if (!application_guests_idle(app)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Startup cancellation retains settings or language children");
        goto fail;
    }
    for (size_t i = flow->count; i-- > 0;)
        if (!application_startup_program_abort(&flow->sources[i].program, error)) goto fail;
    if (flow->committed) {
        if (!application_q3_campaign_launch_finish(app, true, error)) goto fail;
        app->startup_flow = NULL;
        free(flow->sources); free(flow);
        return true;
    }
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
    application_map_load_finish(app, false);
    if (!application_q3_campaign_launch_finish(app, false, error)) goto fail;
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

static startup_source *append_source(struct application_startup_flow *flow, qa_error *error)
{
    if (flow->count == flow->capacity) {
        size_t capacity = flow->capacity ? flow->capacity * 2 : 8;
        if (capacity < flow->capacity || capacity > SIZE_MAX / sizeof(*flow->sources)) {
            application_fail(error, QA_ERROR_MEMORY, "Startup source roster exceeds native storage");
            return NULL;
        }
        startup_source *sources = realloc(flow->sources, capacity * sizeof(*sources));
        if (!sources) {
            application_fail(error, QA_ERROR_MEMORY, "Retaining physical startup source phases");
            return NULL;
        }
        flow->sources = sources; flow->capacity = capacity;
    }
    startup_source *source = flow->sources + flow->count++;
    *source = (startup_source){0};
    return source;
}

static bool begin(qa_application *app, const qa_launch_draft *draft,
    bool replacing, qa_error *error)
{
    const qa_application_startup_hooks *hooks = app ? app->startup_hooks : NULL;
    if (!hooks || !hooks->prepare_source || !hooks->advance_source || !hooks->read_script ||
        !hooks->release_script || !hooks->script_complete || !hooks->release_source ||
        !hooks->prepare_candidate || !hooks->finish_candidate ||
        !hooks->preinit_source || !hooks->retire_source ||
        !hooks->read_source_script || !hooks->release_source_script ||
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
        (replacing ? qa_configuration_prepare_replacing(app->configuration, flow->product.draft, &flow->transaction, error)
            : qa_configuration_prepare(app->configuration, flow->product.draft, &flow->transaction, error));
    if (ok) flow->candidate = qa_configuration_candidate(flow->transaction);
    if (ok) ok = application_publication_begin(app, flow->previous, flow->candidate, &flow->publication, error);
    size_t provider_count = ok ? flow->publication->next_count : 0;
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
    while (ok && primary < provider_count &&
        flow->publication->next[primary] != primary_provider) ++primary;
    if (ok && primary == provider_count)
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Startup candidate has no actual primary map source");
    for (size_t ordinal = 0; ok && ordinal < provider_count; ++ordinal) {
        size_t i = ordinal == 0 ? primary : ordinal <= primary ? ordinal - 1 : ordinal;
        application_provider *provider = flow->publication->next[i];
        if (provider->constructed && provider->attached) {
            for (size_t index = 0; ok && flow->hooks.program_source; ++index) {
                qa_application_startup_source target, previous;
                bool found, inherited = false;
                ok = application_provider_startup_source_at(provider, index, &target, &found, error);
                if (!ok || !found) break;
                const qa_launch_instance *selected = qa_launch_snapshot_find(flow->candidate,
                    target.descriptor->selection.instance);
                if (!selected || selected->state != provider || selected->storage != target.descriptor->storage) {
                    ok = application_fail(error, QA_ERROR_ARGUMENT, "Retained console lost its actual candidate descriptor");
                    break;
                }
                target.descriptor = selected;
                ok = flow->hooks.program_source(flow->hooks.context, app, flow->candidate,
                    &target, &previous, &inherited, error);
                if (!ok || !inherited) continue;
                startup_source *source = append_source(flow, error);
                if (!source) { ok = false; break; }
                source->provider = provider; source->owner = target;
                ok = application_startup_program_prepare(app, flow->candidate, &previous,
                    &source->owner, &source->program, error);
            }
            continue;
        }
        qa_catalog *catalog = qa_launch_instance_catalog(provider->launch);
        if (!catalog) catalog = qa_launch_snapshot_catalog(flow->candidate);
        const qa_product *product = qa_catalog_product(catalog, provider->launch->selection.product);
        qa_console *console = NULL;
        qa_cvars *cvars = NULL;
        qa_command_context command = {0};
        ok = product && application_provider_console_prepare(app, provider,
            flow->publication->initial_world ? flow->publication->initial_world : app->world,
            catalog, product, choices,
            &console, &cvars, &command, error);
        if (!ok) break;
        if (provider->kind == APPLICATION_PROVIDER_Q2) {
            application_q2_source_scripts scripts = {.context = provider,
                .read = q2_read, .release = q2_release, .complete = q2_complete};
            ok = application_native_q2_console_scripts(provider, &scripts, error);
        }
        for (size_t source_index = 0; ok; ++source_index) {
            qa_application_startup_source physical;
            bool found;
            ok = application_provider_startup_source_at(provider, source_index, &physical, &found, error);
            if (!ok || !found) break;
            const qa_launch_instance *selected = qa_launch_snapshot_find(flow->candidate,
                physical.descriptor->selection.instance);
            if (!selected || selected->state != provider || selected->storage != physical.descriptor->storage ||
                !physical.console || !physical.cvars || physical.scope.provider != provider->owner ||
                !physical.declaration_owner) {
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Startup enumeration lost its actual physical receiver");
                break;
            }
            physical.descriptor = selected;
            startup_source *source = append_source(flow, error);
            if (!source) { ok = false; break; }
            source->provider = provider; source->owner = physical;
            if (flow->hooks.program_source) {
                qa_application_startup_source previous;
                bool inherited = false;
                ok = flow->hooks.program_source(flow->hooks.context, app, flow->candidate,
                    &source->owner, &previous, &inherited, error);
                if (ok && inherited)
                    ok = application_startup_program_prepare(app, flow->candidate, &previous,
                        &source->owner, &source->program, error);
            }
            ok = ok && qa_application_capture_command_context(app, &source->owner.command, &source->owner.command, error) &&
                flow->hooks.prepare_source(flow->hooks.context, app, flow->candidate,
                    &source->owner, &source->phase, error);
            if (ok && !source->phase)
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Startup factory returned no retained phase");
            if (ok) {
                qa_application_startup_source refreshed;
                ok = application_provider_startup_source_at(provider, source_index, &refreshed, &found, error);
                if (ok && (!found || refreshed.console != physical.console ||
                    refreshed.descriptor->storage != selected->storage ||
                    refreshed.scope.provider != physical.scope.provider || refreshed.scope.kind != physical.scope.kind ||
                    refreshed.scope.seat != physical.scope.seat || !refreshed.cvars))
                    ok = application_fail(error, QA_ERROR_ARGUMENT, "Startup configuration replaced its physical console authority");
                if (ok) {
                    source->owner.cvars = refreshed.cvars;
                    if (source->program) ok = application_startup_program_refresh(source->program, &source->owner, error);
                }
            }
        }
    }
    app->routing_snapshot = routing;
    app->routing_providers = providers;
    app->routing_provider_count = count;
    flow->advancing = false;
    if (!ok) abort_failed_startup(app, error);
    return ok;
}

bool application_startup_flow_begin(qa_application *app, const qa_launch_draft *draft, qa_error *error)
{ return begin(app, draft, false, error); }

bool application_startup_flow_begin_replacing(qa_application *app, const qa_launch_draft *draft, qa_error *error)
{ return begin(app, draft, true, error); }

bool qa_application_startup_advance(qa_application *app, bool *complete, qa_error *error)
{
    if (!app || !complete || app->operation != APPLICATION_IDLE || app->frame_preparing ||
        app->q3_round_active || app->destroy_requested || qa_application_should_stop(app))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup frame requires its idle application driver");
    *complete = false;
    struct application_startup_flow *flow = app->startup_flow;
    if (!flow) { *complete = true; return true; }
    if (flow->advancing || flow->cancelling || flow->generation != app->command_generation ||
        !source_consoles_idle(flow))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup frame lost its retained candidate lifetime");
    if (!candidate_callbacks_idle(app, flow, error)) return false;
    flow->advancing = true;
    app->operation = APPLICATION_CONFIGURING;
    bool ok = true;
    while (ok && flow->index < flow->count) {
        startup_source *source = flow->sources + flow->index;
        if (!source->phase) { ++flow->index; continue; }
        bool done = false;
        ok = flow->hooks.advance_source(flow->hooks.context, source->phase, source->owner.console, &done, error);
        if (!ok || !done) break;
        if (!qa_console_idle(source->owner.console) || qa_console_pending(source->owner.console)) {
            ok = application_fail(error, QA_ERROR_ARGUMENT,
                "Startup phase completion requires its drained physical console");
            break;
        }
        ++flow->index;
    }
    bool candidate_complete = flow->hooks.advance_candidate == NULL;
    if (ok && flow->index == flow->count && flow->hooks.advance_candidate)
        ok = flow->hooks.advance_candidate(flow->hooks.context, app, flow->candidate,
            &candidate_complete, error);
    if (ok && flow->index == flow->count && candidate_complete) {
        ok = flow->hooks.prepare_candidate(flow->hooks.context, app, flow->candidate, error);
        if (ok) flow->configured = true;
        for (size_t i = 0; ok && i < flow->count; ++i)
            if (flow->sources[i].phase && flow->sources[i].provider->kind == APPLICATION_PROVIDER_Q2)
                ok = application_native_q2_console_scripts(flow->sources[i].provider, NULL, error);
        if (ok) ok = release_phases(flow, error);
        if (ok) ok = qa_configuration_validate(flow->transaction, error);
        for (size_t i = 0; ok && i < flow->count; ++i)
            ok = application_startup_program_preflight(flow->sources[i].program, error);
        for (size_t i = 0; ok && i < flow->count; ++i)
            ok = application_startup_program_seal(flow->sources[i].program, error);
        if (ok) ok = qa_configuration_commit(flow->transaction, error);
        if (ok) {
            flow->transaction = NULL;
            flow->candidate = NULL;
            flow->committed = true;
            bool published = app->state != QA_APPLICATION_FAULTED;
            finish_candidate(app, flow, qa_configuration_current(app->configuration), published);
            application_q3_product_finish(app, &flow->product, published);
            application_map_load_finish(app, published);
            for (size_t i = 0; ok && i < flow->count; ++i)
                ok = published ? application_startup_program_adopt(&flow->sources[i].program, error)
                    : application_startup_program_abort(&flow->sources[i].program, error);
            if (!ok) application_fault(app, error);
            if (ok) ok = application_q3_campaign_launch_finish(app, published, error);
            if (ok) {
                app->startup_flow = NULL;
                free(flow->sources); free(flow);
                if (app->state == QA_APPLICATION_READY) app->state = QA_APPLICATION_RUNNING;
                *complete = true;
            }
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

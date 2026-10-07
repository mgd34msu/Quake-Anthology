#include "startup_flow.h"
#include "map_travel_private.h"
#include "q3_product.h"
#include "q3_campaign_launch.h"
#include "native_q2_console.h"
#include "native_q1_console.h"
#include "native_q3_console.h"
#include "native_q3_remote_role.h"
#include "startup_program.h"
#include "engine_shutdown.h"
#include "rankings.h"
#include "guest_qc_original_save.h"
#include "guest_native_q2_original_save.h"
#include "qa/console_cvar_observer.h"
#include <stdlib.h>

typedef struct startup_source {
    application_provider *provider;
    qa_application_startup_source owner;
    bool programmed;
    void *phase;
} startup_source;

struct application_startup_flow {
    qa_application_startup_hooks hooks;
    application_q3_product_preparation product;
    qa_configuration_transaction *transaction;
    application_publication *publication;
    application_publication *validated_publication;
    application_startup_program *program;
    const qa_launch_snapshot *candidate, *previous;
    qa_console *console;
    qa_cvars *cvars;
    qa_command_context root_command;
    const qa_launch_snapshot *resources_candidate;
    void *resources;
    startup_source *sources;
    size_t count, index, capacity;
    uint64_t generation;
    bool advancing, configured, validated, candidate_finished, cancelling, committed;
    bool resources_ready, resources_consumed, resources_finished;
    bool resources_consuming, resources_finishing;
    bool resource_advancing, resource_waiting;
    bool preparing_resources;
    bool images_advancing, images_waiting, images_completed;
    bool candidate_abort_refused;
    bool engine_only, root_admitted, root_preparing, root_prepared, root_settled;
    application_provider *root_definition_provider;
};

static bool source_consoles_idle(const struct application_startup_flow *);

bool qa_application_startup_pending(const qa_application *app)
{ return app && app->startup_flow; }

const qa_launch_snapshot *qa_application_startup_candidate(const qa_application *app)
{ return app && app->startup_flow ? app->startup_flow->candidate : NULL; }

bool qa_application_startup_root_read(const qa_application *app,
    const qa_launch_snapshot *candidate, qa_console **console, qa_cvars **cvars,
    qa_command_context *command, qa_error *error)
{
    const struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow || !flow->hooks.prepare_root || (!candidate && !flow->engine_only) ||
        !flow->root_admitted ||
        !flow->console || !flow->cvars || app->console != flow->console || app->cvars != flow->cvars ||
        app->engine_shutdown || !app->session || flow->root_command.owner ||
        flow->root_command.registry != qa_actors_identity(qa_session_actors(app->session)) ||
        flow->root_command.generation != flow->generation)
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE root read lost its retained physical startup owner");
    const qa_launch_snapshot *current = qa_configuration_current(app->configuration);
    bool associated;
    if (flow->engine_only)
        associated = !candidate && !flow->candidate && !flow->transaction && !flow->publication &&
            !flow->validated_publication && current == flow->previous;
    else if (flow->committed)
        associated = candidate && flow->resources_consumed && flow->resources_candidate == candidate &&
            current == candidate;
    else
        associated = candidate && flow->candidate == candidate && flow->transaction &&
            ((qa_configuration_candidate(flow->transaction) == candidate && current == flow->previous) ||
             (flow->validated_publication && flow->validated_publication->candidate == candidate &&
              current == candidate));
    if (!associated)
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE root read selected another actual startup operation");
    if (console) *console = flow->console;
    if (cvars) *cvars = flow->cvars;
    if (command) *command = flow->root_command;
    return true;
}

bool qa_application_startup_root_phase(const qa_application *app,
    const qa_launch_snapshot *candidate)
{
    const struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!qa_application_startup_root_read(app, candidate, NULL, NULL, NULL, NULL) ||
        flow->generation != app->command_generation || flow->committed || flow->cancelling ||
        flow->candidate_finished || flow->resources_consumed || app->publication_started ||
        app->frame_preparing || app->q3_round_active || app->destroy_requested ||
        qa_application_should_stop(app) || qa_configuration_current(app->configuration) != flow->previous ||
        (!flow->engine_only && qa_configuration_candidate(flow->transaction) != candidate) ||
        !source_consoles_idle(flow) || !qa_console_idle(flow->console)) return false;
    return (app->operation == APPLICATION_CONFIGURING && flow->advancing &&
        (flow->root_preparing || flow->images_advancing || flow->resource_advancing)) ||
        (app->operation == APPLICATION_IDLE && !flow->advancing &&
         (flow->images_waiting || flow->resource_waiting));
}

bool qa_application_startup_root_definition_phase(const qa_application *app,
    const qa_launch_snapshot *candidate)
{
    const struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    application_provider *provider = flow ? flow->root_definition_provider : NULL;
    if (!provider || !candidate || !flow->root_prepared || !flow->advancing ||
        app->operation != APPLICATION_CONFIGURING || flow->generation != app->command_generation ||
        flow->configured || flow->validated || flow->committed || flow->cancelling ||
        flow->resources || flow->resources_consumed || app->publication_started ||
        app->frame_preparing || app->q3_round_active || app->destroy_requested ||
        qa_application_should_stop(app) || !flow->publication ||
        flow->publication->candidate != candidate || flow->publication->published ||
        provider->application != app || provider->kind != APPLICATION_PROVIDER_Q3 ||
        provider->constructed || provider->attached || provider->close_pending || !provider->launch ||
        !qa_application_startup_root_read(app, candidate, NULL, NULL, NULL, NULL) ||
        !qa_console_idle(flow->console) || !source_consoles_idle(flow)) return false;
    const qa_launch_instance *selected = qa_launch_snapshot_find(candidate,
        provider->launch->selection.instance);
    if (!selected || selected->state != provider || selected->storage != provider->launch->storage)
        return false;
    for (size_t i = 0; i < flow->publication->next_count; ++i)
        if (flow->publication->next[i] == provider) return true;
    return false;
}

bool application_startup_root_register(application_provider *provider, const char *name,
    const char *value, uint32_t flags, uint64_t owner, qa_cvar_save_policy policy, qa_error *error)
{
    qa_application *app = provider ? provider->application : NULL;
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!app || !app->cvars)
        return application_fail(error, QA_ERROR_ARGUMENT, "Shared definition lost its actual ENGINE registry");
    if (!flow || !flow->hooks.prepare_root)
        return qa_cvars_register(app->cvars, name, value, flags, owner, NULL, error) &&
            qa_cvars_declare_save_policy(app->cvars, name, policy, error);
    if (flow->root_definition_provider)
        return application_fail(error, QA_ERROR_ARGUMENT, "Shared definition already has an entered source owner");
    flow->root_definition_provider = provider;
    qa_cvars_edit *edit = qa_cvars_prepared_edit(flow->cvars);
    bool ok = qa_application_startup_root_definition_phase(app, flow->candidate);
    if (!ok)
        application_fail(error, QA_ERROR_ARGUMENT, "Shared definition lost its actual preparing source and ENGINE root");
    if (ok) {
        if (edit) {
            ok = qa_cvars_edit_apply(edit,
                &(qa_cvars_edit_command){.kind = QA_CVARS_EDIT_REGISTER,
                    .name = name, .value = value, .flags = flags, .owner = owner, .save_policy = policy}, error);
        } else ok = qa_cvars_register(flow->cvars, name, value, flags, owner, NULL, error) &&
            qa_cvars_declare_save_policy(flow->cvars, name, policy, error);
    }
    flow->root_definition_provider = NULL;
    return ok;
}

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

static startup_source *source_at(application_provider *provider, const qa_console *console, uint64_t view)
{
    struct application_startup_flow *flow = provider && provider->application
        ? provider->application->startup_flow : NULL;
    if (!flow || application_startup_flow_provider(provider->application, provider->owner) != provider)
        return NULL;
    for (size_t i = 0; i < flow->count; ++i)
        if (flow->sources[i].provider == provider && flow->sources[i].owner.console) {
            startup_source *source = flow->sources + i;
            if ((!console || source->owner.console == console) &&
                (view ? source->owner.command.cvar_view == view : game_scope(source->owner.scope.kind)))
                return source;
        }
    return NULL;
}

bool qa_application_startup_source_read(qa_application *app, const qa_launch_snapshot *snapshot,
    const qa_launch_instance *instance, qa_application_startup_source *out,
    qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!app || !snapshot || !instance || !out ||
        (snapshot != qa_configuration_current(app->configuration) &&
         snapshot != app->routing_snapshot && (!flow || snapshot != flow->candidate)) ||
        qa_launch_snapshot_find(snapshot, instance->selection.instance) != instance)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup source read requires its retained physical candidate");
    application_provider *provider = instance->state;
    if (!provider || provider->application != app || provider->close_pending)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup source read lost its actual provider");
    startup_source *source = source_at(provider, NULL, 0);
    if (source) {
        *out = source->owner;
        out->descriptor = instance;
        return true;
    }
    application_publication *publication = app->startup_publication;
    bool prepared = false;
    if (publication && publication->candidate == snapshot && !publication->published)
        for (size_t i = 0; i < publication->next_count; ++i)
            if (publication->next[i] == provider) { prepared = true; break; }
    if (!provider->constructed || (!provider->attached && !prepared))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup source has no prepared or installed physical console");
    qa_application_startup_source physical;
    bool found = false;
    for (size_t i = 0;; ++i) {
        bool present;
        if (!application_provider_startup_source_at(provider, i, &physical, &present, error)) return false;
        if (!present) break;
        if (game_scope(physical.scope.kind)) { found = true; break; }
    }
    if (!found)
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected physical source has no genuine GAME console");
    application_provider *prior = app->startup_preinit_provider;
    if (!provider->attached) app->startup_preinit_provider = provider;
    bool captured = qa_application_capture_command_context(app, &physical.command, &physical.command, error);
    app->startup_preinit_provider = prior;
    if (captured) {
        physical.descriptor = instance;
        *out = physical;
    }
    return captured;
}

bool application_startup_source_active(const application_provider *provider)
{ return application_startup_console_active(provider, NULL); }

bool application_startup_console_active(const application_provider *provider, const qa_cvars *cvars)
{
    startup_source *source = source_at((application_provider *)provider, NULL, qa_cvars_view_identity(cvars));
    return source && source->phase;
}

bool application_startup_command_allowed(application_provider *provider, const qa_command_invocation *command)
{ return application_startup_console_command_allowed(provider, NULL, command); }

bool application_startup_console_command_allowed(application_provider *provider,
    const qa_console *console, const qa_command_invocation *command)
{
    startup_source *source = source_at(provider, console, command ? command->context.cvar_view : 0);
    if (!source || !source->phase) return true;
    struct application_startup_flow *flow = provider->application->startup_flow;
    return !flow->hooks.allow_command || flow->hooks.allow_command(flow->hooks.context, source->phase, command);
}

bool qa_application_startup_replay_variables(qa_application *app, qa_console *console,
    const qa_command_context *command, qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow || !console || !command) return application_fail(error, QA_ERROR_ARGUMENT, "Variable replay needs its retained source console and command");
    if (!flow->images_completed)
        return application_fail(error, QA_ERROR_ARGUMENT, "Variable replay precedes the actual completed image-settings programme");
    if (flow->engine_only) {
        qa_console *actual_console = NULL;
        qa_cvars *registry = NULL;
        qa_command_context actual;
        if (!qa_application_startup_bootstrap_images_ready(app) ||
            !qa_application_startup_root_read(app, NULL, &actual_console, &registry, &actual, error) ||
            console != actual_console || command->session != actual.session ||
            command->owner != actual.owner || command->client != actual.client ||
            command->seat != actual.seat || command->origin != actual.origin ||
            command->dialect != actual.dialect || command->cvar_view != actual.cvar_view ||
            command->registry != actual.registry || command->generation != actual.generation ||
            command->console_text != actual.console_text || command->direct != actual.direct ||
            command->script || !qa_actor_id_equal(command->actor, actual.actor) ||
            !qa_cvars_edit_returned_is(qa_cvars_prepared_edit(registry), registry))
            return application_fail(error, QA_ERROR_ARGUMENT, "Variable replay lost its returned ENGINE bootstrap context");
        return application_startup_seed_root(app, console, &actual, error);
    }
    const qa_launch_snapshot *routing = app->routing_snapshot;
    application_provider **providers = app->routing_providers;
    size_t count = app->routing_provider_count;
    app->routing_snapshot = flow->candidate;
    app->routing_providers = flow->publication ? flow->publication->next : NULL;
    app->routing_provider_count = flow->publication ? flow->publication->next_count : 0;
    startup_source *source = flow->sources + flow->index;
    bool ok = application_startup_seed_console(source->provider, &source->owner, command, error);
    app->routing_snapshot = routing;
    app->routing_providers = providers;
    app->routing_provider_count = count;
    return ok;
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
    startup_source *source = source_at(provider, console, command ? command->cvar_view : 0);
    if (!source || !source->phase || !qa_application_command_context_active(provider->application, command))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup script lost its retained source phase");
    struct application_startup_flow *flow = provider->application->startup_flow;
    return flow->hooks.read_script(flow->hooks.context, source->phase, command, name, out, lease, error);
}

void application_startup_script_release(application_provider *provider, void *lease)
{ application_startup_console_script_release(provider, NULL, lease); }

void application_startup_console_script_release(application_provider *provider,
    const qa_cvars *cvars, void *lease)
{
    startup_source *source = source_at(provider, NULL, qa_cvars_view_identity(cvars));
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
    startup_source *source = source_at(provider, console, command ? command->cvar_view : 0);
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
    flow->validated_publication = publication;
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

static bool release_sources(application_provider *provider, const qa_cvars *cvars, qa_error *error)
{
    struct application_startup_flow *flow = provider && provider->application
        ? provider->application->startup_flow : NULL;
    if (!flow) return true;
    for (size_t i = flow->count; i-- > 0;) {
        startup_source *source = flow->sources + i;
        if (source->provider != provider || (cvars && source->owner.cvars!=cvars)) continue;
        if (source->phase)
            return application_fail(error, QA_ERROR_ARGUMENT, "Source teardown requires its returned configuration phase");
    }
    for (size_t i = 0; i < flow->count; ++i)
        if (flow->sources[i].provider == provider && (!cvars || flow->sources[i].owner.cvars==cvars)) {
            flow->sources[i].owner.console = NULL;
            flow->sources[i].owner.cvars = NULL;
        }
    return true;
}

bool application_startup_flow_release_provider(application_provider *provider, qa_error *error)
{ return release_sources(provider,NULL,error); }

bool application_startup_flow_release_view(application_provider *provider,
    const qa_cvars *cvars, qa_error *error)
{ return release_sources(provider,cvars,error); }

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
    if (!flow->committed && (flow->candidate || flow->engine_only) && flow->hooks.candidate_languages &&
        !flow->hooks.candidate_languages(flow->hooks.context, app, flow->candidate, &languages, &count, error))
        return false;
    return application_guests_languages_idle(app, languages, count) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Startup candidate still has unreturned source callbacks or unowned language admissions");
}

bool qa_application_startup_images_phase(const qa_application *app,
    const qa_launch_snapshot *candidate, const qa_application_startup_source *linked)
{
    const struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow || !candidate || flow->candidate != candidate || !linked || !linked->descriptor ||
        !flow->hooks.advance_images || flow->images_completed || flow->index || flow->configured ||
        flow->validated || flow->committed || flow->cancelling || flow->candidate_finished ||
        !flow->transaction || qa_configuration_candidate(flow->transaction) != candidate ||
        qa_configuration_current(app->configuration) != flow->previous ||
        flow->generation != app->command_generation || app->publication_started ||
        app->frame_preparing || app->q3_round_active || app->destroy_requested ||
        app->engine_shutdown || qa_application_should_stop(app) || flow->resources ||
        !flow->console || !flow->cvars || app->console != flow->console || app->cvars != flow->cvars ||
        !source_consoles_idle(flow) || !qa_console_idle(app->console)) return false;
    if (!((app->operation == APPLICATION_CONFIGURING && flow->advancing && flow->images_advancing) ||
          (app->operation == APPLICATION_IDLE && !flow->advancing && flow->images_waiting))) return false;
    for (size_t i = 0; i < flow->count; ++i) {
        const qa_application_startup_source *actual = &flow->sources[i].owner;
        const qa_command_context *a = &actual->command, *b = &linked->command;
        if (actual->descriptor->storage == linked->descriptor->storage &&
            actual->console == linked->console && actual->cvars == linked->cvars &&
            actual->scope.provider == linked->scope.provider && actual->scope.kind == linked->scope.kind &&
            actual->scope.seat == linked->scope.seat && actual->declaration_owner == linked->declaration_owner &&
            a->cvar_view == b->cvar_view && a->owner == b->owner && a->session == b->session && a->client == b->client &&
            a->seat == b->seat && a->origin == b->origin && a->dialect == b->dialect &&
            a->registry == b->registry && a->generation == b->generation &&
            a->console_text == b->console_text && a->script == b->script &&
            qa_actor_id_equal(a->actor, b->actor)) return true;
    }
    return false;
}

static bool resource_phase_current(const qa_application *app,
    const qa_launch_snapshot *candidate)
{
    const struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow || (!candidate && !flow->engine_only) || flow->candidate != candidate || flow->committed ||
        flow->cancelling || flow->candidate_finished || flow->index != flow->count ||
        (flow->engine_only ? (!flow->root_prepared || flow->transaction || flow->publication ||
            flow->validated_publication) : (!flow->transaction ||
            qa_configuration_candidate(flow->transaction) != candidate)) ||
        qa_configuration_current(app->configuration) != flow->previous ||
        flow->generation != app->command_generation || flow->resources_consumed ||
        app->publication_started || app->frame_preparing || app->q3_round_active ||
        app->destroy_requested || app->engine_shutdown || !flow->console || !flow->cvars ||
        app->console != flow->console || app->cvars != flow->cvars ||
        !source_consoles_idle(flow) || !qa_console_idle(app->console)) return false;
    return true;
}

bool qa_application_startup_resource_phase_associated(const qa_application *app,
    const qa_launch_snapshot *candidate)
{
    if (!resource_phase_current(app, candidate) || qa_application_should_stop(app)) return false;
    const struct application_startup_flow *flow = app->startup_flow;
    if (app->operation == APPLICATION_IDLE && !flow->advancing && flow->resource_waiting) return true;
    if (app->operation != APPLICATION_CONFIGURING || !flow->advancing) return false;
    if (flow->resource_advancing) return true;
    if (flow->engine_only)
        return flow->root_settled && flow->configured && flow->resources_candidate == candidate &&
            (flow->preparing_resources || flow->resources);
    const application_publication *publication = flow->validated_publication;
    return flow->validated && flow->configured && flow->resources_candidate == candidate &&
        (flow->preparing_resources || flow->resources) && publication &&
        publication->candidate == candidate && publication->previous == flow->previous &&
        !publication->published && !publication->failed_retained;
}

static bool entered_resources_current(const qa_application *app,
    const qa_launch_snapshot *candidate)
{
    const struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    const application_publication *publication = flow ? flow->validated_publication : NULL;
    return flow && (candidate || flow->engine_only) && flow->resources && flow->resources_candidate == candidate &&
        flow->resources_consumed && !flow->resources_finished && flow->configured &&
        (flow->engine_only ? (flow->root_settled && !publication && !flow->transaction &&
            qa_configuration_current(app->configuration) == flow->previous)
          : (flow->validated && publication && publication->candidate == candidate &&
            publication->previous == flow->previous && qa_configuration_current(app->configuration) == candidate)) &&
        flow->generation == app->command_generation && !app->engine_shutdown &&
        flow->console && flow->cvars && app->console == flow->console && app->cvars == flow->cvars &&
        source_consoles_idle(flow) && qa_console_idle(app->console);
}

bool qa_application_startup_publication_consuming(const qa_application *app,
    const qa_launch_snapshot *candidate)
{
    if (!entered_resources_current(app, candidate)) return false;
    const struct application_startup_flow *flow = app->startup_flow;
    if (flow->engine_only)
        return flow->resources_consuming && !flow->resources_finishing && flow->advancing &&
            !flow->cancelling && !flow->candidate_finished && flow->root_prepared &&
            app->operation == APPLICATION_CONFIGURING && !app->destroy_requested &&
            !app->frame_preparing && !app->q3_round_active;
    return flow->resources_consuming && !flow->resources_finishing && flow->advancing &&
        !flow->cancelling && !flow->committed && !flow->candidate_finished &&
        flow->candidate == candidate && flow->transaction &&
        app->operation == APPLICATION_CONFIGURING && app->publication_started &&
        !flow->validated_publication->published && !flow->validated_publication->failed_retained &&
        !app->destroy_requested && !app->frame_preparing && !app->q3_round_active;
}

bool qa_application_startup_publication_cleanup(const qa_application *app,
    const qa_launch_snapshot *candidate)
{
    return entered_resources_current(app, candidate) && app->startup_flow->resources_finishing &&
        !app->startup_flow->resources_consuming;
}

const qa_launch_snapshot *qa_application_startup_publication_previous(const qa_application *app,
    const qa_launch_snapshot *candidate)
{
    return qa_application_startup_resource_phase_associated(app, candidate) ||
        qa_application_startup_publication_consuming(app, candidate) ||
        qa_application_startup_publication_cleanup(app, candidate)
        ? app->startup_flow->previous : NULL;
}

static bool resource_phase_ready(const qa_application *app,
    const qa_launch_snapshot *candidate, bool cleanup)
{
    if (!resource_phase_current(app, candidate)) return false;
    struct application_startup_flow *flow = app->startup_flow;
    if (cleanup ? (app->operation != APPLICATION_IDLE || flow->advancing ||
            !flow->resource_waiting || flow->resources || flow->preparing_resources ||
            app->state == QA_APPLICATION_FAULTED)
        : !qa_application_startup_resource_phase_associated(app, candidate)) return false;
    if (!app->session ||
        !qa_session_safe(app->session) || (app->world && !qa_world_idle(app->world)) ||
        !application_bots_can_destroy(app) || !application_rankings_idle(app) ||
        !qa_inventory_idle(app->inventory) || (app->combat && !qa_combat_idle(app->combat)) ||
        (app->pickups && !qa_pickups_idle(app->pickups)) ||
        (app->modes && !qa_modes_idle(app->modes))) return false;
    qa_error error = {0};
    qa_cvars_edit *values = NULL;
    if (flow->hooks.candidate_values &&
        !flow->hooks.candidate_values(flow->hooks.context, app, candidate, &values, &error))
        return false;
    if (values ? !qa_cvars_edit_returned_is(values, app->cvars)
               : !qa_cvars_observer_idle(app->cvars)) return false;
    return candidate_callbacks_idle((qa_application *)app, flow, &error);
}

bool qa_application_startup_resource_phase(const qa_application *app,
    const qa_launch_snapshot *candidate)
{ return resource_phase_ready(app, candidate, false); }

bool qa_application_startup_release_cleanup_phase(const qa_application *app,
    const qa_launch_snapshot *candidate)
{ return resource_phase_ready(app, candidate, true); }

bool application_startup_flow_configuration_idle(const qa_application *app)
{
    const struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow) return application_guests_idle(app);
    if (!flow->resources) {
        if (flow->resources_consumed && !flow->resources_finished) return false;
        qa_cvars_edit *values = NULL;
        qa_error error = {0};
        if ((flow->candidate || flow->engine_only) && flow->hooks.candidate_values &&
            !flow->hooks.candidate_values(flow->hooks.context, app, flow->candidate, &values, &error)) return false;
        if (!values) return application_guests_idle(app);
        return flow->advancing && flow->configured && !flow->cancelling && !flow->committed &&
            app->operation == APPLICATION_CONFIGURING && flow->generation == app->command_generation &&
            qa_configuration_current(app->configuration) == flow->previous && flow->transaction &&
            qa_configuration_candidate(flow->transaction) == flow->candidate &&
            qa_cvars_edit_returned_is(values, app->cvars) &&
            candidate_callbacks_idle((qa_application *)app, (struct application_startup_flow *)flow, &error);
    }
    if (!flow->advancing || flow->cancelling || flow->committed || !flow->resources_ready ||
        flow->resources_consumed || !flow->validated_publication ||
        flow->validated_publication->candidate != flow->candidate ||
        flow->validated_publication->previous != flow->previous ||
        flow->validated_publication->published || flow->validated_publication->failed_retained ||
        qa_configuration_current(app->configuration) != flow->previous ||
        qa_configuration_candidate(flow->transaction) != flow->candidate ||
        flow->resources_candidate != flow->candidate || app->operation != APPLICATION_CONFIGURING ||
        flow->generation != app->command_generation ||
        !flow->hooks.owned_publication_ready(flow->hooks.context, app, flow->candidate, flow->resources))
        return false;
    const qa_application_language_ticket *const *languages = NULL;
    size_t count = 0;
    qa_error error = {0};
    if (flow->hooks.candidate_languages &&
        !flow->hooks.candidate_languages(flow->hooks.context, (qa_application *)app,
            flow->candidate, &languages, &count, &error)) return false;
    return application_guests_languages_idle(app, languages, count);
}

bool application_startup_flow_retirement_ready(const qa_application *app,
    const qa_launch_snapshot *candidate, const qa_cvars_edit *values, qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow || (!candidate && !flow->engine_only) || !values || flow->candidate != candidate ||
        flow->advancing || !flow->cancelling || !flow->candidate_abort_refused ||
        flow->committed || flow->candidate_finished || flow->resources || flow->resources_consumed ||
        (flow->engine_only ? (!flow->root_admitted || flow->transaction || flow->publication ||
            flow->validated_publication) : (!flow->transaction ||
            qa_configuration_candidate(flow->transaction) != candidate)) ||
        qa_configuration_current(app->configuration) != flow->previous ||
        flow->generation != app->command_generation || !source_consoles_idle(flow) ||
        !flow->hooks.candidate_values || !flow->hooks.candidate_retirement_ready)
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Candidate ENGINE retirement requires its refused retained cancellation");
    qa_cvars_edit *owned = NULL;
    if (!flow->hooks.candidate_values(flow->hooks.context, app, candidate, &owned, error)) return false;
    if (owned != values || !qa_cvars_edit_abort_is(values, app->cvars))
        return application_fail(error, QA_ERROR_ARGUMENT,
            "Candidate ENGINE retirement lost its actual returned canonical edit");
    return candidate_callbacks_idle((qa_application *)app, flow, error) &&
        flow->hooks.candidate_retirement_ready(flow->hooks.context, app, candidate, values, error);
}

static bool finish_resources(qa_application *app, struct application_startup_flow *flow, qa_error *error)
{
    if (flow->resources_finished) return true;
    if (!flow->resources)
        return application_fail(error, QA_ERROR_ARGUMENT, "Entered publication lost its resource cleanup receipt");
    bool complete = false;
    flow->resources_finishing = true;
    bool ok = flow->hooks.finish_publication(flow->hooks.context, app, flow->resources_candidate,
        &flow->resources, &complete, error);
    flow->resources_finishing = false;
    if (complete && flow->resources)
        return application_fail(error, QA_ERROR_ARGUMENT, "Completed publication cleanup retained its resource owner");
    if (complete) flow->resources_finished = true;
    return ok && (complete || application_fail(error, QA_ERROR_ARGUMENT,
        "Entered publication retains unfinished resource cleanup"));
}

bool application_startup_flow_consume_publication(qa_application *app, application_publication *publication,
    const qa_launch_snapshot *candidate, qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow || !flow->hooks.prepare_publication) return true;
    if (!flow->advancing || !flow->validated || !flow->configured ||
        flow->validated_publication != publication ||
        flow->candidate != candidate || qa_configuration_current(app->configuration) != candidate ||
        !app->publication_started || flow->generation != app->command_generation)
        return application_fail(error, QA_ERROR_ARGUMENT, "Resource consume lost its actual entered publication ticket");
    if (!flow->resources) {
        qa_cvars_edit *values = NULL;
        if (flow->resources_candidate || flow->resources_consumed || !flow->hooks.candidate_values)
            return application_fail(error, QA_ERROR_ARGUMENT, "Publication lost its actual final resource owner");
        return flow->hooks.candidate_values(flow->hooks.context, app, candidate, &values, error) &&
            ((!values && qa_cvars_observer_idle(app->cvars)) || application_fail(error, QA_ERROR_ARGUMENT,
                "Resource-free publication retains a canonical scalar owner or callback"));
    }
    if (!flow->resources_ready || flow->resources_consumed || flow->resources_candidate != candidate)
        return application_fail(error, QA_ERROR_ARGUMENT, "Resource consume lost its prepared actual bundle");
    flow->resources_consumed = true;
    flow->resources_consuming = true;
    flow->hooks.consume_publication(flow->hooks.context, app, candidate, flow->resources);
    flow->resources_consuming = false;
    return finish_resources(app, flow, error);
}

bool application_startup_flow_cleanup_publication(qa_application *app, application_publication *publication,
    qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow || flow->validated_publication != publication) return true;
    if (flow->resources_consumed) {
        if (flow->resources_finished) return true;
        if (app->publication_started)
            return application_fail(error, QA_ERROR_ARGUMENT, "Entered resources await returned publication cleanup");
        return finish_resources(app, flow, error);
    }
    if (!flow->resources) return true;
    return flow->hooks.abort_publication(flow->hooks.context, app, flow->resources_candidate,
        &flow->resources, error) && (!flow->resources || application_fail(error, QA_ERROR_ARGUMENT,
            "Aborted publication retains its resource owner"));
}

bool qa_application_startup_abort(qa_application *app, qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!flow) return true;
    if (flow->advancing || !source_consoles_idle(flow))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup cancellation requires returned source callbacks");
    if (!candidate_callbacks_idle(app, flow, error)) return false;
    flow->resource_waiting = false;
    flow->images_waiting = false;
    flow->advancing = true;
    flow->cancelling = true;
    if (!flow->committed) {
        if (!application_startup_program_abort(&flow->program, error)) goto fail;
        if (!release_phases(flow, error)) goto fail;
        application_publication *publication = flow->validated_publication ? flow->validated_publication : flow->publication;
        if (publication && !application_publication_discard_sources(app, publication, error)) goto fail;
    }
    if (flow->resources || (flow->resources_consumed && !flow->resources_finished)) {
        if (flow->resources_consumed) {
            if (!finish_resources(app, flow, error)) goto fail;
        } else {
            if (!flow->hooks.abort_publication(flow->hooks.context, app, flow->resources_candidate,
                &flow->resources, error)) goto fail;
            if (flow->resources) {
                application_fail(error, QA_ERROR_ARGUMENT, "Aborted publication retains its resource owner");
                goto fail;
            }
        }
    }
    if (!flow->committed && flow->hooks.abort_candidate) {
        if (!flow->hooks.abort_candidate(flow->hooks.context, app, flow->candidate, error)) {
            flow->candidate_abort_refused = true;
            goto fail;
        }
        flow->candidate_abort_refused = false;
    }
    if (!application_guests_idle(app)) {
        application_fail(error, QA_ERROR_ARGUMENT, "Startup cancellation retains settings or language children");
        goto fail;
    }
    if (!application_startup_program_abort(&flow->program, error)) goto fail;
    if (flow->committed) {
        if (flow->engine_only) finish_candidate(app, flow, NULL, true);
        else if (!application_q3_campaign_launch_finish(app, true, error)) goto fail;
        app->startup_flow = NULL;
        free(flow->sources); free(flow);
        return true;
    }
    if (!release_phases(flow, error)) goto fail;
    finish_candidate(app, flow, flow->candidate, false);
    if (flow->engine_only) {
        application_engine_shutdown_release_candidate(app, NULL);
        app->startup_flow = NULL;
        free(flow);
        return true;
    }
    /* Added CLIENT rows belong to the retained candidate until their actual
     * configuration children have retired. The running GAME remains attached. */
    for (size_t i=0; i<qa_launch_snapshot_instance_count(flow->candidate); ++i) {
        application_provider *provider=qa_launch_snapshot_instance(flow->candidate,i)->state;
        if (provider && provider->attached && provider->kind==APPLICATION_PROVIDER_Q3 &&
            !application_native_q3_remote_roles_retain(provider,
                qa_launch_snapshot_choices(flow->previous),error)) goto fail;
    }
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
    application_engine_shutdown_release_candidate(app, flow->candidate);
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

bool qa_application_startup_import_abort(qa_application *app,qa_error *error)
{
    if (!app) return application_fail(error,QA_ERROR_ARGUMENT,"Original import cleanup requires its application");
    if (!app->q1_original_save && !app->q2_original_save) return true;
    if (app->startup_flow || app->operation!=APPLICATION_IDLE || app->q3_round_active ||
        app->frame_preparing || app->publication_started || app->destroy_requested || app->finalizing ||
        !application_guests_idle(app) || !qa_console_idle(app->console) ||
        (app->session && !qa_session_safe(app->session)) || (app->world && !qa_world_idle(app->world)))
        return application_fail(error,QA_ERROR_ARGUMENT,"Original import cleanup requires completed startup cancellation");
    application_q1_original_dispose(app);
    application_q2_original_dispose(app);
    return true;
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

static bool prepare_root(qa_application *app, struct application_startup_flow *flow, qa_error *error)
{
    if (!flow->hooks.prepare_root) return true;
    qa_command_context actual = {.dialect = qa_cvars_dialect(flow->cvars), .origin = QA_COMMAND_LOCAL,
        .cvar_view = qa_cvars_view_identity(flow->cvars)};
    if (!qa_application_capture_command_context(app, &actual, &flow->root_command, error)) return false;
    flow->root_admitted = true;
    flow->root_preparing = true;
    bool ok = flow->hooks.prepare_root(flow->hooks.context, app, flow->candidate,
        flow->console, flow->cvars, &flow->root_command, error);
    flow->root_preparing = false;
    if (ok) flow->root_prepared = true;
    return ok;
}

bool qa_application_startup_bootstrap(qa_application *app, qa_error *error)
{
    const qa_application_startup_hooks *hooks = app ? app->startup_hooks : NULL;
    if (!app || !hooks || !hooks->prepare_root || !hooks->finish_candidate ||
        app->startup_flow || app->client_preparation || app->operation != APPLICATION_IDLE || app->frame_preparing ||
        app->q3_round_active || app->destroy_requested || app->engine_shutdown ||
        app->publication_started || app->failed_publications || qa_application_should_stop(app) ||
        app->live_providers || app->provider_states || app->pending_close || app->q1_original_save || app->q2_original_save ||
        app->routing_snapshot || app->routing_providers || app->routing_provider_count ||
        qa_configuration_current(app->configuration) || !app->console || !app->cvars ||
        !application_guests_idle(app) || !qa_console_idle(app->console) ||
        !qa_cvars_observer_idle(app->cvars) || !app->session || !qa_session_safe(app->session) ||
        (app->world && !qa_world_idle(app->world)) || !application_rankings_idle(app) ||
        !application_bots_can_destroy(app) || !qa_inventory_idle(app->inventory) ||
        (app->combat && !qa_combat_idle(app->combat)) ||
        (app->pickups && !qa_pickups_idle(app->pickups)) ||
        (app->modes && !qa_modes_idle(app->modes)))
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE bootstrap requires its genuine source-free returned application");
    if (!hooks->candidate_values || !hooks->prepare_publication || !hooks->ready_publication ||
        !hooks->owned_publication_ready || !hooks->consume_publication ||
        !hooks->finish_publication || !hooks->abort_publication || !hooks->abort_candidate)
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE bootstrap requires its actual checked scalar and resource owners");
    struct application_startup_flow *flow = calloc(1, sizeof(*flow));
    if (!flow) return application_fail(error, QA_ERROR_MEMORY, "Retaining source-free ENGINE startup");
    flow->hooks = *hooks;
    flow->engine_only = true;
    flow->images_completed = hooks->advance_images == NULL;
    flow->console = app->console;
    flow->cvars = app->cvars;
    flow->generation = app->command_generation;
    flow->previous = qa_configuration_current(app->configuration);
    flow->advancing = true;
    app->startup_flow = flow;
    app->operation = APPLICATION_CONFIGURING;
    bool ok = prepare_root(app, flow, error);
    flow->advancing = false;
    app->operation = APPLICATION_IDLE;
    if (!ok) abort_failed_startup(app, error);
    return ok;
}

static bool prepare_physical_source(qa_application *app, struct application_startup_flow *flow,
    application_provider *provider, const qa_application_startup_source *physical, qa_error *error)
{
    const qa_launch_instance *selected=qa_launch_snapshot_find(flow->candidate,
        physical->descriptor->selection.instance);
    bool ok=true, found=false;
    startup_source *source = append_source(flow, error);
    if (!source) return false;
    source->provider = provider; source->owner = *physical;
    if (flow->hooks.program_source) {
        qa_application_startup_source previous;
        bool inherited = false;
        ok = flow->hooks.program_source(flow->hooks.context, app, flow->candidate,
            &source->owner, &previous, &inherited, error);
        if (ok && inherited) {
            application_startup_program *program=NULL;
            ok = application_startup_program_prepare(app, flow->publication, &previous,
                &source->owner, &program, error);
            source->programmed=ok;
        }
    }
    ok = ok && qa_application_capture_command_context(app, &source->owner.command, &source->owner.command, error) &&
        flow->hooks.prepare_source(flow->hooks.context, app, flow->candidate,
            &source->owner, &source->phase, error);
    if (ok && !source->phase)
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Startup factory returned no retained phase");
    if (ok) {
        qa_application_startup_source refreshed;
        for (size_t index=0; ok; ++index) {
            ok=application_provider_startup_source_at(provider,index,&refreshed,&found,error);
            if (!ok || !found || (refreshed.console==physical->console &&
                refreshed.cvars==physical->cvars && refreshed.scope.kind==physical->scope.kind &&
                refreshed.scope.seat==physical->scope.seat)) break;
        }
        if (ok && (!found || refreshed.console != physical->console ||
            refreshed.descriptor->storage != selected->storage ||
            refreshed.scope.provider != physical->scope.provider || refreshed.scope.kind != physical->scope.kind ||
            refreshed.scope.seat != physical->scope.seat || refreshed.cvars != physical->cvars ||
            refreshed.command.cvar_view != physical->command.cvar_view))
            ok = application_fail(error, QA_ERROR_ARGUMENT, "Startup configuration replaced its physical console authority");
        if (ok) {
            source->owner.declaration_owner = refreshed.declaration_owner;
            if (source->programmed) ok = application_startup_program_refresh(flow->program, &source->owner, error);
        }
    }
    return ok;
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
        app->startup_flow || app->client_preparation)
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup preparation requires complete actual source owners");
    if ((hooks->prepare_publication || hooks->ready_publication || hooks->owned_publication_ready ||
         hooks->consume_publication || hooks->finish_publication || hooks->abort_publication) &&
        (!hooks->prepare_publication || !hooks->ready_publication || !hooks->owned_publication_ready ||
         !hooks->consume_publication || !hooks->finish_publication || !hooks->abort_publication))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup resources require complete checked publication owners");
    struct application_startup_flow *flow = calloc(1, sizeof(*flow));
    if (!flow) return application_fail(error, QA_ERROR_MEMORY, "Retaining startup candidate");
    flow->hooks = *hooks;
    flow->images_completed = hooks->advance_images == NULL;
    flow->console = app->console;
    flow->cvars = app->cvars;
    flow->generation = app->command_generation;
    flow->advancing = true;
    app->startup_flow = flow;
    flow->previous = qa_configuration_current(app->configuration);
    bool ok = application_q3_product_prepare_draft(app, draft, &flow->product, error) &&
        (replacing ? qa_configuration_prepare_replacing(app->configuration, flow->product.draft, &flow->transaction, error)
            : qa_configuration_prepare(app->configuration, flow->product.draft, &flow->transaction, error));
    if (ok) flow->candidate = qa_configuration_candidate(flow->transaction);
    if (ok) ok = prepare_root(app, flow, error);
    if (ok) ok = application_publication_begin(app, flow->previous, flow->candidate, &flow->publication, error);
    if (ok) {
        qa_application_startup_source engine={.scope={.kind=QA_APPLICATION_CONSOLE_ENGINE},
            .console=flow->console,.cvars=flow->cvars,.command=flow->root_command};
        ok=application_startup_program_prepare(app,flow->publication,&engine,&engine,&flow->program,error);
    }
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
    if (ok && provider_count && primary == provider_count)
        ok = application_fail(error, QA_ERROR_ARGUMENT, "Startup candidate has no actual primary map source");
    for (size_t ordinal = 0; ok && ordinal < provider_count; ++ordinal) {
        size_t i = ordinal == 0 ? primary : ordinal <= primary ? ordinal - 1 : ordinal;
        application_provider *provider = flow->publication->next[i];
        if (provider->constructed && provider->attached) {
            if (provider->kind==APPLICATION_PROVIDER_Q3)
                ok=application_native_q3_remote_roles_prepare(provider,choices,error);
            for (size_t index = 0; ok; ++index) {
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
                if (provider->kind==APPLICATION_PROVIDER_Q3 &&
                    target.scope.kind==QA_APPLICATION_CONSOLE_Q3_CGAME &&
                    !application_native_q3_remote_role_selected(selected,choices,target.scope.seat))
                    continue;
                bool new_client=provider->kind==APPLICATION_PROVIDER_Q3 &&
                    target.scope.kind==QA_APPLICATION_CONSOLE_Q3_CGAME &&
                    !application_native_q3_remote_role_selected(selected,
                        qa_launch_snapshot_choices(flow->previous),target.scope.seat);
                if (new_client) {
                    ok=prepare_physical_source(app,flow,provider,&target,error);
                    continue;
                }
                if (!flow->hooks.program_source) continue;
                ok = flow->hooks.program_source(flow->hooks.context, app, flow->candidate,
                    &target, &previous, &inherited, error);
                if (!ok || !inherited) continue;
                startup_source *source = append_source(flow, error);
                if (!source) { ok = false; break; }
                source->provider = provider; source->owner = target;
                application_startup_program *program=NULL;
                ok = application_startup_program_prepare(app, flow->publication, &previous,
                    &source->owner, &program, error);
                source->programmed=ok;
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
            ok=prepare_physical_source(app,flow,provider,&physical,error);
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

static bool advance_images(qa_application *app, struct application_startup_flow *flow,
    bool *complete, qa_error *error)
{
    *complete = flow->images_completed;
    if (*complete) return true;
    flow->images_advancing = true;
    bool ok = flow->hooks.advance_images(flow->hooks.context, app, flow->candidate, complete, error);
    flow->images_advancing = false;
    flow->images_completed = ok && *complete;
    flow->images_waiting = ok && !*complete;
    *complete = flow->images_completed;
    return ok;
}

bool qa_application_startup_bootstrap_images_ready(const qa_application *app)
{
    const struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    return flow && flow->engine_only && flow->root_prepared && flow->images_completed &&
        !flow->advancing && !flow->cancelling && !flow->committed && !flow->candidate_finished &&
        !flow->configured && !flow->root_settled && !flow->resources && !flow->resources_consumed &&
        !flow->resource_waiting && !flow->preparing_resources && !flow->images_waiting &&
        app->operation == APPLICATION_IDLE && !app->frame_preparing && !app->q3_round_active &&
        !app->destroy_requested && !qa_application_should_stop(app) &&
        flow->generation == app->command_generation && qa_console_idle(flow->console) &&
        qa_application_startup_root_read(app, NULL, NULL, NULL, NULL, NULL);
}

bool qa_application_startup_bootstrap_images_advance(qa_application *app,
    bool *complete, qa_error *error)
{
    struct application_startup_flow *flow = app ? app->startup_flow : NULL;
    if (!complete || !flow || !flow->engine_only || !flow->root_prepared ||
        flow->advancing || flow->cancelling || flow->committed || flow->candidate_finished ||
        flow->configured || flow->root_settled || flow->resources || flow->resources_consumed ||
        flow->resource_waiting || flow->preparing_resources ||
        app->operation != APPLICATION_IDLE || app->frame_preparing || app->q3_round_active ||
        app->destroy_requested || qa_application_should_stop(app) ||
        flow->generation != app->command_generation || !qa_console_idle(flow->console) ||
        !qa_application_startup_root_read(app, NULL, NULL, NULL, NULL, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Images-only bootstrap lost its actual returned ENGINE flow");
    *complete = false;
    if (!candidate_callbacks_idle(app, flow, error)) return false;
    flow->advancing = true;
    flow->images_waiting = false;
    app->operation = APPLICATION_CONFIGURING;
    bool ok = advance_images(app, flow, complete, error);
    flow->advancing = false;
    app->operation = APPLICATION_IDLE;
    if (!ok) abort_failed_startup(app, error);
    return ok;
}

static bool advance_root(qa_application *app, struct application_startup_flow *flow,
    bool *complete, qa_error *error)
{
    bool ok = true;
    if (!flow->resources_consumed) {
        if (!flow->configured) {
            bool done = !flow->hooks.advance_candidate;
            flow->resource_advancing = true;
            if (flow->hooks.advance_candidate)
                ok = flow->hooks.advance_candidate(flow->hooks.context, app, NULL, &done, error);
            flow->resource_advancing = false;
            flow->resource_waiting = ok && !done;
            if (!ok || !done) return ok;
            flow->configured = true;
        }
        if (!flow->root_settled) {
            bool done = !flow->hooks.advance_validated_candidate;
            flow->resource_advancing = true;
            if (flow->hooks.advance_validated_candidate)
                ok = flow->hooks.advance_validated_candidate(flow->hooks.context, app, NULL, &done, error);
            flow->resource_advancing = false;
            flow->resource_waiting = ok && !done;
            if (!ok || !done) return ok;
            flow->root_settled = true;
        }
        qa_cvars_edit *values = NULL;
        if (!flow->hooks.candidate_values(flow->hooks.context, app, NULL, &values, error)) return false;
        if (!values || !qa_cvars_edit_returned_is(values, flow->cvars))
            return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE bootstrap lost its actual returned canonical edit");
        flow->resources_candidate = NULL;
        flow->preparing_resources = true;
        ok = flow->hooks.prepare_publication(flow->hooks.context, app, NULL, &flow->resources, error);
        if (ok && !flow->resources)
            ok = application_fail(error, QA_ERROR_ARGUMENT, "ENGINE resource preparation returned no actual owner");
        if (ok) ok = flow->hooks.ready_publication(flow->hooks.context, app, NULL, flow->resources, error);
        flow->preparing_resources = false;
        if (!ok) return false;
        flow->resources_ready = flow->hooks.owned_publication_ready(flow->hooks.context, app, NULL, flow->resources);
        if (!flow->resources_ready)
            return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE resources lack actual owned child proofs");
        flow->resources_consumed = true;
        flow->committed = true;
        flow->resources_consuming = true;
        flow->hooks.consume_publication(flow->hooks.context, app, NULL, flow->resources);
        flow->resources_consuming = false;
    }
    ok = finish_resources(app, flow, error);
    if (flow->resources_finished) {
        finish_candidate(app, flow, NULL, true);
        app->startup_flow = NULL;
        free(flow);
        *complete = true;
    }
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
    if (flow->advancing || flow->cancelling || flow->generation != app->command_generation ||
        !source_consoles_idle(flow))
        return application_fail(error, QA_ERROR_ARGUMENT, "Startup frame lost its retained candidate lifetime");
    if (!candidate_callbacks_idle(app, flow, error)) return false;
    flow->advancing = true;
    app->operation = APPLICATION_CONFIGURING;
    flow->resource_waiting = false;
    flow->images_waiting = false;
    bool ok = true;
    if (!flow->images_completed) {
        bool images_complete = false;
        ok = advance_images(app, flow, &images_complete, error);
        if (!ok || !images_complete) goto returned;
    }
    if (flow->engine_only) {
        ok = advance_root(app, flow, complete, error);
        goto returned;
    }
    while (ok && flow->index < flow->count) {
        startup_source *source = flow->sources + flow->index;
        if (!source->phase) { ++flow->index; continue; }
        bool done = false;
        ok = flow->hooks.advance_source(flow->hooks.context, source->phase, source->owner.console, &done, error);
        if (!ok || !done) break;
        if (!qa_console_idle(source->owner.console)) {
            ok = application_fail(error, QA_ERROR_ARGUMENT,
                "Startup phase completion requires its returned physical console");
            break;
        }
        ++flow->index;
    }
    bool candidate_complete = flow->validated || flow->hooks.advance_candidate == NULL;
    if (ok && !flow->validated && flow->index == flow->count && flow->hooks.advance_candidate) {
        flow->resource_advancing = true;
        ok = flow->hooks.advance_candidate(flow->hooks.context, app, flow->candidate,
            &candidate_complete, error);
        flow->resource_advancing = false;
        flow->resource_waiting = ok && !candidate_complete;
    }
    if (ok && flow->index == flow->count && candidate_complete) {
        if (!flow->validated) {
            ok = flow->hooks.prepare_candidate(flow->hooks.context, app, flow->candidate, error);
            if (ok) flow->configured = true;
            for (size_t i = 0; ok && i < flow->count; ++i)
                if (flow->sources[i].phase && flow->sources[i].provider->kind == APPLICATION_PROVIDER_Q2)
                    ok = application_native_q2_console_scripts(flow->sources[i].provider, NULL, error);
            if (ok) ok = release_phases(flow, error);
            for (size_t i=0; ok && i<flow->count; ++i) {
                startup_source *source=flow->sources+i;
                if (source->provider->kind==APPLICATION_PROVIDER_Q3 && source->provider->attached &&
                    source->owner.scope.kind==QA_APPLICATION_CONSOLE_Q3_CGAME &&
                    !application_native_q3_remote_role_selected(source->owner.descriptor,
                        qa_launch_snapshot_choices(flow->previous),source->owner.scope.seat))
                    ok=flow->hooks.preinit_source(flow->hooks.context,app,flow->candidate,
                        &source->owner,error) && qa_cvars_apply_latched(source->owner.cvars,NULL,error);
            }
            if (ok) ok = qa_configuration_validate(flow->transaction, error);
            if (ok) flow->validated = true;
        }
        if (ok) ok = application_publication_retire_sources(app, flow->validated_publication, error);
        for (size_t i = 0; ok && i < flow->count; ++i) {
            startup_source *source = flow->sources + i;
            qa_application_startup_source actual;
            bool found = false;
            for (size_t index = 0; ok; ++index) {
                ok = application_provider_startup_source_at(source->provider, index, &actual, &found, error);
                if (!ok || !found || (actual.console == source->owner.console &&
                    actual.cvars == source->owner.cvars && actual.scope.kind == source->owner.scope.kind &&
                    actual.scope.seat == source->owner.scope.seat)) break;
            }
            if (ok && (!found || !actual.descriptor ||
                actual.descriptor->storage != source->owner.descriptor->storage ||
                actual.cvars != source->owner.cvars || actual.scope.provider != source->owner.scope.provider ||
                actual.scope.kind != source->owner.scope.kind || actual.scope.seat != source->owner.scope.seat ||
                !actual.declaration_owner))
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Validated source changed its actual physical configuration authority");
            if (ok) {
                source->owner.declaration_owner = actual.declaration_owner;
                if (source->programmed) ok = application_startup_program_refresh(flow->program, &source->owner, error);
                if (ok && flow->hooks.refresh_source)
                    ok = flow->hooks.refresh_source(flow->hooks.context, app,
                        flow->candidate, &source->owner, error);
            }
        }
        bool publication_complete = flow->hooks.advance_validated_candidate == NULL;
        if (ok && flow->hooks.advance_validated_candidate) {
            flow->resource_advancing = true;
            ok = flow->hooks.advance_validated_candidate(flow->hooks.context, app,
                flow->candidate, &publication_complete, error);
            flow->resource_advancing = false;
            flow->resource_waiting = ok && !publication_complete;
        }
        if (ok && !publication_complete) goto returned;
        qa_cvars_edit *values = NULL;
        if (ok && flow->hooks.candidate_values)
            ok = flow->hooks.candidate_values(flow->hooks.context, app, flow->candidate, &values, error);
        if (ok && values && !flow->hooks.prepare_publication)
            ok = application_fail(error, QA_ERROR_ARGUMENT,
                "Canonical values require their actual publication resource owner");
        bool prepare_resources = flow->hooks.prepare_publication && (!flow->hooks.candidate_values || values);
        if (ok && prepare_resources)
            ok = flow->hooks.prepare_candidate(flow->hooks.context, app, flow->candidate, error);
        if (ok && prepare_resources) {
            flow->resources_candidate = flow->candidate;
            flow->preparing_resources = true;
            ok = flow->hooks.prepare_publication(flow->hooks.context, app, flow->candidate, &flow->resources, error);
            if (ok && !flow->resources)
                ok = application_fail(error, QA_ERROR_ARGUMENT, "Resource preparation returned no retained publication owner");
            if (ok) ok = flow->hooks.ready_publication(flow->hooks.context, app, flow->candidate, flow->resources, error);
            flow->preparing_resources = false;
            if (ok) {
                flow->resources_ready = flow->hooks.owned_publication_ready(flow->hooks.context, app,
                    flow->candidate, flow->resources);
                if (!flow->resources_ready)
                    ok = application_fail(error, QA_ERROR_ARGUMENT, "Publication resources lack actual owned child proofs");
            }
        }
        if (ok) ok = application_startup_program_preflight(flow->program, error);
        if (ok) ok = application_startup_program_seal(flow->program, error);
        if (ok && flow->validated_publication->owns_values)
            ok = application_publication_ready_values(app, flow->validated_publication, error);
        if (ok) ok = qa_configuration_commit(flow->transaction, error);
        if (ok) {
            flow->transaction = NULL;
            flow->candidate = NULL;
            flow->committed = true;
            bool published = app->state != QA_APPLICATION_FAULTED;
            finish_candidate(app, flow, qa_configuration_current(app->configuration), published);
            application_q3_product_finish(app, &flow->product, published);
            application_map_load_finish(app, published);
            if (published) ok = application_map_level_entry(app, true, error);
            if (ok) ok = published ? application_startup_program_adopt(&flow->program, error)
                : application_startup_program_abort(&flow->program, error);
            if (ok && (flow->resources || (flow->resources_consumed && !flow->resources_finished))) {
                if (app->state == QA_APPLICATION_FAULTED) {
                    if (error) *error = app->publication_error;
                    ok = false;
                } else ok = application_fail(error, QA_ERROR_ARGUMENT,
                    "Committed publication retains entered resource cleanup");
            }
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
returned:
    if (app->startup_flow) app->startup_flow->advancing = false;
    app->operation = APPLICATION_IDLE;
    if (ok && app->state == QA_APPLICATION_FAULTED) {
        if (error) *error = app->publication_error;
        ok = false;
    }
    if (!ok && (!app->startup_flow || !app->startup_flow->engine_only ||
        !app->startup_flow->resources_consumed)) abort_failed_startup(app, error);
    return ok;
}

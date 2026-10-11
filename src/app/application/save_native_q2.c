#include "internal.h"
#include "unified_events.h"
#include "save_native_q2.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_baseline.h"
#include "map_private.h"
#include "world_bounds.h"
#include "qa/map_sidecars.h"
#include "guest_q3_console.h"
#include "guest_q3_private.h"
#include "guest_q3_factory.h"

#include <stdlib.h>

struct application_native_q2_scratch {
    qa_application *candidate, *application;
    application_provider *target, *source;
    const qa_launch_snapshot *snapshot;
    qa_application_native_baseline_services services;
    qa_fs_root *write_root;
    struct application_native_q2_baseline *phase;
    struct application_native_q2_scratch *next;
};

static void close_world(void *world)
{
    (void)qa_world_destroy(world, NULL);
}

bool application_native_q2_baseline_services_prepare(qa_application *candidate,
    const qa_application_options *options, qa_application_native_baseline_services **out, qa_error *error)
{
    if (!candidate || candidate->operation != APPLICATION_PERSISTING || !options || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "native platform preparation requires its isolated candidate");
    struct application_native_q2_scratch *owner = calloc(1, sizeof(*owner));
    if (!owner) return application_fail(error, QA_ERROR_MEMORY, "retaining native platform preparation");
    owner->candidate = candidate;
    owner->services.options = *options;
    owner->next = candidate->native_baselines;
    candidate->native_baselines = owner;
    *out = &owner->services;
    return true;
}

bool application_source_baseline_prepare(application_provider *target,
    const qa_launch_snapshot *snapshot, qa_application_native_baseline_services *services,
    struct application_native_q2_scratch **out, qa_error *error)
{
    qa_application *candidate = target ? target->application : NULL;
    if (!candidate || !snapshot || !services || !out || *out ||
        candidate->operation != APPLICATION_PERSISTING ||
        !target->constructed || !target->attached ||
        !candidate->map_resource || !target->product_catalog ||
        (services->context != NULL) != (services->destroy != NULL) ||
        ((services->options.native_q2_services || services->options.q3_services ||
          services->options.q3_client_prepare || services->options.q3_client_registry_reference ||
          services->options.q3_client_effect || services->options.q3_campaign_command || services->options.startup_hooks ||
          services->options.model_admission || services->options.console_print ||
          services->options.world_change_ready || services->options.before_world_change ||
          services->options.world_retired) &&
         (!services->context || services->options.guest_context == candidate->guest_context)))
        return application_fail(error, QA_ERROR_ARGUMENT, "native baseline requires separately owned source platform services");
    struct application_native_q2_scratch *scratch = calloc(1, sizeof(*scratch));
    if (!scratch) return application_fail(error, QA_ERROR_MEMORY, "retaining native baseline application graph");
    scratch->candidate = candidate;
    scratch->target = target;
    scratch->snapshot = snapshot;
    qa_launch_snapshot_retain(snapshot);
    scratch->services = *services;
    *services = (qa_application_native_baseline_services){0};
    scratch->next = candidate->native_baselines;
    candidate->native_baselines = scratch;
    *out = scratch;

    qa_application_options options = scratch->services.options;
    options.q3_product_policy = &candidate->q3_product;
    options.actor_capacity = qa_actors_capacity(qa_session_actors(candidate->session));
    bool ok = application_create_native_baseline(&options,
        qa_session_strings(candidate->session), qa_launch_snapshot_catalog(snapshot),
        &scratch->application, error);
    if (!ok) return false;
    qa_application *app = scratch->application;
    struct application_q3_guest *target_q3 = q3g_engine(target);
    if (target_q3 && target_q3->game) {
        if (!qa_fs_root_temporary_create(&scratch->write_root, error)) return false;
        app->baseline_write_root = scratch->write_root;
    }
    qa_resource_retain(candidate->map_resource);
    app->map_resource = candidate->map_resource;
    app->map_sidecars = candidate->map_sidecars;
    qa_map_sidecars_retain(app->map_sidecars);
    qa_bsp_view map;
    qa_world *world = NULL;
    qa_world_hooks hooks = application_world_hooks(app);
    ok = qa_bsp_open(qa_resource_bytes(app->map_resource), &map, error) &&
        qa_map_sidecars_apply_entities(app->map_sidecars, &map, error) &&
        qa_collision_create(&map, &app->geometry, error) &&
        qa_collision_bind_resource(app->geometry, app->map_resource, error) &&
        qa_map_sidecars_apply_materials(app->map_sidecars, app->geometry, qa_session_strings(app->session), error) &&
        qa_world_create(qa_session_actor_registry(app->session), app->geometry, &hooks,
            QA_WORLD_SNAPSHOT_DEFAULT_FRAMES, &world, error);
    if (!ok) return false;
    qa_physics_services physics = application_physics_services(app);
    if (!qa_physics_init(app->physics, world, (qa_actor_id){0}, &physics, error) ||
        !qa_session_adopt_restored_world(app->session, world, close_world, error)) {
        (void)qa_world_destroy(world, NULL);
        return false;
    }
    app->world = world;
    app->physics_ready = true;
    app->routing_snapshot = snapshot;
    app->current_map = candidate->current_map;
    if (!application_map_restore_identity(app, snapshot, error) ||
        !application_provider_prepare(app, target->launch, &scratch->source, error)) return false;
    app->providers = calloc(1, sizeof(*app->providers));
    if (!app->providers) return application_fail(error, QA_ERROR_MEMORY, "retaining native baseline provider roster");
    app->providers[0] = scratch->source;
    app->provider_count = 1;
    app->routing_providers = app->providers;
    app->routing_provider_count = 1;
    if (target_q3 && target_q3->game) {
        qa_console *console = NULL;
        qa_cvars *cvars = NULL;
        qa_command_context command = {0};
        if (!target_q3->game->service_sequence ||
            !application_guest_q3_console_prepare(app, scratch->source, world,
                target->product, qa_launch_snapshot_choices(snapshot), &console, &cvars,
                &command, error)) return false;
        q3g_engine(scratch->source)->role_sequence = target_q3->game->service_sequence - 1;
    }
    if (!application_unified_event_owner_bind(app, scratch->source, false, error) ||
        !application_provider_construct(app, scratch->source, app->world,
        target->product_catalog, target->product, qa_launch_snapshot_choices(snapshot), error)) return false;
    if (!qa_session_add(app->session, &scratch->source->component, error)) return false;
    scratch->source->component_attached = true;
    scratch->source->attached = true;
    return true;
}

application_provider *application_source_baseline_provider(struct application_native_q2_scratch *scratch)
{ return scratch ? scratch->source : NULL; }

bool application_source_baseline_ready(struct application_native_q2_scratch *scratch, qa_error *error)
{
    if (!scratch || !scratch->application || !scratch->source)
        return application_fail(error, QA_ERROR_ARGUMENT, "Source baseline readiness requires its retained graph");
    return !scratch->services.ready || scratch->services.ready(scratch->services.context, error);
}

bool application_native_q2_scratch_prepare(application_provider *target,
    const qa_launch_snapshot *snapshot, qa_application_native_baseline_services *services,
    struct application_native_q2_scratch **out, qa_error *error)
{
    if (!target || target->kind != APPLICATION_PROVIDER_NATIVE || !target->state.native.q2_engine)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 baseline requires its actual GAME owner");
    if (!application_source_baseline_prepare(target, snapshot, services, out, error)) return false;
    struct application_native_q2_scratch *scratch = *out;
    qa_application *app = scratch->application;
    struct application_native_q2 *engine = scratch->source->state.native.q2_engine;
    if (!qa_session_allocate(app->session, scratch->source->owner, engine->definition, true, 0,
        &engine->world_actor, error)) return false;
    app->physics->world_actor = engine->world_actor;
    if (!application_native_q2_activate(engine, error) ||
        !application_native_q2_prepare_restore(scratch->source, error)) return false;
    return true;
}

bool application_native_q2_scratch_begin(struct application_native_q2_scratch *scratch, qa_error *error)
{
    if (!scratch || scratch->phase || !scratch->application || !scratch->source)
        return application_fail(error, QA_ERROR_ARGUMENT, "native baseline graph is not freshly prepared");
    return application_native_q2_baseline_begin(scratch->target, scratch->source, &scratch->phase, error);
}

bool application_native_q2_scratch_spawn(struct application_native_q2_scratch *scratch,
    const char *map, const char *entities, const char *spawn, qa_error *error)
{
    return scratch && scratch->phase
        ? application_native_q2_baseline_spawn(scratch->phase, map, entities, spawn, error)
        : application_fail(error, QA_ERROR_ARGUMENT, "native baseline has no active source phase");
}

bool application_native_q2_scratch_end(struct application_native_q2_scratch *scratch, qa_error *error)
{
    if (!scratch || !scratch->phase)
        return application_fail(error, QA_ERROR_ARGUMENT, "native baseline has no active source phase");
    if (!application_source_baseline_ready(scratch, error) ||
        !application_native_q2_baseline_end(scratch->phase, error)) return false;
    scratch->phase = NULL;
    return true;
}

bool application_native_q2_baselines_destroy(qa_application *candidate, qa_error *error)
{
    while (candidate && candidate->native_baselines) {
        struct application_native_q2_scratch *scratch = candidate->native_baselines;
        if (scratch->phase) {
            bool ended = application_native_q2_baseline_abort(scratch->phase, error);
            if (!scratch->target->state.native.q2_engine->baseline) scratch->phase = NULL;
            if (!ended) return false;
        }
        if (scratch->application) {
            if (!application_composition_destroy(scratch->application, error)) return false;
            if (scratch->source) {
                if (!application_provider_close(scratch->application, scratch->source, error)) return false;
                scratch->source = NULL;
            }
            if (!qa_application_destroy(scratch->application, error)) return false;
            scratch->application = NULL;
        }
        if (scratch->services.destroy &&
            !scratch->services.destroy(scratch->services.context, error)) return false;
        scratch->services.destroy = NULL;
        scratch->services.context = NULL;
        if (!qa_fs_root_temporary_dispose(&scratch->write_root, error)) return false;
        qa_launch_snapshot_release(scratch->snapshot);
        candidate->native_baselines = scratch->next;
        free(scratch);
    }
    return true;
}

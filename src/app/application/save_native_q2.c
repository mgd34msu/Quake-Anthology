#include "internal.h"
#include "save_native_q2.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_baseline.h"
#include "map_private.h"
#include "world_bounds.h"

#include <stdlib.h>

struct application_native_q2_scratch {
    qa_application *candidate, *application;
    application_provider *target, *source;
    const qa_launch_snapshot *snapshot;
    qa_application_native_baseline_services services;
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

bool application_native_q2_scratch_prepare(application_provider *target,
    const qa_launch_snapshot *snapshot, qa_application_native_baseline_services *services,
    struct application_native_q2_scratch **out, qa_error *error)
{
    qa_application *candidate = target ? target->application : NULL;
    if (!candidate || !snapshot || !services || !out || *out ||
        candidate->operation != APPLICATION_PERSISTING ||
        target->kind != APPLICATION_PROVIDER_NATIVE ||
        !target->constructed || !target->attached || !target->state.native.q2_engine ||
        !candidate->map_resource || !target->product_catalog ||
        (services->context != NULL) != (services->destroy != NULL) ||
        ((services->options.native_q2_services || services->options.console_print ||
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
    qa_resource_retain(candidate->map_resource);
    app->map_resource = candidate->map_resource;
    qa_bsp_view map;
    qa_world *world = NULL;
    qa_world_hooks hooks = application_world_hooks(app);
    ok = qa_bsp_open(qa_resource_bytes(app->map_resource), &map, error) &&
        qa_collision_create(&map, &app->geometry, error) &&
        qa_world_create(qa_session_actor_registry(app->session), app->geometry, &hooks, &world, error);
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
    if (!application_provider_construct(app, scratch->source, app->world,
        target->product_catalog, target->product, qa_launch_snapshot_choices(snapshot), error)) return false;
    if (!qa_session_add(app->session, &scratch->source->component, error)) return false;
    scratch->source->component_attached = true;
    scratch->source->attached = true;
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
    if ((scratch->services.ready &&
         !scratch->services.ready(scratch->services.context, error)) ||
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
        qa_launch_snapshot_release(scratch->snapshot);
        candidate->native_baselines = scratch->next;
        free(scratch);
    }
    return true;
}

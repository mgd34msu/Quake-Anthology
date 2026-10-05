#include "internal.h"
#include "save_private.h"
#include "save_native_q2.h"
#include "native_q2_callbacks.h"
#include "guest_native_q2_private.h"
#include "save_content.h"
#include "control_frame.h"
#include "bots_round.h"
#include "rankings.h"
#include "native_q3_clients.h"
#include "native_q3_wire.h"
#include "native_q1_wire.h"
#include "native_client_roles.h"
#include "q3_product.h"
#include "startup_flow.h"
#include "map_travel_private.h"
#include "guest_q3_mod_operations.h"
#include "guest_q3_components.h"
#include "qa/rankings_save.h"
#include "qa/player_progress_save.h"
#include "qa/catalog_save.h"
#include "qa/console_cvar_observer.h"

#include <stdlib.h>
#include <string.h>
#include <math.h>

bool application_fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

void application_fault(qa_application *application, const qa_error *error)
{
    if (application == NULL || application->state == QA_APPLICATION_FAULTED)
        return;
    application->state = QA_APPLICATION_FAULTED;
    if (error != NULL && error->code != QA_OK)
        application->publication_error = *error;
    else
        qa_error_set(&application->publication_error, QA_ERROR_ARGUMENT, 0,
                     "application publication failed");
}

typedef struct application_think_call {
    qa_think_fn callback;
    void *context;
    qa_think_scope scope;
} application_think_call;

static bool think_body(void *opaque, const application_q3_mod_actor_request *request,
                       bool *result, qa_error *error)
{
    application_think_call *call = opaque;
    if (!call->callback(call->context, request->self, &call->scope, error)) return false;
    *result = true;
    return true;
}

static bool dispatch_think(void *opaque, qa_think_fn callback, void *callback_context,
                           qa_actor_id actor, const qa_think_scope *scope, qa_error *error)
{
    qa_application *application = opaque;
    application_think_call call = {callback, callback_context, *scope};
    application_q3_mod_actor_request request = {.self = actor,
        .source.think = {.time_ns = scope->time_ns, .elapsed_ns = scope->interval_elapsed_ns}};
    bool result;
    return application_q3_mod_actor_dispatch(application->mod_operations, Q3_MOD_THINK,
        &request, think_body, &call, &result, error);
}

static char *copy_text(const char *text, qa_error *error)
{
    if (text == NULL)
        return NULL;
    size_t length = strlen(text);
    if (length == SIZE_MAX) {
        application_fail(error, QA_ERROR_MEMORY,
                         "application path is too long");
        return NULL;
    }
    char *copy = malloc(length + 1);
    if (copy == NULL) {
        application_fail(error, QA_ERROR_MEMORY,
                         "cannot retain application path");
        return NULL;
    }
    memcpy(copy, text, length + 1);
    return copy;
}

void qa_application_options_default(qa_application_options *options)
{
    if (options == NULL)
        return;
    *options = (qa_application_options){
        .catalog_generation = 1,
        .actor_capacity = 16384,
        .component_capacity = 256,
        .discover_mods = true,
        .mixed_source_order = true,
        .native_process_policy = {
#if defined(__linux__) && defined(__x86_64__)
            .backend = QA_NATIVE_GUEST_HOST_X86_64,
            .instruction_budget = 0,
#else
            .backend = QA_NATIVE_GUEST_EMULATED,
            .instruction_budget = 50000000u,
#endif
            .maximum_image_bytes = 256u * 1024u * 1024u,
            .maximum_backing_bytes = 1024u * 1024u * 1024u,
            .stack_bytes = 8u * 1024u * 1024u,
            .runtime_trap_bytes = 1024u * 1024u,
        },
    };
}

static bool discover(qa_application *application, bool discover_mods,
                     uint64_t generation, qa_catalog **out, qa_error *error)
{
    qa_catalog_options options = {
        .resources = application->resources,
        .content_root = application->content_root,
        .install_roots = (const char *const *)application->install_roots,
        .install_root_count = application->install_root_count,
        .user_root = application->user_root,
        .generation = generation,
        .discover_mods = discover_mods,
    };
    return qa_catalog_discover(&options, out, error);
}

static bool create_application(const qa_application_options *options,
                                const qa_save_image *restore,
                                const qa_strings *baseline_strings,
                                qa_application_content_graph **saved_content,
                                qa_catalog *baseline_catalog,
                                qa_application **out, qa_error *error)
{
    if (options == NULL || out == NULL || options->content_root == NULL ||
        options->content_root[0] == '\0' || options->actor_capacity == 0 ||
        options->component_capacity == 0 ||
        (options->install_root_count && !options->install_roots) ||
        options->install_root_count > SIZE_MAX / sizeof(char *))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "application needs content and session capacities");
    *out = NULL;

    qa_application *application = calloc(1, sizeof(*application));
    if (application == NULL)
        return application_fail(error, QA_ERROR_MEMORY,
                                "cannot allocate application owner");
    if (saved_content) {
        application->content_graph = *saved_content;
        *saved_content = NULL;
    }
    application->state = QA_APPLICATION_READY;
    application->command_generation = 1;
    application->native_runner = options->native_runner;
    application->native_runtime = options->native_runtime;
    qa_native_runtime_retain(application->native_runtime);
    application->native_process_policy = options->native_process_policy;
    application->native_bootstrap = copy_text(options->native_bootstrap, error);
    if (options->native_bootstrap && !application->native_bootstrap) goto fail;
    application->guest_context = options->guest_context;
    application->prompt_context = options->prompt_context;
    application->prompt_supported = options->prompt_supported;
    application->startup_hooks = options->startup_hooks;
    application->dedicated = options->dedicated;
    application->q3_services = options->q3_services;
    application->q3_client_prepare = options->q3_client_prepare;
    application->q3_component_scene_prepare = options->q3_component_scene_prepare;
    application->q3_component_client_drop = options->q3_component_client_drop;
    application->q3_client_registry_reference = options->q3_client_registry_reference;
    application->q3_client_effect = options->q3_client_effect;
    application->q3_campaign_command = options->q3_campaign_command;
    application->q3_round_services = options->q3_round_services;
    application->ranking_effect = options->ranking_effect;
    application->native_q2_services = options->native_q2_services;
    application->model_admission = options->model_admission;
    application->world_change_ready = options->world_change_ready;
    application->before_world_change = options->before_world_change;
    application->world_retired = options->world_retired;
    application->console_print = options->console_print;
    if (!application_console_create(application, error)) goto fail;
    if (!restore && !baseline_strings && !application_startup_create(application,
        options->startup_commands, options->startup_command_count, error)) goto fail;
    if (restore) {
        qa_q3_product_policy saved = {0};
        if (!application_save_q3_product_decode(restore, &saved, error) ||
            !application_q3_product_import(application->cvars, &saved, &application->q3_product, error)) goto fail;
        if (!application_save_startup_decode(restore, application, error)) goto fail;
    } else if (baseline_strings) {
        if (!options->q3_product_policy || !application_q3_product_import(application->cvars,
            options->q3_product_policy, &application->q3_product, error)) goto fail;
    } else if (!application_q3_product_initial(application->cvars, options->startup_commands,
        options->startup_command_count, &application->q3_product, error)) goto fail;
    qa_builtin_random_seed(&application->random,
                           (uint32_t)options->catalog_generation ^ UINT32_C(0x71616e74));
    application->content_root = copy_text(options->content_root, error);
    application->user_root = copy_text(options->user_root, error);
    application->ranking_game_key = copy_text(options->ranking_game_key, error);
    if (application->content_root == NULL ||
        (options->user_root != NULL && application->user_root == NULL) ||
        (options->ranking_game_key != NULL && application->ranking_game_key == NULL))
        goto fail;
    if (options->install_root_count) {
        application->install_roots = calloc(options->install_root_count,
                                            sizeof(*application->install_roots));
        if (!application->install_roots) {
            application_fail(error, QA_ERROR_MEMORY, "cannot retain game installation locations");
            goto fail;
        }
        for (size_t i = 0; i < options->install_root_count; ++i) {
            const char *root = options->install_roots[i];
            if (!root || !*root) {
                application_fail(error, QA_ERROR_ARGUMENT, "game installation location is empty");
                goto fail;
            }
            application->install_roots[i] = copy_text(root, error);
            if (!application->install_roots[i]) goto fail;
            ++application->install_root_count;
        }
    }

    if (restore) {
        if (!application->content_graph ||
            !qa_application_content_claim_pool(application->content_graph,
                application_save_content_application_pool(application->content_graph),
                &application->resources, error) ||
            !qa_application_content_retain_catalog(application->content_graph,
                application_save_content_application_catalog(application->content_graph),
                &application->catalog, error)) goto fail;
    } else if (baseline_catalog) {
        application->catalog = baseline_catalog;
        qa_catalog_retain(baseline_catalog);
        application->resources = qa_catalog_resources(baseline_catalog);
        qa_resource_pool_retain(application->resources);
    } else {
        application->resources = qa_resource_pool_create(error);
        if (application->resources == NULL) goto fail;
    }
    bool restored_owners = restore != NULL || baseline_strings != NULL;
    const qa_ranking_provider *ranking_provider = baseline_strings ? NULL : options->ranking_provider;
    bool rankings_created = restored_owners
        ? qa_rankings_create_restored(ranking_provider, NULL, &application->rankings, error)
        : qa_rankings_create(ranking_provider, NULL, &application->rankings, error);
    if (!rankings_created)
        goto fail;
    if (options->player_profile_root != NULL) {
        application->user_files = options->player_profile_root;
        qa_fs_root_retain(application->user_files);
        if (!(restored_owners
            ? qa_player_progress_create_restored(application->user_files,
                "player-progress.json", &application->progress, error)
            : qa_player_progress_open(application->user_files,
                "player-progress.json", &application->progress, error)))
            goto fail;
    }
    application->catalog_generation = (restore || baseline_catalog)
        ? qa_catalog_generation(application->catalog) : options->catalog_generation;
    application->discover_mods = options->discover_mods;
    if (!restore && !baseline_catalog && !discover(application, options->discover_mods,
                  application->catalog_generation, &application->catalog, error))
        goto fail;
    if (restore || baseline_catalog) {
        if (qa_catalog_q3_restricted(application->catalog) &&
            (!application->q3_product.restriction_resolved || !application->q3_product.filesystem_restricted)) {
            application_fail(error, QA_ERROR_FORMAT, "Saved application catalog differs from its retained Q3 media policy");
            goto fail;
        }
    } else if (options->initial_product_key) {
        const qa_product *selected = qa_catalog_find(application->catalog, options->initial_product_key);
        if (!selected && !strcmp(options->initial_product_key, "q3"))
            for (size_t i = 0; i < qa_catalog_count(application->catalog); ++i) {
                const qa_product *product = qa_catalog_at(application->catalog, i);
                if (product->family == QA_GAME_Q3 && product->edition == QA_EDITION_CLASSIC && product->builtin) {
                    selected = product;
                    break;
                }
            }
        if (selected && (!application_q3_product_prepare(application->catalog, selected->id,
            &application->q3_product, error) || !application_startup_seed_engine(application, selected->id, error))) goto fail;
    }

    qa_session_options session = {
        .actor_capacity = options->actor_capacity,
        .component_capacity = options->component_capacity,
        .mixed_order = options->mixed_source_order,
        .actor_released = application_actor_released,
        .observer_actor = application_native_client_observer_actor,
        .think_dispatch = dispatch_think,
        .source_actor = application_arsenal_source_actor,
        .prepare_commands = application_control_frames_prepare,
        .run_commands = application_control_frames_commands,
        .end_commands = application_control_frames_end,
        .frame_exit = application_native_q2_frames_exit,
        .controlled_actor = application_control_frames_actor,
        .release_context = application,
    };
    bool session_created;
    if (baseline_strings != NULL) {
        qa_buffer encoded = {0};
        qa_strings *strings = NULL;
        qa_actor_checkpoint actors = {.capacity = session.actor_capacity};
        session_created = qa_save_strings_encode(baseline_strings, &encoded, error) &&
            qa_save_strings_decode((qa_bytes){encoded.data, encoded.size}, &strings, error) &&
            qa_session_create_restored(&session, &actors, strings, &application->session, error);
        if (session_created) strings = NULL;
        qa_strings_destroy(strings);
        qa_buffer_free(&encoded);
    } else {
        session_created = restore == NULL
            ? qa_session_create(&session, &application->session, error)
            : application_save_session_create(&session, restore, &application->session, error);
    }
    if (!session_created)
        goto fail;
    application->campaign_unit=qa_campaign_unit_create(qa_session_strings(application->session),error);
    if (!application->campaign_unit) goto fail;
    if (!qa_inventory_create(qa_session_actor_registry(application->session),
                             &application->inventory, error))
        goto fail;
    qa_combat_hooks combat_hooks = application_combat_hooks(application);
    if (!qa_combat_create(qa_session_actor_registry(application->session), &combat_hooks,
                          &application->combat, error))
        goto fail;
    if (!application_q3_mod_operations_create(application->session, application->combat,
                                               application->inventory,
                                               &application->mod_operations, error))
        goto fail;
    if (!qa_pickups_create(qa_session_actor_registry(application->session),
                           application->combat, application->inventory,
                           &application->pickups, error))
        goto fail;
    if (!application_composition_create(application, error))
        goto fail;
    if (!application_control_frames_create(application, error))
        goto fail;

    application->source_shutdown_admitted = !restored_owners;
    *out = application;
    return true;

fail:
    application_fault(application, error);
    qa_error cleanup = {0};
    if (!qa_application_destroy(application, &cleanup)) {
        /* Its actual services and source descendants remain the caller's
         * retryable owner; a failed constructor cannot free their context. */
        *out = application;
        if (error && cleanup.code != QA_OK) *error = cleanup;
    }
    return false;
}

bool qa_application_create(const qa_application_options *options,
                             qa_application **out, qa_error *error)
{
    return create_application(options, NULL, NULL, NULL, NULL, out, error);
}

bool application_create_restored(const qa_application_options *options,
                                   const qa_save_image *image,
                                   qa_application_content_graph **content,
                                   qa_application **out, qa_error *error)
{
    if (image == NULL || !content || !*content)
        return application_fail(error, QA_ERROR_ARGUMENT, "restored application requires a save image");
    return create_application(options, image, NULL, content, NULL, out, error);
}

bool application_create_native_baseline(const qa_application_options *options,
                                         const qa_strings *strings,
                                         qa_catalog *catalog,
                                         qa_application **out, qa_error *error)
{
    if (strings == NULL || catalog == NULL || qa_catalog_resources(catalog) == NULL)
        return application_fail(error, QA_ERROR_ARGUMENT, "native baseline requires the exact source string namespace");
    return create_application(options, NULL, strings, NULL, catalog, out, error);
}

qa_application_content_graph *qa_application_content_graph_read(const qa_application *application)
{
    return application ? (application->capture_content_graph ? application->capture_content_graph
                                                            : application->content_graph) : NULL;
}

qa_application_state qa_application_get_state(const qa_application *application)
{
    return application == NULL ? QA_APPLICATION_FAULTED : application->state;
}

const qa_error *qa_application_error(const qa_application *application)
{
    return application == NULL || application->state != QA_APPLICATION_FAULTED
               ? NULL
               : &application->publication_error;
}

void qa_application_request_stop(qa_application *application)
{
    if (application != NULL && application->state != QA_APPLICATION_FAULTED)
        application->state = QA_APPLICATION_STOPPING;
}

bool qa_application_should_stop(const qa_application *application)
{
    return application == NULL || application->state == QA_APPLICATION_STOPPING ||
           application->state == QA_APPLICATION_FAULTED;
}

qa_resource_pool *qa_application_resources(qa_application *application)
{
    return application == NULL ? NULL : application->resources;
}

qa_catalog *qa_application_catalog(qa_application *application)
{
    return application == NULL ? NULL : application->catalog;
}

qa_cvars *qa_application_cvars(qa_application *application)
{
    return application == NULL ? NULL : application->cvars;
}

qa_console *qa_application_console(qa_application *application)
{
    return application == NULL ? NULL : application->console;
}

const char *qa_application_provider_instance(const qa_application *application,
                                              qa_actor_owner owner)
{
    if (application == NULL || owner == 0 || application->destroy_requested)
        return NULL;
    for (const application_provider *provider = application->live_providers;
         provider; provider = provider->next_live) {
        if (provider->attached && provider->constructed && provider->owner == owner)
            return provider->launch->selection.instance;
    }
    return NULL;
}

bool qa_application_provider_owner(const qa_application *application,
                                     const char *instance, qa_actor_owner *out)
{
    if (application == NULL || instance == NULL || out == NULL ||
        application->destroy_requested)
        return false;
    for (size_t i = 0; i < application->provider_count; ++i) {
        const application_provider *provider = application->providers[i];
        if (provider->attached && provider->constructed &&
            strcmp(provider->launch->selection.instance, instance) == 0) {
            *out = provider->owner;
            return true;
        }
    }
    return false;
}

bool qa_application_provider_gravity(const qa_application *application,
                                       qa_actor_owner owner, float *out)
{
    if (application == NULL || owner == 0 || out == NULL ||
        application->destroy_requested)
        return false;
    for (size_t i = 0; i < application->provider_count; ++i) {
        application_provider *provider = application->providers[i];
        if (!provider->attached || !provider->constructed || provider->owner != owner)
            continue;
        if (provider->kind == APPLICATION_PROVIDER_Q1)
            return qa_q1_game_gravity(provider->state.q1, out);
        if (provider->kind == APPLICATION_PROVIDER_QC) {
            qa_console *console;
            qa_cvars *cvars;
            if (!application_guest_console_at(provider, 0, &console, &cvars, NULL))
                return false;
            const qa_cvar_view *gravity = qa_cvars_find(cvars, "sv_gravity");
            if (gravity != NULL && isfinite(gravity->number)) {
                *out = gravity->number;
                return true;
            }
        }
        return false;
    }
    return false;
}

bool qa_application_q1_fog_owner(qa_application *application, qa_actor_owner *out)
{
    if (!application || !out || application->destroy_requested) return false;
    application_provider *provider = application_world_provider(application, QA_ROLE_ENTITIES, "");
    if (!provider || !provider->constructed || !provider->attached || provider->close_pending ||
        provider->kind != APPLICATION_PROVIDER_Q1) return false;
    *out = provider->owner;
    return true;
}

bool qa_application_q1_fog_read(qa_application *application, qa_actor_id actor,
                                 qa_q1_fog_state *out)
{
    if (application == NULL || out == NULL || application->destroy_requested)
        return false;
    application_provider *provider = application_world_provider(application, QA_ROLE_ENTITIES, "");
    return provider != NULL && provider->constructed && provider->attached && !provider->close_pending &&
           provider->kind == APPLICATION_PROVIDER_Q1 &&
           qa_q1_game_map_fog_read(provider->state.q1, actor, out);
}

bool qa_application_q1_monster_counts(const qa_application *application,
                                       uint32_t seat, uint32_t *total,
                                       uint32_t *killed)
{
    if (application == NULL || total == NULL || killed == NULL)
        return false;
    qa_actor_id actor;
    if (!qa_application_player_actor(application, seat, &actor))
        return false;
    application_provider *provider = application_world_provider((qa_application *)application,
                                                               QA_ROLE_ENTITIES, "");
    return provider != NULL && provider->constructed && provider->kind == APPLICATION_PROVIDER_Q1 &&
           qa_q1_game_monster_counts(provider->state.q1, total, killed);
}

uint64_t qa_application_configuration_generation(const qa_application *application)
{
    return application == NULL ? 0 : qa_configuration_generation(application->configuration);
}

qa_session *qa_application_session(qa_application *application)
{
    return application == NULL ? NULL : application->session;
}

qa_world *qa_application_world(qa_application *application)
{
    return application == NULL ? NULL : application->world;
}

qa_combat *qa_application_combat(qa_application *application)
{
    return application == NULL ? NULL : application->combat;
}

qa_inventory *qa_application_inventory(qa_application *application)
{
    return application == NULL ? NULL : application->inventory;
}

qa_pickups *qa_application_pickups(qa_application *application)
{
    return application == NULL ? NULL : application->pickups;
}

qa_targets *qa_application_targets(qa_application *application)
{
    return application == NULL ? NULL : application->targets;
}

qa_player_progress *qa_application_player_progress(qa_application *application)
{
    return application == NULL ? NULL : application->progress;
}

qa_rankings *qa_application_rankings(qa_application *application)
{
    return application == NULL ? NULL : application->rankings;
}

const qa_launch_snapshot *qa_application_launch(const qa_application *application)
{
    return application == NULL || application->configuration == NULL
               ? NULL
               : qa_configuration_current(application->configuration);
}

bool qa_application_map_read(const qa_application *application,
                             qa_application_map_view *out)
{
    if (application == NULL || out == NULL || !application->map_view_ready ||
        application->world == NULL || application->map_resource == NULL)
        return false;
    const char *name = qa_strings_cstr(qa_session_strings(application->session),
                                      application->current_map);
    if (name == NULL)
        return false;
    *out = (qa_application_map_view){
        .geometry = application->map_geometry,
        .presentation = application->map_presentation,
        .name = name,
        .resource = application->map_resource,
        .revision = application->map_revision
    };
    return true;
}

bool qa_application_map_origin_read(const qa_application *app, qa_launch_resource_origin *out)
{
    if (!app || !out || !app->map_resource) return false;
    const qa_launch_snapshot *snapshots[] = {app->routing_snapshot, qa_application_launch(app)};
    for (size_t j = 0; j < 2; ++j) {
        const qa_launch_snapshot *snapshot = snapshots[j];
        const qa_launch_choices *choices = qa_launch_snapshot_choices(snapshot);
        if (!choices) continue;
        for (size_t i = 0; i < qa_launch_snapshot_resource_count(snapshot); ++i) {
            const qa_launch_resource *resource = qa_launch_snapshot_resource(snapshot, i);
            if (resource->resource != app->map_resource || resource->product != choices->world.geometry ||
                strcmp(resource->path, choices->world.map)) continue;
            qa_launch_resource_origin origin;
            if (!qa_launch_snapshot_resource_origin(snapshot, i, &origin) || !origin.acquisition ||
                origin.acquisition->resource_id != qa_resource_id(app->map_resource) ||
                !qa_vfs_acquisition_retained(origin.content, origin.acquisition, NULL)) return false;
            if (origin.kind==QA_LAUNCH_ORIGIN_SOURCE_QW &&
                (origin.catalog_mount || origin.product!=resource->product ||
                 origin.source.catalog!=origin.catalog || origin.source.content!=origin.content ||
                 origin.source.product!=resource->product || !qa_launch_source_files_current(&origin.source,NULL))) return false;
            if (origin.kind!=QA_LAUNCH_ORIGIN_CATALOG && origin.kind!=QA_LAUNCH_ORIGIN_SOURCE_QW) return false;
            *out = origin; return true;
        }
    }
    return false;
}

bool qa_application_motion_read(const qa_application *application,
                                qa_actor_id actor,
                                qa_application_motion_view *out)
{
    if (application == NULL || out == NULL ||
        actor.slot >= application->motion_capacity)
        return false;
    const application_motion_record *record =
        &application->motion[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor))
        return false;
    *out = (qa_application_motion_view){
        .actor = record->actor,
        .body = record->body,
        .view_angles = record->view_angles,
        .angular_kick = record->angular_kick,
        .hold_until_ns = record->hold_until_ns,
        .revision = record->revision,
        .reason = record->reason,
        .force_view_angles = record->force_view_angles,
        .apply_angular_kick = record->apply_angular_kick,
    };
    return true;
}

size_t qa_application_shader_remap_count(const qa_application *application)
{
    return application == NULL ? 0 : application->shader_remap_count;
}

bool qa_application_shader_remap_at(const qa_application *application,
                                    size_t index,
                                    qa_application_shader_remap_view *out)
{
    if (application == NULL || out == NULL ||
        index >= application->shader_remap_count)
        return false;
    const application_shader_remap *remap =
        &application->shader_remaps[index];
    *out = (qa_application_shader_remap_view){
        .original = remap->original,
        .replacement = remap->replacement,
        .time_ns = remap->time_ns,
    };
    return true;
}

bool qa_application_q2_visual_read(const qa_application *application,
                                   qa_actor_id actor,
                                   qa_application_q2_visual_view *out)
{
    if (application == NULL || out == NULL ||
        actor.slot >= application->q2_visual_capacity)
        return false;
    const application_q2_visual_record *record =
        &application->q2_visuals[actor.slot];
    if (!record->active || !qa_actor_id_equal(record->actor, actor))
        return false;
    *out = (qa_application_q2_visual_view){.actor = actor,
                                           .visual = record->visual,
                                           .revision = record->revision};
    return true;
}

bool qa_application_rediscover(qa_application *application, bool discover_mods,
                               qa_error *error)
{
    if (application == NULL || application->operation != APPLICATION_IDLE || application->q3_round_active || application->frame_preparing ||
        application->client_preparation ||
        !application_rankings_idle(application) ||
        application->state == QA_APPLICATION_FAULTED ||
        application->state == QA_APPLICATION_STOPPING)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "catalog discovery requires an idle application");
    if (application->catalog_generation == UINT64_MAX)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "catalog generation is exhausted");
    application->operation = APPLICATION_DISCOVERING;
    qa_catalog *candidate = NULL;
    uint64_t generation = application->catalog_generation + 1;
    bool ok = discover(application, discover_mods, generation, &candidate, error);
    if (ok && application->q3_product.restriction_resolved && application->q3_product.filesystem_restricted)
        ok = qa_catalog_q3_restrict(candidate, error);
    if (ok) {
        qa_catalog *previous = application->catalog;
        application->catalog = candidate;
        application->catalog_generation = generation;
        application->discover_mods = discover_mods;
        qa_catalog_release(previous);
    }
    if (!ok) qa_catalog_release(candidate);
    application->operation = APPLICATION_IDLE;
    return ok;
}

bool qa_application_apply(qa_application *application,
                          const qa_launch_draft *draft, qa_error *error)
{
    if (application == NULL || draft == NULL ||
        application->client_preparation ||
        application->operation != APPLICATION_IDLE || application->q3_round_active || application->frame_preparing ||
        !application_guests_idle(application) || !application_rankings_idle(application) ||
        !application_bots_can_destroy(application) ||
        application->state == QA_APPLICATION_FAULTED ||
        application->state == QA_APPLICATION_STOPPING)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "configuration requires an idle healthy application");
    application->operation = APPLICATION_CONFIGURING;
    bool ok = application_apply(application, draft, error);
    application->operation = APPLICATION_IDLE;
    if (ok && application->state == QA_APPLICATION_READY &&
        !qa_application_startup_pending(application))
        application->state = QA_APPLICATION_RUNNING;
    return ok;
}

bool qa_application_guest_context_rebind_ready(const qa_application *application,
                                                const qa_scene_frame *current_frame,
                                                qa_error *error)
{
    if (!application || application->operation != APPLICATION_IDLE || application->q3_round_active || application->frame_preparing ||
        application->q1_original_save ||
        !application->session || !application->world || !application->console ||
        !qa_session_safe(application->session) ||
        !qa_session_destroy_ready(application->session) ||
        !qa_world_idle(application->world) || !qa_combat_idle(application->combat) ||
        !qa_console_idle(application->console) || !application_guests_idle(application) ||
        !application_rankings_idle(application) ||
        !application_bots_can_destroy(application) ||
        (application->modes && !qa_modes_idle(application->modes)) ||
        (application->equipment && !qa_equipment_idle(application->equipment)) ||
        application->publication_started || application->destroy_requested ||
        application->finalizing || application->pending_close ||
        application->routing_snapshot || application->routing_providers ||
        application->routing_provider_count ||
        (application->state != QA_APPLICATION_READY &&
         application->state != QA_APPLICATION_RUNNING))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "frontend publication requires idle application callback owners");
    for (application_provider *provider = application->live_providers;
         provider; provider = provider->next_live)
        if (provider->constructed && provider->attached &&
            !application_guest_frontend_rebind_ready(provider, current_frame,
                application->guest_context, error))
            return false;
    return true;
}

void qa_application_guest_context_rebind(qa_application *application, void *context,
                                           qa_scene_frame *destination_frame)
{
    if (!application || application->operation != APPLICATION_IDLE || application->q3_round_active || application->frame_preparing ||
        !application_rankings_idle(application)) return;
    void *previous_context = application->guest_context;
    for (application_provider *provider = application->live_providers;
         provider; provider = provider->next_live)
        if (provider->constructed && provider->attached)
            application_guest_frontend_rebind(provider, destination_frame,
                previous_context, context);
    application->guest_context = context;
    if (application->prompt_context == previous_context)
        application->prompt_context = context;
}

bool qa_application_q1_paused(const qa_application *application)
{ return application && application->q1_paused; }

uint64_t application_frame_revision(const qa_application *application)
{ return application ? application->frame_revision : 0; }

bool qa_application_complete_frame(qa_application *application, qa_error *error)
{
    if (!application || application->operation != APPLICATION_IDLE || application->q3_round_active || application->frame_preparing ||
        application->client_preparation ||
        application->q1_original_save ||
        application->publication_started || application->destroy_requested ||
        application->finalizing || application->pending_close ||
        (application->state != QA_APPLICATION_READY &&
         application->state != QA_APPLICATION_RUNNING) ||
        !application_guests_idle(application) || !application_rankings_idle(application) || !qa_session_safe(application->session) ||
        (application->world && !qa_world_idle(application->world)))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "completed frame requires its actual idle driver boundary");
    if (application->frame_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "completed frame revision is exhausted");
    ++application->frame_revision;
    return true;
}

bool qa_application_clients_drain(qa_application *application, qa_error *error)
{
    if (!application || application->operation != APPLICATION_IDLE ||
        application->q3_round_active || application->frame_preparing ||
        application->client_preparation || application->q1_original_save ||
        qa_application_startup_pending(application) ||
        application->publication_started || application->destroy_requested ||
        application->finalizing || application->pending_close ||
        application->routing_snapshot || application->routing_providers ||
        application->routing_provider_count ||
        (application->state != QA_APPLICATION_READY &&
         application->state != QA_APPLICATION_RUNNING) ||
        !qa_console_idle(application->console) ||
        !application_guests_idle(application) ||
        !application_rankings_idle(application) ||
        !application_bots_can_destroy(application) ||
        !qa_session_safe(application->session) ||
        (application->world && !qa_world_idle(application->world)) ||
        (application->combat && !qa_combat_idle(application->combat)) ||
        (application->modes && !qa_modes_idle(application->modes)) ||
        (application->equipment && !qa_equipment_idle(application->equipment)))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "client drop drain requires returned application callbacks");
    return application_native_q3_clients_drain(application, error);
}

bool application_q1_pause_set(qa_application *application, application_provider *provider,
    bool paused, qa_error *error)
{
    if (!application || application->operation != APPLICATION_IDLE || application->q3_round_active || application->frame_preparing ||
        application->state != QA_APPLICATION_RUNNING || !application_guests_idle(application) ||
        !application_rankings_idle(application) ||
        !qa_session_safe(application->session) || !qa_world_idle(application->world) ||
        !provider || !provider->constructed || !provider->attached ||
        provider != application_world_provider(application, QA_ROLE_ENTITIES, "") ||
        (provider->kind != APPLICATION_PROVIDER_Q1 && provider->kind != APPLICATION_PROVIDER_QC))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Quake pause requires the idle active Quake source server");
    application->q1_paused = paused;
    return true;
}

bool qa_application_advance(qa_application *application, uint64_t elapsed_ns,
                            qa_error *error)
{
    qa_application_travel_view travel;
    bool pending_map = qa_application_travel_read(application, &travel) &&
        travel.target.kind == QA_TRAVEL_MAP;
    if (application == NULL || application->operation != APPLICATION_IDLE || application->q3_round_active || application->frame_preparing ||
        application->client_preparation ||
        qa_application_startup_pending(application) || pending_map || application->q1_original_save ||
        !application_guests_idle(application) || !application_rankings_idle(application) ||
        application->state != QA_APPLICATION_RUNNING)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "frame advance requires an idle running application");
    if (application->q1_paused) return true;
    application_provider *source = application_world_provider(application, QA_ROLE_ENTITIES, "");
    bool command_only = elapsed_ns == 0 && source &&
        source->kind == APPLICATION_PROVIDER_Q2;
    application->operation = APPLICATION_ADVANCING;
    bool ok = command_only
        ? qa_session_command_turn(application->session, error)
        : qa_session_advance(application->session, elapsed_ns, error);
    if (!ok) {
        qa_error cleanup = {0};
        (void)application_control_frames_abort(application, &cleanup);
    }
    if (ok)
        ok = application_players_advance(application, error);
    if (ok && application->modes != NULL) {
        uint64_t now = qa_session_elapsed(application->session);
        for (size_t index = 0; index < application->mode_count; ++index) {
            if (application_native_q3_mode_frame_owned(application,
                                                        application->mode_ids[index]))
                continue;
            if (!qa_modes_frame(application->modes,
                                application->mode_ids[index], now, elapsed_ns,
                                error)) {
                ok = false;
                break;
            }
        }
    }
    if (ok)
        ok = application_rankings_frame_ordinary(application, error);
    if (ok)
        ok = application_q3_components_drain(application->components, error);
    if (ok)
        ok = application_native_q1_wire_observe(application, error);
    if (ok)
        ok = application_native_q3_clients_drain(application, error);
    if (ok)
        ok = application_q3_publish_local_snapshots(application, error);
    application->operation = APPLICATION_IDLE;
    qa_error close_error = {0};
    if (!application_drain_provider_closes(application, &close_error)) {
        application_fault(application, &close_error);
        if (ok && error != NULL)
            *error = close_error;
        ok = false;
    }
    if (!ok)
        application_fault(application,
                          qa_session_faulted(application->session)
                              ? qa_session_error(application->session)
                              : error);
    return ok;
}

static bool shutdown_admitted_bots(qa_application *application, qa_error *error)
{
    if (!application->source_shutdown_admitted)
        return true;
    if (!application_bots_shutdown(application, false, error))
        return false;
    application->source_shutdown_admitted = false;
    return true;
}

static bool retire_control_inputs(qa_application *application, qa_error *error)
{
    if (application_control_frames_idle(application)) return true;
    if (application->operation != APPLICATION_IDLE || application->q3_round_active ||
        application->frame_preparing ||
        (application->session && !qa_session_safe(application->session)))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "input retirement requires returned source invocations");
    application->operation = APPLICATION_DESTROYING;
    bool okay = application_control_frames_abort(application, error);
    application->operation = APPLICATION_IDLE;
    return okay;
}

static bool retire_sources(qa_application *application, bool server, bool restarting, qa_error *error)
{
    if (application == NULL)
        return true;
    if (application->client_preparation)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "CLIENT preparation still retains the application");
    if (!retire_control_inputs(application, error)) return false;
    if (!application_native_q2_callbacks_drain_application(application, error)) return false;
    if (application->operation != APPLICATION_IDLE || application->q3_round_active || application->frame_preparing || application->destroy_requested ||
        !qa_console_idle(application->console) ||
        (application->cvars && !qa_cvars_observer_idle(application->cvars)) ||
        (application->pickups && !qa_pickups_idle(application->pickups)) ||
        (application->combat && !qa_combat_idle(application->combat)) ||
        !qa_inventory_idle(application->inventory) ||
        !application_guests_idle(application) || !application_rankings_idle(application) ||
        !application_bots_can_destroy(application) ||
        (application->world != NULL && !qa_world_idle(application->world)) ||
        (application->session != NULL && !qa_session_destroy_ready(application->session)))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "source retirement requires idle application owners");
    if (application->world_change_ready != NULL &&
        !application->world_change_ready(application->guest_context, application, error))
        return false;
    if (restarting && !application_map_stop_prepare(application, error)) return false;
    application->operation = APPLICATION_DESTROYING;
    if (!server) application->state = QA_APPLICATION_STOPPING;
    bool ok = application_rankings_close(application, error) &&
        shutdown_admitted_bots(application, error) &&
        (server ? qa_configuration_clear(application->configuration, error) :
            application_composition_destroy(application, error)) &&
        (application->session == NULL || application_drain_provider_closes(application, error));
    application->operation = APPLICATION_IDLE;
    if (!ok)
        return false;
    if (application->provider_states != 0)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "source retirement still has retained provider owners");
    if (server) {
        application_players_close(application);
        if (!restarting) application_map_dispose(application);
        application->state = QA_APPLICATION_READY;
    }
    return true;
}
bool qa_application_retire_sources(qa_application *application, qa_error *error)
{ return retire_sources(application, false, false, error); }

bool qa_application_end_game(qa_application *application, qa_error *error)
{
    if (!application || application->state == QA_APPLICATION_STOPPING ||
        application->state == QA_APPLICATION_FAULTED)
        return application_fail(error, QA_ERROR_ARGUMENT, "Ending a game requires its live engine");
    return retire_sources(application, true, false, error);
}

bool qa_application_stop_server(qa_application *application, qa_actor_owner owner, qa_error *error)
{
    if (!application || !owner || application->state == QA_APPLICATION_STOPPING ||
        application->state == QA_APPLICATION_FAULTED)
        return application_fail(error, QA_ERROR_ARGUMENT, "Server shutdown requires its live application owner");
    if (!qa_application_launch(application)) return true;
    application_provider *source = application_world_provider(application, QA_ROLE_ENTITIES, "");
    if (!source || source->owner != owner || !source->constructed ||
        (source->kind != APPLICATION_PROVIDER_Q2 &&
         !(source->kind == APPLICATION_PROVIDER_NATIVE && source->state.native.q2_engine)))
        return application_fail(error, QA_ERROR_ARGUMENT, "Server shutdown lost its actual Q2 primary source");
    return retire_sources(application, true, true, error);
}

bool qa_application_destroy(qa_application *application, qa_error *error)
{
    if (application == NULL)
        return true;
    if (application->engine_shutdown)
        return application_fail(error, QA_ERROR_ARGUMENT, "ENGINE shutdown still retains its application parents");
    if (application->client_preparation)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "CLIENT preparation still retains the application");
    if (!retire_control_inputs(application, error)) return false;
    if (!application_native_q2_callbacks_drain_application(application, error)) return false;
    if (application->operation != APPLICATION_IDLE || application->q3_round_active || application->frame_preparing ||
        !qa_console_destroy_ready(application->console) ||
        (application->cvars && !qa_cvars_observer_idle(application->cvars)) ||
        (application->pickups && !qa_pickups_idle(application->pickups)) ||
        (application->combat && !qa_combat_idle(application->combat)) ||
        !qa_inventory_idle(application->inventory) ||
        !application_guests_idle(application) || !application_rankings_idle(application) ||
        !application_bots_can_destroy(application) ||
        !qa_rankings_close_ready(application->rankings) ||
        (application->world != NULL && !qa_world_idle(application->world)) ||
        (application->session != NULL &&
         !qa_session_destroy_ready(application->session)))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "application destruction requires a safe point");
    if (!application_native_q2_baselines_destroy(application, error)) return false;
    application->operation = APPLICATION_DESTROYING;
    if (!application_rankings_close(application, error) ||
        !shutdown_admitted_bots(application, error) ||
        !application_composition_destroy(application, error) ||
        (application->session != NULL &&
         !qa_session_retire_world(application->session, error)) ||
        (application->session != NULL &&
         !application_drain_provider_closes(application, error))) {
        application->operation = APPLICATION_IDLE;
        return false;
    }

    /* Configuration destruction can synchronously release its last provider
     * owners. Do not make those callbacks eligible to free this enclosing
     * frame: the public-destroy latch is set only after it returns. */
    application->destroy_requested = true;
    if (application->provider_states != 0)
        return true;
    if (application_finalize(application, error))
        return true;

    application->destroy_requested = false;
    application->operation = APPLICATION_IDLE;
    return false;
}

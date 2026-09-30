#include "internal.h"
#include "save_private.h"
#include "save_native_q2.h"
#include "save_content.h"
#include "qa/rankings_save.h"
#include "qa/player_progress_save.h"
#include "qa/catalog_save.h"

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
    };
}

static bool discover(qa_application *application, bool discover_mods,
                     uint64_t generation, qa_catalog **out, qa_error *error)
{
    qa_catalog_options options = {
        .resources = application->resources,
        .content_root = application->content_root,
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
        options->component_capacity == 0)
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
    application->guest_context = options->guest_context;
    application->q3_services = options->q3_services;
    application->q3_client_effect = options->q3_client_effect;
    application->native_q2_services = options->native_q2_services;
    application->world_change_ready = options->world_change_ready;
    application->before_world_change = options->before_world_change;
    application->world_retired = options->world_retired;
    application->console_print = options->console_print;
    if (!application_console_create(application, error)) goto fail;
    qa_builtin_random_seed(&application->random,
                           (uint32_t)options->catalog_generation ^ UINT32_C(0x71616e74));
    application->content_root = copy_text(options->content_root, error);
    application->user_root = copy_text(options->user_root, error);
    if (application->content_root == NULL ||
        (options->user_root != NULL && application->user_root == NULL))
        goto fail;

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
    if (application->user_root != NULL &&
        (!qa_fs_root_open(application->user_root, &application->user_files,
                          error) ||
         !(restored_owners
            ? qa_player_progress_create_restored(application->user_files,
                "player-progress.json", &application->progress, error)
            : qa_player_progress_open(application->user_files,
                "player-progress.json", &application->progress, error))))
        goto fail;
    application->catalog_generation = (restore || baseline_catalog)
        ? qa_catalog_generation(application->catalog) : options->catalog_generation;
    application->discover_mods = options->discover_mods;
    if (!restore && !baseline_catalog && !discover(application, options->discover_mods,
                  application->catalog_generation, &application->catalog, error))
        goto fail;

    qa_session_options session = {
        .actor_capacity = options->actor_capacity,
        .component_capacity = options->component_capacity,
        .mixed_order = options->mixed_source_order,
        .actor_released = application_actor_released,
        .source_actor = application_arsenal_source_actor,
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
    if (!qa_inventory_create(qa_session_actor_registry(application->session),
                             &application->inventory, error))
        goto fail;
    qa_combat_hooks combat_hooks = application_combat_hooks(application);
    if (!qa_combat_create(qa_session_actor_registry(application->session), &combat_hooks,
                          &application->combat, error))
        goto fail;
    if (!qa_pickups_create(qa_session_actor_registry(application->session),
                           application->combat, application->inventory,
                           &application->pickups, error))
        goto fail;
    if (!application_composition_create(application, error))
        goto fail;

    *out = application;
    return true;

fail:
    /* Restored actors can be live before the first provider is admitted. Their
     * release observers still borrow every shared service and control array. */
    if (application->session != NULL)
        (void)qa_session_retire_world(application->session, NULL);
    application_map_dispose(application);
    qa_console_destroy(application->console);
    qa_cvars_destroy(application->cvars);
    (void)application_composition_destroy(application, NULL);
    qa_targets_destroy(application->targets);
    application->targets = NULL;
    free(application->physics);
    free(application->motion);
    free(application->controls);
    free(application->q2_visuals);
    qa_arena_destroy(&application->event_arena);
    free(application->events);
    free(application->q2_map_events);
    free(application->q3_map_events);
    free(application->q2_player_events);
    free(application->protocol_events);
    free(application->shader_remaps);
    (void)qa_session_destroy(application->session, NULL);
    application->session = NULL;
    (void)qa_pickups_destroy(application->pickups, NULL);
    (void)qa_combat_destroy(application->combat, NULL);
    (void)qa_inventory_destroy(application->inventory, NULL);
    qa_catalog_release(application->catalog);
    qa_player_progress_close(application->progress);
    (void)qa_rankings_close(application->rankings, NULL);
    qa_fs_root_close(application->user_files);
    qa_resource_pool_destroy(application->resources);
    application_save_content_destroy(application->content_graph);
    free(application->content_root);
    free(application->user_root);
    free(application);
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
    for (size_t i = 0; i < application->provider_count; ++i) {
        const application_provider *provider = application->providers[i];
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
    if (application == NULL || application->operation != APPLICATION_IDLE ||
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
    if (ok) {
        qa_catalog *previous = application->catalog;
        application->catalog = candidate;
        application->catalog_generation = generation;
        application->discover_mods = discover_mods;
        qa_catalog_release(previous);
    }
    application->operation = APPLICATION_IDLE;
    return ok;
}

bool qa_application_apply(qa_application *application,
                          const qa_launch_draft *draft, qa_error *error)
{
    if (application == NULL || draft == NULL ||
        application->operation != APPLICATION_IDLE ||
        !application_guests_idle(application) ||
        !application_bots_can_destroy(application) ||
        application->state == QA_APPLICATION_FAULTED ||
        application->state == QA_APPLICATION_STOPPING)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "configuration requires an idle healthy application");
    application->operation = APPLICATION_CONFIGURING;
    bool ok = application_apply(application, draft, error);
    application->operation = APPLICATION_IDLE;
    if (ok && application->state == QA_APPLICATION_READY)
        application->state = QA_APPLICATION_RUNNING;
    return ok;
}

bool qa_application_guest_context_rebind_ready(const qa_application *application,
                                                const qa_scene_frame *current_frame,
                                                qa_error *error)
{
    if (!application || application->operation != APPLICATION_IDLE ||
        !application->session || !application->world || !application->console ||
        !qa_session_safe(application->session) ||
        !qa_session_destroy_ready(application->session) ||
        !qa_world_idle(application->world) || !qa_combat_idle(application->combat) ||
        !qa_console_idle(application->console) || !application_guests_idle(application) ||
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
    void *previous_context = application->guest_context;
    for (application_provider *provider = application->live_providers;
         provider; provider = provider->next_live)
        if (provider->constructed && provider->attached)
            application_guest_frontend_rebind(provider, destination_frame,
                previous_context, context);
    application->guest_context = context;
}

bool qa_application_q1_paused(const qa_application *application)
{ return application && application->q1_paused; }

uint64_t application_frame_revision(const qa_application *application)
{ return application ? application->frame_revision : 0; }

bool qa_application_complete_frame(qa_application *application, qa_error *error)
{
    if (!application || application->operation != APPLICATION_IDLE ||
        application->publication_started || application->destroy_requested ||
        application->finalizing || application->pending_close ||
        (application->state != QA_APPLICATION_READY &&
         application->state != QA_APPLICATION_RUNNING) ||
        !application_guests_idle(application) || !qa_session_safe(application->session) ||
        (application->world && !qa_world_idle(application->world)))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "completed frame requires its actual idle driver boundary");
    if (application->frame_revision == UINT64_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "completed frame revision is exhausted");
    ++application->frame_revision;
    return true;
}

bool application_q1_pause_set(qa_application *application, application_provider *provider,
    bool paused, qa_error *error)
{
    if (!application || application->operation != APPLICATION_IDLE ||
        application->state != QA_APPLICATION_RUNNING || !application_guests_idle(application) ||
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
    if (application == NULL || application->operation != APPLICATION_IDLE ||
        !application_guests_idle(application) ||
        application->state != QA_APPLICATION_RUNNING)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "frame advance requires an idle running application");
    if (application->q1_paused) return true;
    application->operation = APPLICATION_ADVANCING;
    bool ok = qa_session_advance(application->session, elapsed_ns, error);
    if (ok)
        ok = application_players_advance(application, error);
    if (ok)
        ok = application_bots_frame(application, error);
    if (ok && application->modes != NULL) {
        uint64_t now = qa_session_elapsed(application->session);
        for (size_t index = 0; index < application->mode_count; ++index)
            if (!qa_modes_frame(application->modes,
                                application->mode_ids[index], now, elapsed_ns,
                                error)) {
                ok = false;
                break;
            }
    }
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

bool qa_application_retire_sources(qa_application *application, qa_error *error)
{
    if (application == NULL)
        return true;
    if (application->operation != APPLICATION_IDLE || application->destroy_requested ||
        !application_guests_idle(application) ||
        !application_bots_can_destroy(application) ||
        (application->world != NULL && !qa_world_idle(application->world)) ||
        (application->session != NULL && !qa_session_destroy_ready(application->session)))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "source retirement requires idle application owners");
    if (application->world_change_ready != NULL &&
        !application->world_change_ready(application->guest_context, application, error))
        return false;
    application->operation = APPLICATION_DESTROYING;
    application->state = QA_APPLICATION_STOPPING;
    bool ok = application_composition_destroy(application, error) &&
        (application->session == NULL || application_drain_provider_closes(application, error));
    application->operation = APPLICATION_IDLE;
    if (!ok)
        return false;
    if (application->provider_states != 0)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "source retirement still has retained provider owners");
    return true;
}

bool qa_application_destroy(qa_application *application, qa_error *error)
{
    if (application == NULL)
        return true;
    if (application->operation == APPLICATION_IDLE &&
        !application_native_q2_baselines_destroy(application, error))
        return false;
    if (application->operation != APPLICATION_IDLE ||
        !application_guests_idle(application) ||
        !application_bots_can_destroy(application) ||
        !qa_rankings_close_ready(application->rankings) ||
        (application->world != NULL && !qa_world_idle(application->world)) ||
        (application->session != NULL &&
         !qa_session_destroy_ready(application->session)))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "application destruction requires a safe point");
    application->operation = APPLICATION_DESTROYING;
    if (!application_composition_destroy(application, error) ||
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

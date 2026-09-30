#include "internal.h"
#include "match_intents.h"
#include "network_q1_signon.h"
#include "guest_native_q2_private.h"
#include "native_q3_console.h"
#include "portals.h"

#include <stdlib.h>

bool application_guests_idle(const qa_application *application)
{
    if (application == NULL)
        return false;
    for (const application_provider *provider = application->live_providers;
         provider != NULL; provider = provider->next_live) {
        if (!application_native_q3_console_idle(provider))
            return false;
        for (size_t index = 0;; ++index) {
            qa_console *console;
            if (!application_guest_console_at((application_provider *)provider, index,
                                                &console, NULL, NULL))
                break;
            if (console != application->console && !qa_console_idle(console))
                return false;
        }
        if (provider->kind == APPLICATION_PROVIDER_QC) {
            if ((provider->state.qc.instance != NULL &&
                 !qa_qc_idle(provider->state.qc.instance)) ||
                (provider->state.qc.game != NULL &&
                 !qa_qc_game_idle(provider->state.qc.game)) ||
                !application_qc_input_idle(provider))
                return false;
        } else if (provider->kind == APPLICATION_PROVIDER_NATIVE &&
                   provider->state.native.q2_engine != NULL) {
            if (!application_native_q2_idle(provider))
                return false;
        } else if (!application_q3_guest_idle(provider)) {
            return false;
        }
    }
    return true;
}

static void remember(bool result, const qa_error *current, const char *fallback,
                     bool *ok, qa_error *first)
{
    if (result || !*ok)
        return;
    *ok = false;
    if (current != NULL && current->code != QA_OK)
        *first = *current;
    else
        qa_error_set(first, QA_ERROR_ARGUMENT, 0, "%s", fallback);
}

bool application_provider_close(qa_application *application,
                                application_provider *provider,
                                qa_error *error)
{
    if (application == NULL || provider == NULL ||
        provider->application != application ||
        application->provider_states == 0)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "invalid provider lifetime release");
    if (provider->attached || provider->component_attached ||
        provider->policy_attached)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "cannot close an attached provider");
    if (!application_provider_deconstruct(provider, error))
        return false;
    if (!application_portals_close(application, provider->owner, error))
        return false;

    if (provider->kind == APPLICATION_PROVIDER_QC) {
        application_qc_release_qualification(provider);
        qa_qc_program_destroy(provider->state.qc.program);
        provider->state.qc.program = NULL;
    } else if (provider->kind == APPLICATION_PROVIDER_QVM) {
        qa_qvm_image_release(provider->state.qvm.image);
        provider->state.qvm.image = NULL;
    } else if (provider->kind == APPLICATION_PROVIDER_NATIVE) {
        qa_native_module_release(provider->state.native.module);
        provider->state.native.module = NULL;
    }

    qa_catalog_release(provider->product_catalog);
    qa_launch_instance_lease_release(provider->launch_lease);
    if (provider->previous_live != NULL)
        provider->previous_live->next_live = provider->next_live;
    else
        application->live_providers = provider->next_live;
    if (provider->next_live != NULL)
        provider->next_live->previous_live = provider->previous_live;
    provider->close_pending = false;
    free(provider);
    --application->provider_states;
    return true;
}

static void queue_close(qa_application *application,
                        application_provider *provider)
{
    if (provider->close_pending)
        return;
    provider->close_pending = true;
    provider->next_close = application->pending_close;
    application->pending_close = provider;
}

void application_provider_release(qa_application *application,
                                  application_provider *provider)
{
    if (application == NULL || provider == NULL)
        return;
    if (application->operation == APPLICATION_ADVANCING ||
        application->operation == APPLICATION_PERSISTING ||
        application->session == NULL ||
        !qa_session_safe(application->session)) {
        queue_close(application, provider);
        return;
    }

    qa_error error = {0};
    if (!application_provider_close(application, provider, &error)) {
        queue_close(application, provider);
        application_fault(application, &error);
        return;
    }

    if (application->destroy_requested && application->provider_states == 0 &&
        !application->finalizing) {
        if (!application_finalize(application, &error))
            application_fault(application, &error);
    }
}

bool application_drain_provider_closes(qa_application *application,
                                       qa_error *error)
{
    if (application == NULL)
        return true;
    if (application->session == NULL ||
        !qa_session_safe(application->session))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "provider close drain requires a session safe point");

    while (application->pending_close != NULL) {
        application_provider *provider = application->pending_close;
        application->pending_close = provider->next_close;
        provider->next_close = NULL;
        provider->close_pending = false;
        if (!application_provider_close(application, provider, error)) {
            queue_close(application, provider);
            return false;
        }
    }
    return true;
}

bool application_finalize(qa_application *application, qa_error *error)
{
    if (application == NULL)
        return true;
    if (!application->destroy_requested || application->finalizing ||
        application->configuration != NULL || application->provider_states != 0 ||
        application->pending_close != NULL || application->live_providers != NULL)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "application services still have retained owners");
    if (!qa_session_destroy_ready(application->session) ||
        !qa_rankings_close_ready(application->rankings) ||
        (application->world != NULL && !qa_world_idle(application->world)))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "application session still has active calls or admissions");
    if (!application_bots_destroy(application, error))
        return false;
    if (!application_match_intents_idle(application->match_intents))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "match map continuation is borrowed");
    application->finalizing = true;

    qa_error first = {0};
    qa_error current = {0};
    bool ok = true;

    bool rankings_closed = qa_rankings_close(application->rankings, &current);
    application->rankings = NULL;
    remember(rankings_closed, &current, "ranking cleanup failed", &ok, &first);
    current = (qa_error){0};

    qa_equipment_destroy(application->equipment);
    application->equipment = NULL;
    qa_modes_destroy(application->modes);
    application->modes = NULL;
    application_match_intents_destroy(application->match_intents);
    application->match_intents = NULL;
    application_q1_signon_destroy(application);
    application_portals_destroy(application);
    qa_targets_destroy(application->targets);
    application->targets = NULL;

    bool destroyed = qa_pickups_destroy(application->pickups, &current);
    remember(destroyed, &current, "pickup destruction failed", &ok, &first);
    if (destroyed)
        application->pickups = NULL;
    current = (qa_error){0};
    destroyed = qa_combat_destroy(application->combat, &current);
    remember(destroyed, &current, "combat destruction failed", &ok, &first);
    if (destroyed)
        application->combat = NULL;
    current = (qa_error){0};
    destroyed = qa_inventory_destroy(application->inventory, &current);
    remember(destroyed, &current, "inventory destruction failed", &ok, &first);
    if (destroyed)
        application->inventory = NULL;
    if (!ok) {
        application->finalizing = false;
        if (error != NULL)
            *error = first;
        return false;
    }

    if (!qa_session_destroy_ready(application->session) ||
        (application->world != NULL && !qa_world_idle(application->world))) {
        application->finalizing = false;
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "application session acquired a teardown admission");
    }
    current = (qa_error){0};
    remember(qa_session_destroy(application->session, &current), &current,
             "session destruction failed", &ok, &first);
    /* The readiness check established that destruction consumes this owner,
     * including when a recorded cleanup fault is returned. */
    application->session = NULL;
    application->world = NULL;
    if (!ok) {
        application->finalizing = false;
        if (error != NULL)
            *error = first;
        return false;
    }

    application_map_dispose(application);
    qa_collision_destroy(application->geometry);
    qa_resource_release(application->map_resource);
    free(application->physics);
    qa_arena_destroy(&application->event_arena);
    free(application->events);
    free(application->q2_map_events);
    free(application->q3_map_events);
    free(application->q2_player_events);
    free(application->protocol_events);
    free(application->mode_ids);
    free(application->motion);
    if (application->controls != NULL)
        for (uint32_t slot = 0; slot < application->control_capacity; ++slot)
            qa_movement_result_free(&application->controls[slot].result);
    free(application->controls);
    free(application->q2_visuals);
    free(application->shader_remaps);
    free(application->providers);
    qa_catalog_release(application->catalog);
    qa_console_destroy(application->console);
    qa_cvars_destroy(application->cvars);
    qa_player_progress_close(application->progress);
    qa_fs_root_close(application->user_files);
    qa_resource_pool_destroy(application->resources);
    free(application->content_root);
    free(application->user_root);
    free(application);
    return true;
}

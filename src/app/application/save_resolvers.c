#include "save_private.h"
#include "guest_projection_private.h"
#include "guest_native_q2_private.h"
#include "guest_native_q2_combat.h"
#include "qa/application_network.h"
#include "qa/frontend.h"
#include "qa/game_q1_checkpoint.h"
#include "qa/game_q1_inventory.h"
#include "qa/game_q2_checkpoint.h"
#include "qa/game_q3_save.h"
#include "qa/modes_save.h"
#include "guest_q3_combat.h"
#include "supplies.h"
#include "guest_q3_components.h"

static application_provider *source_owner(qa_application *app, qa_actor_owner owner)
{
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider && provider->attached && provider->constructed &&
            !provider->close_pending && provider->owner == owner)
            return provider;
    }
    return NULL;
}

static bool combat_binding(void *opaque, qa_actor_id actor, uint64_t serial,
                            qa_combat_binding *out, qa_error *error)
{
    qa_application *app = opaque;
    application_q3_component *component=application_q3_components_actor_owner(app,actor);
    if(component) return application_q3_component_combat_binding(component,actor,serial,out,error);
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
    application_provider *provider = record ? source_owner(app, record->owner) : NULL;
    application_provider *character = application_provider_for(app, actor, QA_ROLE_CHARACTER, "");
    struct application_q3_guest *source = character ? q3g_engine(character) : NULL;
    if (source && source->game && source->game->combat)
        return application_q3_combat_binding(source->game->combat, actor, serial, out, error);
    if (provider && provider->kind == APPLICATION_PROVIDER_NATIVE &&
        provider->state.native.q2_engine)
        return application_native_q2_combat_binding(provider, actor, serial, out, error);
    struct application_q3_guest *guest = provider ? q3g_engine(provider) : NULL;
    if (guest && guest->game && guest->game->combat)
        return application_q3_combat_binding(guest->game->combat, actor, serial, out, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "Saved combat primary has no restored source callback owner");
}

static bool admission(void *opaque, qa_actor_id actor,
                       qa_combat_admission *out, qa_error *error)
{
    qa_application *app = opaque;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
    application_provider *provider = record ? source_owner(app, record->owner) : NULL;
    if (provider && provider->kind == APPLICATION_PROVIDER_Q3)
        return qa_q3_game_damage_admission(provider->state.q3, actor, out, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "Saved damage admission has no restored native callback owner");
}

static bool primary(void *opaque, qa_actor_id actor, uint64_t serial,
                    qa_inventory_binding *out, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = application_provider_for(app, actor, QA_ROLE_INVENTORY, "");
    if (provider && provider->kind == APPLICATION_PROVIDER_NATIVE &&
        provider->state.native.q2_engine)
        return application_native_q2_inventory_binding(provider, actor, serial, out, error);
    struct application_q3_guest *guest = provider ? q3g_engine(provider) : NULL;
    if (guest && guest->game)
        return application_guest_projection_inventory_binding(guest->game, actor,
                                                                serial, out, error);
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "Saved primary inventory has no restored source callback owner");
}

static bool inventory_group(void *opaque, qa_actor_id actor, uint64_t serial,
    const qa_inventory_source_group *saved, qa_inventory_items *out, qa_error *error)
{
    qa_application *app = opaque;
    if (!saved)
        return application_fail(error, QA_ERROR_ARGUMENT, "Saved inventory group is absent");
    qa_actor_owner modes_owner = qa_strings_find(qa_session_strings(app->session),
        (qa_bytes){(const uint8_t *)"application:modes", sizeof("application:modes") - 1});
    if (app->modes && modes_owner && saved->owner == modes_owner)
        return qa_modes_inventory_group(app->modes, actor, serial, saved, out, error);
    application_provider *provider = source_owner(app, saved->owner);
    if (provider) {
        if (provider->kind == APPLICATION_PROVIDER_Q1)
            return qa_q1_game_inventory_group(provider->state.q1, actor, serial, saved, out, error);
        if (provider->kind == APPLICATION_PROVIDER_Q2)
            return qa_q2_game_inventory_group(provider->state.q2, actor, serial, saved, out, error);
        if (provider->kind == APPLICATION_PROVIDER_Q3)
            return qa_q3_game_inventory_group(provider->state.q3, actor, serial, saved, out, error);
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "Saved inventory group has no restored source callback owner");
}

static bool pickup_observer(void *opaque, qa_actor_id actor, qa_actor_owner owner,
    uint64_t serial, qa_pickup_observer *out, qa_error *error)
{
    qa_application *app = opaque;
    application_provider *provider = source_owner(app, owner);
    if (provider) {
        if (provider->kind == APPLICATION_PROVIDER_Q1)
            return qa_q1_game_pickup_observer(provider->state.q1, actor, owner, serial, out, error);
        if (provider->kind == APPLICATION_PROVIDER_Q2)
            return qa_q2_game_pickup_observer(provider->state.q2, actor, owner, serial, out, error);
        if (provider->kind == APPLICATION_PROVIDER_Q3)
            return qa_q3_game_pickup_observer(provider->state.q3, actor, serial, out, error);
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "Saved pickup observation has no restored source callback owner");
}

static bool pickup_rule(void *opaque,qa_actor_id actor,qa_actor_owner owner,
    uint64_t serial,uint32_t id,qa_pickup_rule *out,qa_error *error)
{
    qa_application *app = opaque;
    return application_supplies_pickup_rule(app->supplies,actor,owner,serial,id,out,error);
}

static bool target(void *opaque, qa_actor_id actor, qa_clock_kind clock,
                     qa_target_binding *out, qa_error *error)
{
    qa_application *app = opaque;
    const qa_actor_record *record = qa_actors_get(qa_session_actors(app->session), actor);
    application_provider *provider = record ? source_owner(app, record->owner) : NULL;
    bool ok = false;
    if (provider) {
        if (provider->kind == APPLICATION_PROVIDER_Q1)
            ok = qa_q1_game_target_binding(provider->state.q1, actor, out, error);
        else if (provider->kind == APPLICATION_PROVIDER_Q2)
            ok = qa_q2_game_target_binding(provider->state.q2, actor, out, error);
        else if (provider->kind == APPLICATION_PROVIDER_Q3)
            ok = qa_q3_game_target_binding(provider->state.q3, actor, out, error);
    }
    if (ok && out->source == clock)
        return true;
    if (error && error->code != QA_OK)
        return false;
    return application_fail(error, QA_ERROR_FORMAT,
                            "Saved authored target has no matching source declaration");
}

bool application_save_resolvers(qa_application *app,
    qa_persistence_gameplay_resolvers *out, qa_error *error)
{
    if (!app || !app->session || !out || app->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Restored gameplay callbacks require retained application owners");
    *out = (qa_persistence_gameplay_resolvers){.context = app,
        .combat = combat_binding, .admission = admission, .inventory_primary = primary,
        .inventory_group = inventory_group, .pickup_observer = pickup_observer, .pickup_rule = pickup_rule,
        .target = target};
    return true;
}

static bool console_identity(void *opaque, qa_console_save_identity kind,
    uint64_t saved, uint64_t *out, qa_error *error)
{
    const application_save_console_context *context = opaque;
    qa_application *app = context->application;
    if (!saved) {
        *out = 0;
        return true;
    }
    if (kind == QA_CONSOLE_SAVE_OWNER &&
        (saved == QA_FRONTEND_COMMAND_OWNER ||
         (saved == QA_NETWORK_COMMAND_OWNER && qa_application_network_command_owner_bound(app)) ||
         (saved <= UINT32_MAX &&
          qa_strings_text(qa_session_strings(app->session), (qa_string_id)saved).data))) {
        /* Foundation restoration preserves every string's exact index,
         * including retired provider and source-service lifetime names. */
        *out = saved;
        return true;
    }
    return application_fail(error, QA_ERROR_UNSUPPORTED,
                            "Saved console lifetime has no declared application identity");
}

static bool console_context(void *opaque, uint64_t captured_registry,
    const qa_command_context *saved, qa_command_context *out, qa_error *error)
{
    const application_save_console_context *context = opaque;
    if (!captured_registry || saved->session || saved->client)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
                                "Saved command session or client has no declared source identity");
    qa_command_context restored = *saved;
    if (!console_identity(opaque, QA_CONSOLE_SAVE_OWNER, saved->owner,
                          &restored.owner, error))
        return false;
    if (saved->registry == captured_registry &&
        saved->generation == context->saved_command_generation) {
        restored.registry = qa_actors_identity(qa_session_actors(context->application->session));
    } else if (saved->registry || saved->generation) {
        /* Keep stale publications inactive even when a saved registry number
         * coincides with the new process's registry namespace. */
        restored.registry = 0;
        if (!restored.generation)
            restored.generation = UINT64_MAX;
    }
    *out = restored;
    return true;
}

bool application_save_console_resolvers(const application_save_console_context *context,
    qa_console_save_resolvers *out, qa_error *error)
{
    if (!context || !context->application || !context->application->session || !out ||
        context->application->destroy_requested)
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Console restoration requires retained candidate application owners");
    *out = (qa_console_save_resolvers){.context = (void *)context,
        .identity = console_identity, .command_context = console_context};
    return true;
}

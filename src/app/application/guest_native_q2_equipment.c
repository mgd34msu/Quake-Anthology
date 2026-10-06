#include "guest_native_q2_private.h"
#include "guest_native_q2_attack.h"
#include "guest_native_q2_equipment.h"
#include "qa/native_host_q2_wire.h"

static bool admitted(application_provider *provider, qa_actor_id actor)
{
    qa_application *app = provider ? provider->application : NULL;
    if (!app || !app->session || app->destroy_requested || !qa_session_safe(app->session) ||
        qa_session_faulted(app->session) || !provider->constructed || !provider->attached ||
        provider->close_pending || provider->kind != APPLICATION_PROVIDER_NATIVE ||
        !provider->launch || !provider->product || provider->product->family != QA_GAME_Q2 ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_PERSISTING &&
         app->operation != APPLICATION_ADVANCING) ||
        (app->state != QA_APPLICATION_READY && app->state != QA_APPLICATION_RUNNING) ||
        !qa_actors_get(qa_session_actors(app->session), actor) ||
        application_provider_for(app, actor, QA_ROLE_ARSENAL, "") != provider)
        return false;
    application_provider **roster = app->routing_providers ? app->routing_providers : app->providers;
    size_t count = app->routing_providers ? app->routing_provider_count : app->provider_count;
    bool found = false;
    for (size_t i = 0; i < count; ++i) found = found || roster[i] == provider;
    struct application_native_q2 *engine = provider->state.native.q2_engine;
    return found && engine && engine->provider == provider && engine->initialized &&
        engine->map_ready && !engine->shutting_down && !engine->activation_failed &&
        (engine->profile == QA_NATIVE_Q2_GAME_API3 || engine->profile == QA_NATIVE_Q2_GAME_API2023) &&
        provider->state.native.host && !qa_native_terminal(qa_native_host_instance(provider->state.native.host)) &&
        application_native_q2_idle(provider);
}

bool application_q2_guest_equipment_read(application_provider *provider, qa_actor_id actor,
    qa_application_native_q2_equipment_view *out, qa_error *error)
{
    if (!out || !admitted(provider, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 equipment requires its idle selected source owner");
    struct application_native_q2 *engine = provider->state.native.q2_engine;
    uint32_t slot = 0;
    for (uint32_t i = 1; i < 257; ++i) {
        const application_native_q2_client *client = engine->clients + i;
        if (!client->connected || !client->begun || client->disconnect_started ||
            !qa_actor_id_equal(client->actor, actor)) continue;
        if (slot) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 equipment repeats a physical client actor");
        slot = i;
    }
    if (!slot) return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 equipment has no begun physical client");
    qa_native_slot_binding binding;
    qa_native_instance *instance = qa_native_host_instance(provider->state.native.host);
    if (!qa_native_slot(instance, slot, &binding, error)) return false;
    if (binding.kind == QA_NATIVE_SLOT_FREE || !qa_actor_id_equal(binding.actor, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 equipment source binding changed its full actor");
    qa_application_native_q2_equipment_view view = {.actor = actor, .provider = provider->owner,
        .profile = engine->profile, .source_slot = slot};
    if (!application_native_q2_whole_source(engine, actor) &&
        !application_native_q2_attack_weapon_read(engine, slot, actor, &view.item, error)) return false;
    qa_q2_player player;
    if (!qa_native_host_q2_player(provider->state.native.host, slot, &player, error)) return false;
    bool rerelease = engine->profile == QA_NATIVE_Q2_GAME_API2023;
    view.view_kick_angles = qa_v3(player.kick_angles[0], player.kick_angles[1], player.kick_angles[2]);
    view.gun_angles = qa_v3(player.gunangles[0], player.gunangles[1], player.gunangles[2]);
    view.gun_offset = qa_v3(player.gunoffset[0], player.gunoffset[1], player.gunoffset[2]);
    view.gun_index = (int32_t)player.gunindex;
    view.frame = (int32_t)player.gunframe;
    if (rerelease) {
        view.skin = (int32_t)player.gunskin;
        view.rate = (int32_t)player.gunrate;
        view.has_skin = view.has_rate = true;
    }
    bool okay = qa_vec_finite(view.view_kick_angles) && qa_vec_finite(view.gun_angles) &&
        qa_vec_finite(view.gun_offset) && view.gun_index >= 0 &&
        (uint32_t)view.gun_index < engine->resource_limit[QA_NATIVE_HOST_MODEL];
    if (!okay) return application_fail(error, QA_ERROR_FORMAT, "Native Q2 equipment public state is invalid");
    if (view.gun_index) {
        uint32_t index = engine->resource_base[QA_NATIVE_HOST_MODEL] + (uint32_t)view.gun_index;
        if (index >= engine->configstring_count || !engine->configstrings[index] || !engine->configstrings[index][0] ||
            !provider->launch->content)
            return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 gunindex has no retained MODEL configstring and content owner");
        view.view_model = engine->configstrings[index];
        view.view_content = provider->launch->content;
        view.visible = true;
    }
    if (!admitted(provider, actor))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 equipment changed its actual selected source owner");
    *out = view;
    return true;
}

bool qa_application_native_q2_equipment_read(qa_application *app, qa_actor_id actor,
    qa_application_native_q2_equipment_view *out, qa_error *error)
{
    if (!app || !app->session || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 equipment requires its actual application and output");
    return application_q2_guest_equipment_read(
        application_provider_for(app, actor, QA_ROLE_ARSENAL, ""), actor, out, error);
}

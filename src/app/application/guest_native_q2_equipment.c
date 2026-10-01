#include "guest_native_q2_private.h"
#include "guest_native_q2_attack.h"
#include "guest_native_q2_equipment.h"

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

static qa_vec3 vector(const uint8_t *bytes)
{
    return qa_v3(qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8));
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
    if (!application_native_q2_attack_weapon_read(engine, slot, actor, &view.item, error)) return false;
    qa_buffer player = {0};
    if (!qa_native_host_q2_player_state(provider->state.native.host, slot, &player, error)) return false;
    bool rerelease = engine->profile == QA_NATIVE_Q2_GAME_API2023;
    bool okay = player.size == (rerelease ? 296u : 184u);
    if (okay) {
        const uint8_t *bytes = player.data;
        view.view_kick_angles = vector(bytes + (rerelease ? 76u : 52u));
        view.gun_angles = vector(bytes + (rerelease ? 88u : 64u));
        view.gun_offset = vector(bytes + (rerelease ? 100u : 76u));
        view.gun_index = qa_load_i32le(bytes + (rerelease ? 112u : 88u));
        view.frame = qa_load_i32le(bytes + (rerelease ? 120u : 92u));
        if (rerelease) {
            view.skin = qa_load_i32le(bytes + 116);
            view.rate = qa_load_i32le(bytes + 124);
            view.has_skin = view.has_rate = true;
        }
        okay = qa_vec_finite(view.view_kick_angles) && qa_vec_finite(view.gun_angles) &&
            qa_vec_finite(view.gun_offset) && view.gun_index >= 0 &&
            (uint32_t)view.gun_index < engine->resource_limit[QA_NATIVE_HOST_MODEL];
    }
    qa_buffer_free(&player);
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

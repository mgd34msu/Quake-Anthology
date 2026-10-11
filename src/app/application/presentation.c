#include "guest_q3_private.h"
#include "guest_native_q2_private.h"
#include "native_q3_wire.h"
#include "native_q3_wire_state.h"
#include "qa/game_q3_source.h"
#include "qa/game_q3_clients.h"
#include "qa/game_q3_configstrings.h"
#include "qa/game_q1_bots.h"
#include "guest_q3_restart.h"
#include "qa/application_q3_client.h"
#include "qa/text.h"
#include "control_frame.h"

bool application_control_intermission(const qa_movement_state *state)
{
    switch (state->kind) {
    case QA_RULESET_Q2_CLASSIC: return state->data.q2.type == 4;
    case QA_RULESET_Q2_RERELEASE: return state->data.q2r.type == 6;
    case QA_RULESET_Q3:
        return state->data.q3.movement_type == 5 || state->data.q3.movement_type == 6;
    default: return false;
    }
}

bool application_source_intermission_read(application_provider *source,
    bool *out, qa_error *error)
{
    bool intermission = false;
    if (source->kind == APPLICATION_PROVIDER_Q1) {
        double time, exit_after;
        if (!qa_q1_bot_clock_read(source->state.q1, &time, &intermission, &exit_after, error)) return false;
    } else if (source->kind == APPLICATION_PROVIDER_Q2)
        intermission = qa_q2_players_in_intermission(source->state.q2);
    else if (source->kind == APPLICATION_PROVIDER_Q3) {
        qa_q3_source_match_state match;
        if (!qa_q3_source_match_state_read(source->state.q3, &match, error)) return false;
        intermission = match.intermission_time_ms != 0;
    } else if (source->kind == APPLICATION_PROVIDER_QC) {
        const qa_qc_definition *definition = qa_qc_program_resolved_globals(source->state.qc.program)->intermission_running;
        if (definition) {
            float value;
            if (definition->type != QA_QC_FLOAT)
                return application_fail(error, QA_ERROR_FORMAT, "Quake intermission lost its typed source declaration");
            if (!qa_qc_global_float(source->state.qc.instance, definition->offset, &value, error)) return false;
            intermission = value != 0;
        }
    }
    *out = intermission;
    return true;
}

static application_provider *selected(qa_application *app, uint32_t seat, qa_launch_role role)
{
    qa_actor_id actor;
    if (qa_application_player_actor(app, seat, &actor))
        return application_provider_for(app, actor, role, NULL);
    const qa_launch_choices *choices = qa_launch_snapshot_choices(qa_application_launch(app));
    const qa_launch_binding *binding = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_SEAT, .seat = seat}, role, NULL);
    if (!binding) binding = qa_launch_binding_for(choices,
        (qa_launch_scope){.kind = QA_SCOPE_WORLD}, role, NULL);
    if (!binding) return NULL;
    for (size_t i = 0; i < app->provider_count; ++i) {
        application_provider *provider = app->providers[i];
        if (provider->attached && !strcmp(provider->launch->selection.instance, binding->instance)) return provider;
    }
    return NULL;
}

static q3g_role *client_role(application_provider *provider, qa_qvm_role kind, uint32_t seat)
{
    struct application_q3_guest *engine = q3g_engine(provider);
    if (!engine) return NULL;
    for (q3g_role *role = engine->roles; role; role = role->next)
        if (role->kind == kind && role->seat == seat && role->ready && !role->retired) return role;
    return NULL;
}

static bool client_ready(q3g_role *role)
{
    if (application_q3_guest_role_loading(role->engine->provider, role->kind, role->seat))
        return false;
    if (role->native_client) {
        const qa_q3_host_client_services *client = &role->client_services;
        return client->gamestate != NULL &&
               client->gamestate(client->context) != NULL;
    }
    if (role->kind != QA_QVM_CGAME || role->local_client || role->initialized)
        return true;
    const qa_q3_host_client_services *client = &role->client_services;
    return client->gamestate != NULL &&
           client->gamestate(client->context) != NULL;
}

static bool local_snapshots(application_provider *provider,
    qa_application_network_q3_frame **owned_frame, qa_unified_frame_lease **storage, qa_error *error)
{
    qa_application *app = provider->application;
    bool native = provider->kind == APPLICATION_PROVIDER_Q3;
    struct application_q3_guest *engine = native ? NULL : q3g_engine(provider);
    uint32_t maximum = 64;
    int32_t time;
    if ((native && !qa_q3_source_max_clients(provider->state.q3, &maximum, error)) ||
        !application_q3_wire_time(provider, &time, error)) return false;
    uint8_t snapshot_bit = 0;
    bool local[64] = {0};
    if (!native) {
        if (!application_q3_guest_snapshot_bit(provider, &snapshot_bit, error)) return false;
        for (application_provider *receiver = app->live_providers; receiver; receiver = receiver->next_live) {
            struct application_q3_guest *owner = q3g_engine(receiver);
            if (!receiver->attached || !receiver->constructed || receiver->close_pending || !owner) continue;
            for (q3g_role *role = owner->roles; role; role = role->next)
                if (role->engine == owner && role->kind == QA_QVM_CGAME && role->host && role->ready &&
                    !role->retired && !role->source_cleared && role->local_client && !role->native_client &&
                    role->client_engine == engine && role->client_source == provider &&
                    role->source_owner == provider->owner && role->client < 64 &&
                    engine->seats[role->client] == role->seat) local[role->client] = true;
        }
    }
    for (uint32_t slot = 0; slot < maximum; ++slot) {
        application_native_q3_wire_publication publication = {0};
        q3g_client *client = native ? NULL : &engine->clients[slot];
        uint32_t parse_cursor = 0;
        if (native) {
            bool wanted;
            if (!application_native_q3_wire_local_publication(provider, slot,
                &publication, &wanted, error)) return false;
            if (!wanted) continue;
        } else {
            if (!local[slot] || !client->connected || !client->begun || client->pending_retirement) continue;
            publication = (application_native_q3_wire_publication){
                .reliable_sequence = client->reliable.sequence,
                .snapshot_bit = snapshot_bit, .snapshot_needed = true, .has_snapshot = client->has_snapshot};
            if (client->has_snapshot) {
                const qa_q3_snapshot *latest = &client->snapshots[
                    (uint32_t)client->snapshot_sequence & (QA_Q3_PACKET_BACKUP - 1)].value;
                if (!latest->valid || latest->message_number != client->snapshot_sequence)
                    return application_fail(error, QA_ERROR_FORMAT, "Local Q3 latest snapshot owner is invalid");
                publication.previous_time = latest->server_time;
                parse_cursor = (uint32_t)latest->parse_entities_number + (uint32_t)latest->entity_count;
            }
        }
        if (publication.has_snapshot && publication.previous_time == time) continue;
        if (!native && client->snapshot_sequence == INT32_MAX)
            return application_fail(error, QA_ERROR_FORMAT, "Local Q3 snapshot message sequence is exhausted");
        if (!native) publication.next_message = client->snapshot_sequence + 1;
        if (native && publication.gamestate_needed) {
            if (!*storage) *storage = application_control_storage_acquire(app, error);
            qa_q3_gamestate *gamestate = *storage ? qa_unified_frame_lease_alloc(*storage,
                1, sizeof(*gamestate), _Alignof(qa_q3_gamestate), error) : NULL;
            if (!gamestate) return false;
            qa_q3_gamestate_init(gamestate);
            gamestate->client_number = (int32_t)slot;
            gamestate->command_sequence = publication.reliable_sequence;
            bool ok = true;
            for (uint32_t index = 0; ok && index < QA_Q3_CONFIGSTRINGS; ++index) {
                const char *text;
                ok = qa_q3_configstring_read(provider->state.q3, index, &text, error) &&
                    qa_q3_configstring_set(gamestate, index, text, error);
            }
            if (ok) ok = application_q3_wire_host_baselines(provider, gamestate, error) &&
                application_native_q3_wire_gamestate(provider, slot, gamestate, error);
            if (!ok) return false;
        }
        if (!publication.snapshot_needed) continue;
        if (!*owned_frame) {
            if (!*storage) *storage = application_control_storage_acquire(app, error);
            *owned_frame = *storage ? qa_unified_frame_lease_alloc(*storage,
                1, sizeof(**owned_frame), _Alignof(qa_application_network_q3_frame), error) : NULL;
            if (!*owned_frame) return false;
        }
        qa_q3_native_client source = {0};
        if ((native && !qa_q3_client_slot_read(provider->state.q3, slot, &source, error)) ||
            !application_q3_wire_host_snapshot(provider, slot, publication.next_message,
                publication.reliable_sequence, publication.snapshot_bit, *owned_frame, error)) return false;
        if (native) {
            (*owned_frame)->snapshot.delta_number = publication.has_snapshot ? publication.next_message - 1 : -1;
            if (!application_native_q3_wire_snapshot(provider, slot, &(*owned_frame)->snapshot,
                                                      source.ping, error)) return false;
        } else {
            (*owned_frame)->snapshot.parse_entities_number = parse_cursor;
            if (!application_q3_guest_publish_snapshot(provider, slot, &(*owned_frame)->snapshot, 0, error)) return false;
        }
    }
    return true;
}

bool application_q3_publish_local_snapshots(qa_application *app, qa_error *error)
{
    if (!app || app->destroy_requested || app->state != QA_APPLICATION_RUNNING ||
        (app->operation != APPLICATION_IDLE && app->operation != APPLICATION_ADVANCING &&
         app->operation != APPLICATION_CONFIGURING) ||
        !qa_session_safe(app->session) || qa_session_faulted(app->session) ||
        !qa_world_idle(app->world))
        return application_fail(error, QA_ERROR_ARGUMENT,
                                "Local Q3 snapshots require the completed source frame");
    qa_application_network_q3_frame *frame = NULL;
    qa_unified_frame_lease *storage = NULL;
    bool ok = true;
    for (application_provider *provider = app->live_providers;
         provider && ok; provider = provider->next_live) {
        if (provider->kind == APPLICATION_PROVIDER_Q3 && provider->state.q3 &&
            provider->attached && provider->constructed && !provider->close_pending) {
            ok = local_snapshots(provider, &frame, &storage, error);
            continue;
        }
        struct application_q3_guest *engine = q3g_engine(provider);
        if (!provider->attached || !provider->constructed || !engine ||
            !engine->map_ready || !engine->game || !engine->game->initialized) continue;
        ok = local_snapshots(provider, &frame, &storage, error);
    }
    qa_unified_frame_lease_release(storage);
    return ok;
}

static bool native_q2_hud(application_provider *provider)
{
    return provider != NULL && provider->kind == APPLICATION_PROVIDER_NATIVE &&
           provider->state.native.q2_engine != NULL &&
           provider->state.native.q2_engine->profile == QA_NATIVE_Q2_CGAME_API2023;
}

bool qa_application_presentation_read(qa_application *app, uint32_t seat,
                                        qa_application_presentation_view *out)
{
    if (!app || !out || app->destroy_requested || !qa_application_launch(app)) return false;
    application_provider *hud = selected(app, seat, QA_ROLE_HUD);
    application_provider *menu = selected(app, seat, QA_ROLE_MENU);
    *out = (qa_application_presentation_view){
        .hud = hud ? hud->owner : 0, .menu = menu ? menu->owner : 0,
        .source_hud = client_role(hud, QA_QVM_CGAME, seat) != NULL || native_q2_hud(hud),
        .source_menu = client_role(menu, QA_QVM_UI, seat) != NULL,
        .source_world = client_role(hud, QA_QVM_CGAME, seat) != NULL
    };
    return true;
}

static bool ready(qa_application *app, qa_error *error)
{
    return (app && app->operation == APPLICATION_IDLE && !app->destroy_requested &&
        app->state != QA_APPLICATION_FAULTED && app->state != QA_APPLICATION_STOPPING &&
        qa_session_safe(app->session) && application_guests_idle(app)) ||
        application_fail(error, QA_ERROR_ARGUMENT, "Guest presentation requires an idle application");
}

static bool presentation_return(qa_application *app, bool ok, bool initialized,
    qa_error *error)
{
    app->operation = APPLICATION_IDLE;
    if (!ok && (!initialized || qa_session_faulted(app->session) ||
        !ready(app, NULL) || (app->world && !qa_world_idle(app->world))))
        application_fault(app, error);
    return ok;
}

static bool initialize(q3g_role *role, qa_error *error)
{
    if (role->initialized) return true;
    if (role->kind == QA_QVM_UI)
        return application_q3_guest_role_initialize(role->engine->provider, role->kind,
            role->seat, 0, 0, 0, false, error);
    int32_t message, time;
    const qa_q3_host_client_services *client = &role->client_services;
    if (!client->gamestate || !client->current_snapshot ||
        !client->current_snapshot(client->context, &message, &time, error))
        return application_fail(error, QA_ERROR_ARGUMENT, "Selected cgame has no admitted client services");
    const qa_q3_gamestate *state = client->gamestate(client->context);
    if (!state) return application_fail(error, QA_ERROR_ARGUMENT, "Selected cgame has no admitted gamestate");
    return application_q3_guest_role_initialize(role->engine->provider, role->kind,
        role->seat, message, state->command_sequence, state->client_number, false, error);
}

bool qa_application_guest_menu_set(qa_application *app, uint32_t seat,
    qa_application_guest_menu menu, bool *handled, qa_error *error)
{
    if (!handled || (unsigned)menu > QA_APPLICATION_GUEST_MENU_INGAME || !ready(app, error)) return false;
    *handled = false;
    app->operation = APPLICATION_ADVANCING;
    bool ok = true, initialized = true;
    int32_t command = menu == QA_APPLICATION_GUEST_MENU_MAIN ? 1 :
        menu == QA_APPLICATION_GUEST_MENU_INGAME ? 2 : 0;
    int32_t result;
    if (menu == QA_APPLICATION_GUEST_MENU_NONE) {
        for (application_provider *provider = app->live_providers; provider && ok; provider = provider->next_live) {
            struct application_q3_guest *engine = q3g_engine(provider);
            if (!engine) continue;
            for (q3g_role *role = engine->roles; role && ok; role = role->next) {
                if (role->kind != QA_QVM_UI || role->seat != seat || !role->initialized ||
                    role->retired || !client_ready(role)) continue;
                ok = q3g_call(role, 7, &command, 1, &result, error);
                *handled = true;
            }
        }
    } else {
        q3g_role *role = client_role(selected(app, seat, QA_ROLE_MENU), QA_QVM_UI, seat);
        if (role && client_ready(role)) {
            initialized = initialize(role, error);
            ok = initialized && q3g_call(role, 7, &command, 1, &result, error);
            *handled = true;
        }
    }
    return presentation_return(app, ok, initialized, error);
}

static bool source_time(q3g_role *role, uint32_t milliseconds, int32_t *out, qa_error *error)
{
    if (role->kind == QA_QVM_UI || !role->local_client) {
        *out = (int32_t)milliseconds;
        return true;
    }
    if (role->native_client)
        return application_native_q3_wire_client_time(role->native_client, out, error);
    qa_application_q3_client_context client;
    if (!qa_application_q3_client_context_read(role->engine->provider->application,
        role->engine->provider->owner, role->seat, &client, error)) return false;
    if (client.source_owner != role->source_owner || client.source_client != role->client ||
        client.service_owner != role->service_owner || client.native_source)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q3 draw time lost its retained original GAME client");
    *out = client.source_milliseconds;
    return true;
}

bool qa_application_present(qa_application *app, uint32_t seat,
                              uint32_t real_milliseconds,
                              uint32_t client_milliseconds, qa_error *error)
{
    if (!ready(app, error)) return false;
    application_provider *hud_provider = selected(app, seat, QA_ROLE_HUD);
    q3g_role *hud = client_role(hud_provider, QA_QVM_CGAME, seat);
    app->operation = APPLICATION_ADVANCING;
    bool ok = true, initialized = true;
    int32_t result;
    if (hud && client_ready(hud)) {
        int32_t args[] = {0, 0, 0};
        ok = source_time(hud, client_milliseconds, &args[0], error);
        if (ok) initialized = initialize(hud, error);
        if (ok) ok = initialized && q3g_call(hud, 3, args, 3, &result, error);
    }
    if (ok && native_q2_hud(hud_provider))
        ok = application_native_q2_draw_hud(hud_provider, seat, client_milliseconds, error);
    q3g_role *menu = ok ? client_role(selected(app, seat, QA_ROLE_MENU), QA_QVM_UI, seat) : NULL;
    if (ok && menu && client_ready(menu)) {
        int32_t time;
        ok = source_time(menu, real_milliseconds, &time, error);
        if (ok) initialized = initialize(menu, error);
        if (ok) ok = initialized && q3g_call(menu, 5, &time, 1, &result, error);
    }
    return presentation_return(app, ok, initialized, error);
}

static bool key(q3g_role *role, int32_t code, bool down, qa_error *error)
{
    if (role->kind == QA_QVM_CGAME && role->abi == QA_QVM_Q3_116N)
        return application_fail(error, QA_ERROR_UNSUPPORTED, "Legacy Q3 cgame has no input event exports");
    int32_t args[] = {code, down ? 1 : 0}, result;
    if (!q3g_call(role, role->kind == QA_QVM_UI ? 3 : 6, args, 2, &result, error)) return false;
    if (code >= 0 && code < 256) role->input_keys[code] = down;
    return true;
}

static int32_t physical_key(const qa_input_event *input)
{
    uint32_t code = input->input.code;
    if (input->input.kind == QA_PHYSICAL_MOUSE) {
        if (code < 1 || code > 255) return -1;
        unsigned button = qa_input_mouse_button(code);
        if (button < 1 || button > 5) return -1;
        code = QA_KEY_MOUSE1 + button - 1;
    } else if (input->input.kind != QA_PHYSICAL_KEY) return -1;
    return code <= INT_MAX ? qa_input_source_key((int)code, QA_RULESET_Q3) : -1;
}

static bool event(q3g_role *role, const qa_input_event *input, bool *handled, qa_error *error)
{
    if (input->kind == QA_INPUT_EVENT_KEY || input->kind == QA_INPUT_EVENT_BUTTON) {
        int32_t source = physical_key(input);
        if (source < 0) return true;
        *handled = true;
        return key(role, source, input->down, error);
    }
    if (input->kind == QA_INPUT_EVENT_TEXT && input->text) {
        qa_bytes text = {(const uint8_t *)input->text, strlen(input->text)};
        if (!qa_utf8_valid(text)) return application_fail(error, QA_ERROR_ARGUMENT, "Guest text input is not UTF-8");
        size_t cursor = 0;
        uint32_t scalar;
        while (qa_utf8_next(text, &cursor, &scalar))
            if (!key(role, (int32_t)scalar | QA_KEY_CHAR_FLAG, true, error)) return false;
        *handled = true;
    } else if (input->kind == QA_INPUT_EVENT_MOUSE) {
        if (!isfinite(input->delta.x) || !isfinite(input->delta.y) ||
            (double)input->delta.x < INT32_MIN || (double)input->delta.x > INT32_MAX ||
            (double)input->delta.y < INT32_MIN || (double)input->delta.y > INT32_MAX)
            return application_fail(error, QA_ERROR_ARGUMENT, "Guest mouse delta exceeds source integer range");
        int32_t args[] = {(int32_t)input->delta.x, (int32_t)input->delta.y}, result;
        *handled = true;
        return q3g_call(role, role->kind == QA_QVM_UI ? 4 : 7, args, 2, &result, error);
    } else if (input->kind == QA_INPUT_EVENT_WHEEL) {
        if (!isfinite(input->delta.y) || fabsf(input->delta.y) > UINT16_MAX)
            return application_fail(error, QA_ERROR_ARGUMENT, "Guest wheel delta exceeds source range");
        if (input->delta.y == 0.0f) return true;
        int32_t code = input->delta.y > 0 ? QA_KEY_WHEEL_UP : QA_KEY_WHEEL_DOWN;
        *handled = true;
        unsigned count = (unsigned)ceilf(fabsf(input->delta.y));
        for (unsigned i = 0; i < count; ++i)
            if (!key(role, code, true, error) || !key(role, code, false, error)) return false;
    }
    return true;
}

bool qa_application_guest_input(qa_application *app, uint32_t seat,
                                  const qa_input_event *input, bool *handled, qa_error *error)
{
    if (!handled || !input || !ready(app, error)) return false;
    *handled = false;
    q3g_role *roles[] = {
        client_role(selected(app, seat, QA_ROLE_MENU), QA_QVM_UI, seat),
        client_role(selected(app, seat, QA_ROLE_HUD), QA_QVM_CGAME, seat)
    };
    int32_t released = !input->down && (input->kind == QA_INPUT_EVENT_KEY ||
        input->kind == QA_INPUT_EVENT_BUTTON) ? physical_key(input) : -1;
    if (released >= 0 && released < 256) {
        for (application_provider *provider = app->live_providers; provider; provider = provider->next_live) {
            struct application_q3_guest *engine = q3g_engine(provider);
            if (!engine) continue;
            for (q3g_role *role = engine->roles; role; role = role->next) {
                if (role->kind == QA_QVM_GAME || (role->kind == QA_QVM_CGAME && role->abi == QA_QVM_Q3_116N) ||
                    role->seat != seat || !role->initialized ||
                    role->retired || !role->input_keys[released] || !client_ready(role)) continue;
                app->operation = APPLICATION_ADVANCING;
                bool ok = key(role, released, false, error);
                if (!presentation_return(app, ok, true, error)) return false;
                *handled = true;
            }
        }
        if (*handled) return true;
    }
    for (size_t i = 0; i < 2; ++i) {
        q3g_role *role = roles[i];
        qa_input_seat *source;
        uint64_t owner;
        if (!role || (role->kind == QA_QVM_CGAME && role->abi == QA_QVM_Q3_116N) || !client_ready(role) ||
            !qa_q3_host_source_input(role->host, &source, &owner)) continue;
        qa_input_focus focus = qa_input_seat_focus(source);
        uint32_t composed = qa_input_seat_catcher(source, 0);
        if (focus == QA_INPUT_CONSOLE || focus == QA_INPUT_CHAT || focus == QA_INPUT_UI ||
            (composed & (QA_INPUT_CATCH_CONSOLE | QA_INPUT_CATCH_CHAT)) ||
            (role->kind == QA_QVM_CGAME && (composed & QA_INPUT_CATCH_UI))) continue;
        uint32_t catcher = qa_input_seat_catcher(source, owner);
        if (!(catcher & (role->kind == QA_QVM_UI ? QA_INPUT_CATCH_UI : QA_INPUT_CATCH_GAME))) continue;
        app->operation = APPLICATION_ADVANCING;
        bool initialized = initialize(role, error);
        bool ok = initialized && event(role, input, handled, error);
        if (!presentation_return(app, ok, initialized, error)) return false;
        if (*handled) return true;
    }
    return true;
}

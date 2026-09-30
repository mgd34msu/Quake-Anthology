#include "guest_q3_private.h"
#include "qa/text.h"

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

bool qa_application_presentation_read(qa_application *app, uint32_t seat,
                                        qa_application_presentation_view *out)
{
    if (!app || !out || app->destroy_requested || !qa_application_launch(app)) return false;
    application_provider *hud = selected(app, seat, QA_ROLE_HUD);
    application_provider *menu = selected(app, seat, QA_ROLE_MENU);
    *out = (qa_application_presentation_view){
        .hud = hud ? hud->owner : 0, .menu = menu ? menu->owner : 0,
        .source_hud = client_role(hud, QA_QVM_CGAME, seat) != NULL,
        .source_menu = client_role(menu, QA_QVM_UI, seat) != NULL
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
    bool ok = true;
    int32_t command = menu == QA_APPLICATION_GUEST_MENU_MAIN ? 1 :
        menu == QA_APPLICATION_GUEST_MENU_INGAME ? 2 : 0;
    int32_t result;
    if (menu == QA_APPLICATION_GUEST_MENU_NONE) {
        for (application_provider *provider = app->live_providers; provider && ok; provider = provider->next_live) {
            struct application_q3_guest *engine = q3g_engine(provider);
            if (!engine) continue;
            for (q3g_role *role = engine->roles; role && ok; role = role->next) {
                if (role->kind != QA_QVM_UI || role->seat != seat || !role->initialized || role->retired) continue;
                ok = q3g_call(role, 7, &command, 1, &result, error);
                *handled = true;
            }
        }
    } else {
        q3g_role *role = client_role(selected(app, seat, QA_ROLE_MENU), QA_QVM_UI, seat);
        if (role) {
            ok = initialize(role, error) && q3g_call(role, 7, &command, 1, &result, error);
            *handled = true;
        }
    }
    app->operation = APPLICATION_IDLE;
    if (!ok) application_fault(app, error);
    return ok;
}

static int32_t source_time(q3g_role *role, uint32_t milliseconds)
{
    if (role->kind == QA_QVM_UI) return (int32_t)milliseconds;
    qa_clock_state clock;
    if (qa_session_clock(role->engine->provider->application->session,
                           role->engine->provider->owner, &clock))
        return (int32_t)(uint32_t)(clock.frame.time_ns / UINT64_C(1000000));
    return role->engine->milliseconds;
}

bool qa_application_present(qa_application *app, uint32_t seat,
                              uint32_t milliseconds, qa_error *error)
{
    if (!ready(app, error)) return false;
    q3g_role *hud = client_role(selected(app, seat, QA_ROLE_HUD), QA_QVM_CGAME, seat);
    q3g_role *menu = client_role(selected(app, seat, QA_ROLE_MENU), QA_QVM_UI, seat);
    app->operation = APPLICATION_ADVANCING;
    bool ok = true;
    int32_t result;
    if (hud) {
        int32_t args[] = {source_time(hud, milliseconds), 0, 0};
        ok = initialize(hud, error) && q3g_call(hud, 3, args, 3, &result, error);
    }
    if (ok && menu) {
        int32_t time = source_time(menu, milliseconds);
        ok = initialize(menu, error) && q3g_call(menu, 5, &time, 1, &result, error);
    }
    app->operation = APPLICATION_IDLE;
    if (!ok) application_fault(app, error);
    return ok;
}

static bool key(q3g_role *role, int32_t code, bool down, qa_error *error)
{
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
    return code <= INT_MAX ? qa_input_source_key((int)code, QA_CONSOLE_Q3) : -1;
}

static bool event(q3g_role *role, const qa_input_event *input, bool *handled, qa_error *error)
{
    if (!initialize(role, error)) return false;
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
        if (!input->delta.y) return true;
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
                if (role->kind == QA_QVM_GAME || role->seat != seat || !role->initialized ||
                    role->retired || !role->input_keys[released]) continue;
                app->operation = APPLICATION_ADVANCING;
                bool ok = key(role, released, false, error);
                app->operation = APPLICATION_IDLE;
                if (!ok) { application_fault(app, error); return false; }
                *handled = true;
            }
        }
        if (*handled) return true;
    }
    for (size_t i = 0; i < 2; ++i) {
        q3g_role *role = roles[i];
        qa_input_seat *source;
        uint64_t owner;
        if (!role || !qa_q3_host_source_input(role->host, &source, &owner)) continue;
        qa_input_focus focus = qa_input_seat_focus(source);
        uint32_t composed = qa_input_seat_catcher(source, 0);
        if (focus == QA_INPUT_CONSOLE || focus == QA_INPUT_CHAT || focus == QA_INPUT_UI ||
            (composed & (QA_INPUT_CATCH_CONSOLE | QA_INPUT_CATCH_CHAT)) ||
            (role->kind == QA_QVM_CGAME && (composed & QA_INPUT_CATCH_UI))) continue;
        uint32_t catcher = qa_input_seat_catcher(source, owner);
        if (!(catcher & (role->kind == QA_QVM_UI ? QA_INPUT_CATCH_UI : QA_INPUT_CATCH_GAME))) continue;
        app->operation = APPLICATION_ADVANCING;
        bool ok = event(role, input, handled, error);
        app->operation = APPLICATION_IDLE;
        if (!ok) { application_fault(app, error); return false; }
        if (*handled) return true;
    }
    return true;
}

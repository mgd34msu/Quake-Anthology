#include "internal.h"
#include <stdio.h>

static double now_ms(void *context) { frontend_seat *seat = context; return (double)seat->frontend->time_ns / 1000000.0; }
static bool connected(void *context) { frontend_seat *seat = context; return qa_application_player_actor(seat->frontend->application, seat->id, &seat->actor); }
static bool focus(void *context, qa_input_focus kind, bool team, qa_error *error)
{
    frontend_seat *seat = context;
    if (!qa_input_seat_set_focus(seat->input, kind, now_ms(seat), error)) return false;
    seat->chat_team = kind == QA_INPUT_CHAT && team;
    return true;
}
static bool clipboard(void *context, qa_buffer *out, qa_error *error)
{
    (void)context;
    char *text = SDL_GetClipboardText();
    if (!text) return frontend_fail(error, QA_ERROR_IO, "reading clipboard");
    size_t size = strlen(text);
    out->data = malloc(size ? size : 1);
    if (!out->data) { SDL_free(text); return frontend_fail(error, QA_ERROR_MEMORY, "retaining clipboard"); }
    memcpy(out->data, text, size); out->size = size;
    SDL_free(text); return true;
}
static const char *ui_clipboard(void *context)
{
    frontend_seat *seat = context;
    SDL_free(seat->clipboard); seat->clipboard = SDL_GetClipboardText();
    return seat->clipboard ? seat->clipboard : "";
}
static bool chat(void *context, const char *text, bool team, bool targeted, int32_t target, qa_error *error)
{
    frontend_seat *seat = context;
    if (targeted) return frontend_fail(error, QA_ERROR_UNSUPPORTED, "targeted chat requires a connected client route");
    (void)target;
    char message[1200];
    snprintf(message, sizeof(message), "%sPlayer %u: %s", team ? "[team] " : "", seat->id + 1, text);
    for (unsigned i = 0; i < seat->frontend->options.seats; ++i)
        if (!qa_hud_notify(seat->frontend->seats[i].hud, message, true,
            seat->frontend->time_ns, UINT64_C(6000000000), error)) return false;
    frontend_print(seat->frontend, message);
    return true;
}
static bool source_input(void *context, qa_input_seat *input, const qa_input_event *event, bool *consumed, qa_error *error)
{
    frontend_seat *seat = context; (void)input;
    return qa_application_guest_input(seat->frontend->application, seat->id, event, consumed, error);
}
bool frontend_menu_open(frontend_seat *seat, qa_ui_id menu, qa_error *error)
{
    bool handled;
    if (menu == FRONTEND_MODS) {
        if (!seat->mods && !qa_ui_mods_create(seat->ui, seat->frontend->application, FRONTEND_MODS, &seat->mods, error)) return false;
        if (!qa_ui_mods_cancel(seat->mods, error)) return false;
    }
    return qa_application_guest_menu_set(seat->frontend->application, seat->id,
            QA_APPLICATION_GUEST_MENU_NONE, &handled, error) &&
        qa_ui_open(seat->ui, menu, now_ms(seat), error);
}
bool frontend_game_menu(frontend_seat *seat, qa_error *error)
{
    bool handled = false;
    qa_application_guest_menu menu = qa_application_launch(seat->frontend->application)
        ? QA_APPLICATION_GUEST_MENU_INGAME : QA_APPLICATION_GUEST_MENU_MAIN;
    if (!qa_application_guest_menu_set(seat->frontend->application, seat->id, menu, &handled, error)) return false;
    return handled ? qa_ui_close_all(seat->ui, now_ms(seat), error) : frontend_menu_open(seat, FRONTEND_HOME, error);
}
static bool hud_data(void *context, const qa_hud_frame *frame, qa_hud_data *out, qa_error *error)
{
    frontend_seat *seat = context;
    qa_application_presentation_view source = {0};
    (void)qa_application_presentation_read(seat->frontend->application, seat->id, &source);
    out->source_vitals = source.source_hud;
    out->crosshair_visible = !source.source_hud;
    uint32_t total, killed;
    if (!source.source_hud && frame->show_scores &&
        qa_application_q1_monster_counts(seat->frontend->application, seat->id, &total, &killed)) {
        snprintf(seat->q1_monster_label, sizeof(seat->q1_monster_label), "Monsters: %u / %u", killed, total);
        seat->q1_monsters = (qa_hud_value){.label = seat->q1_monster_label, .value = killed, .maximum = total};
        out->bars = &seat->q1_monsters; out->bar_count = 1;
    }
    if (!source.source_hud && seat->q2_view_ready && qa_actor_id_equal(frame->actor, seat->q2_actor)) {
        out->source_vitals = true;
        out->vitals = seat->q2_vitals; out->vital_count = 3;
        out->timers = &seat->q2_timer; out->timer_count = seat->q2_timer.until_ns > frame->time_ns;
        out->scores = seat->q2_scores; out->score_count = seat->q2_score_count;
        if (seat->q2_help) { out->help_title = "Objectives"; out->help_lines = seat->q2_help_lines; out->help_count = 2; }
    }
    if (!frame->actor.registry) return true;
    return qa_application_weapon_read(seat->frontend->application, frame->actor, &out->selected_weapon, error);
}
static bool input_handler(void *context, qa_input_seat *input, qa_input_focus kind, const qa_input_event *event)
{
    frontend_seat *seat = context;
    qa_error error = {0}; bool handled = false;
    if (event->kind == QA_INPUT_EVENT_KEY && event->down && !event->repeat && event->input.code == QA_KEY_ESCAPE && kind == QA_INPUT_GAME) {
        if (!frontend_game_menu(seat, &error)) frontend_print(seat->frontend, error.message);
        return true;
    }
    (void)input;
    if (seat->wheel && kind == QA_INPUT_GAME) {
        if (!qa_hud_wheel_input(seat->wheel, seat->id, event, &handled, &error)) frontend_print(seat->frontend, error.message);
        if (handled) return true;
    }
    if (kind == QA_INPUT_CONSOLE || kind == QA_INPUT_CHAT) {
        if (!qa_seat_console_input(seat->console, event, kind, seat->chat_team, &handled, &error)) frontend_print(seat->frontend, error.message);
    }
    return handled;
}
static bool menu_action(void *context, uint32_t id, qa_ui_id control, const qa_ui_action *action, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    qa_frontend *frontend = seat->frontend;
    qa_ui_state state;
    if (!qa_ui_state_read(seat->ui, &state, error)) return false;
    if (state.menu == FRONTEND_SETTINGS) {
        qa_cvars *cvars = qa_application_cvars(frontend->application);
        if (control == 1 && (action->kind == QA_UI_SELECT || action->kind == QA_UI_ROW_ACTIVATE)) {
            seat->selected_setting = action->value.row;
            const qa_cvar_view *setting = qa_cvars_at(cvars, seat->selected_setting);
            snprintf(seat->setting_value, sizeof(seat->setting_value), "%s", setting ? setting->value : "");
        } else if (control == 2 && action->kind == QA_UI_CHANGE_TEXT) {
            snprintf(seat->setting_value, sizeof(seat->setting_value), "%s", action->value.text ? action->value.text : "");
        } else if ((control == 3 && action->kind == QA_UI_ACTIVATE) || (control == 2 && action->kind == QA_UI_SUBMIT)) {
            const qa_cvar_view *setting = qa_cvars_at(cvars, seat->selected_setting);
            if (!setting) return frontend_fail(error, QA_ERROR_ARGUMENT, "selected setting was removed");
            return qa_cvars_set_console(cvars, setting->name, seat->setting_value, error);
        }
        return true;
    }
    if (action->kind != QA_UI_ACTIVATE) return true;
    switch (control) {
    case 1: return frontend_menu_open(seat, FRONTEND_LIBRARY, error);
    case 2: return frontend_menu_open(seat, FRONTEND_MODS, error);
    case 3: return frontend_menu_open(seat, FRONTEND_SETTINGS, error);
    case 4: return frontend_menu_open(seat, FRONTEND_RANKINGS, error);
    case 5: return qa_ui_close_all(seat->ui, now_ms(seat), error);
    case 6: qa_application_request_stop(frontend->application); return true;
    case 7: return frontend_menu_open(seat, FRONTEND_ASSISTANCE, error);
    case 8: return frontend_menu_open(seat, FRONTEND_BINDINGS, error);
    default: return true;
    }
}
static qa_ui_control button(frontend_seat *seat, qa_ui_id id, const char *label, float y)
{
    return (qa_ui_control){.id = id, .kind = QA_UI_BUTTON, .label = label,
        .rect = {120, y, 400, 32}, .visible = true, .enabled = true,
        .context = seat, .action = menu_action};
}
static bool home(void *context, uint32_t id, qa_ui_menu *out, qa_error *error)
{
    frontend_seat *seat = context; (void)id; (void)error;
    const char *labels[] = {"Play a game", "Mods", "Settings", "Ranking account", "Resume", "Quit", "Assistance", "Controls"};
    for (size_t i = 0; i < 8; ++i) seat->controls[i] = button(seat, i + 1, labels[i], 78 + (float)i * 40);
    bool live = qa_application_launch(seat->frontend->application) != NULL;
    seat->controls[1].enabled = live; seat->controls[4].enabled = live;
    *out = (qa_ui_menu){.id = FRONTEND_HOME, .title = "Quake Anthology", .controls = seat->controls,
        .count = 8, .fullscreen = !live};
    return true;
}
static bool settings_open(void *context, uint32_t id, qa_error *error)
{
    frontend_seat *seat = context; (void)id; (void)error;
    const qa_cvar_view *setting = qa_cvars_at(qa_application_cvars(seat->frontend->application), seat->selected_setting);
    snprintf(seat->setting_value, sizeof(seat->setting_value), "%s", setting ? setting->value : "");
    return true;
}
static bool settings(void *context, uint32_t id, qa_ui_menu *out, qa_error *error)
{
    frontend_seat *seat = context; (void)id;
    qa_cvars *cvars = qa_application_cvars(seat->frontend->application);
    size_t count = qa_cvars_count(cvars);
    if (count > seat->settings_capacity) {
        if (count > SIZE_MAX / sizeof(*seat->settings_rows)) return frontend_fail(error, QA_ERROR_MEMORY, "settings list overflow");
        qa_ui_row *rows = realloc(seat->settings_rows, count * sizeof(*rows));
        if (!rows) return frontend_fail(error, QA_ERROR_MEMORY, "allocating settings list");
        seat->settings_rows = rows; seat->settings_capacity = count; ++seat->settings_revision;
    }
    for (size_t i = 0; i < count; ++i) {
        const qa_cvar_view *setting = qa_cvars_at(cvars, i);
        seat->settings_rows[i] = (qa_ui_row){.key = setting->name, .label = setting->name,
            .detail = setting->value, .enabled = true};
    }
    for (size_t i = 0; i < 4; ++i) seat->controls[i] = button(seat, i + 1, "", 100 + (float)i * 40);
    seat->controls[0].kind = QA_UI_LIST; seat->controls[0].rect = (qa_scene_rect_f){40, 88, 560, 240};
    seat->controls[0].value.list.rows = seat->settings_rows; seat->controls[0].value.list.count = count;
    seat->controls[0].value.list.selected = seat->selected_setting;
    seat->controls[0].value.list.row_height = 24; seat->controls[0].value.list.revision = seat->settings_revision;
    seat->controls[0].enabled = count != 0;
    const qa_cvar_view *selected = qa_cvars_at(cvars, seat->selected_setting);
    seat->controls[1].kind = QA_UI_FIELD; seat->controls[1].label = "Value";
    seat->controls[1].rect = (qa_scene_rect_f){40, 340, 420, 30};
    seat->controls[1].value.field.text = seat->setting_value; seat->controls[1].value.field.maximum = 255;
    seat->controls[2].label = "Apply"; seat->controls[2].rect = (qa_scene_rect_f){480, 340, 120, 30};
    seat->controls[3].label = selected && selected->description ? selected->description : "Select a setting to inspect or edit";
    seat->controls[3].rect = (qa_scene_rect_f){40, 392, 560, 60}; seat->controls[3].enabled = false;
    *out = (qa_ui_menu){.id = FRONTEND_SETTINGS, .title = "Engine settings", .controls = seat->controls, .count = 4, .fullscreen = true};
    return true;
}
bool frontend_seats_create(qa_frontend *frontend, qa_error *error)
{
    qa_cvars *cvars = qa_application_cvars(frontend->application);
    if (!qa_input_settings_register(cvars, QA_MOVEMENT_NETQUAKE, error) || !qa_input_device_settings_register(cvars, error)) return false;
    qa_launch_seat players[QA_INPUT_LOCAL_SEATS] = {0};
    char player_names[QA_INPUT_LOCAL_SEATS][32];
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        snprintf(player_names[i], sizeof(player_names[i]), "Player %u", i + 1);
        players[i] = (qa_launch_seat){.id = i, .name = player_names[i], .local = true, .input_device = i};
    }
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i]; seat->frontend = frontend; seat->id = i;
        qa_command_context command = {.seat = i, .origin = QA_COMMAND_SEAT, .dialect = QA_CONSOLE_Q1, .direct = true};
        qa_input_seat_options input = {.context = command, .console = qa_application_console(frontend->application),
            .cvars = cvars, .gamepad = qa_gamepad_defaults(), .ui = input_handler, .ui_user = seat,
            .before_ui = source_input, .before_ui_user = seat};
        seat->input = qa_input_seat_create(&input, error);
        if (!seat->input || !qa_input_default_bindings(seat->input, (int32_t)i, error)) return false;
        qa_seat_console_options console = {.command = command, .commands = input.console,
            .context = seat, .now_ms = now_ms, .connected = connected, .clipboard = clipboard, .focus = focus, .chat = chat};
        seat->console = qa_seat_console_create(&console, error);
        if (!seat->console || !qa_font_selection_init(&seat->fonts, i, frontend->classic, frontend->primary, NULL, 0, error)) return false;
        qa_ui_options ui = {.seat = i, .input = seat->input, .fonts = seat->fonts,
            .white = qa_scene_white(frontend->ui_images), .context = seat, .clipboard = ui_clipboard,
            .binding = frontend_binding_capture, .binding_cancel = frontend_binding_cancel};
        if (!qa_ui_create(&ui, &seat->ui, error) || !qa_ui_register(seat->ui,
            &(qa_ui_menu_registration){.id = FRONTEND_HOME, .context = seat, .factory = home}, error) ||
            !qa_ui_register(seat->ui, &(qa_ui_menu_registration){.id = FRONTEND_SETTINGS,
                .context = seat, .factory = settings, .open = settings_open}, error)) return false;
        if (!frontend_bindings_create(seat, error)) return false;
        if (!qa_ui_library_create(seat->ui, frontend->application, FRONTEND_LIBRARY,
                players, frontend->options.seats, &seat->library, error) ||
            !qa_ui_rankings_create(seat->ui, frontend->application, FRONTEND_RANKINGS, -1, &seat->rankings, error) ||
            !qa_hud_create(&(qa_hud_options){.ui = seat->ui, .application = frontend->application, .seat = i,
                .context = seat, .read = hud_data}, &seat->hud, error) || !frontend_wheel_create(seat, error)) return false;
    }
    return true;
}
bool frontend_seats_destroy(qa_frontend *frontend, qa_error *error)
{
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i];
        if (!qa_hud_wheel_destroy(seat->wheel, error)) return false;
        seat->wheel = NULL;
        if (!qa_hud_destroy(seat->hud, error)) return false;
        seat->hud = NULL;
        if (!qa_ui_mods_destroy(seat->mods, 0, error)) return false;
        seat->mods = NULL;
        if (!qa_ui_library_destroy(seat->library, 0, error)) return false;
        seat->library = NULL;
        if (!qa_ui_rankings_destroy(seat->rankings, 0, error)) return false;
        seat->rankings = NULL;
        if (!qa_ui_destroy(seat->ui, 0, error)) return false;
        seat->ui = NULL;
        qa_seat_console_destroy(seat->console); seat->console = NULL;
        qa_input_seat_destroy(seat->input); seat->input = NULL;
        frontend_player_retire(seat);
        frontend_bindings_destroy(seat);
        free(seat->settings_rows); seat->settings_rows = NULL;
        SDL_free(seat->clipboard); seat->clipboard = NULL;
        free(seat->wheel_items); free(seat->wheel_definitions); free(seat->wheel_labels);
        seat->wheel_items = NULL; seat->wheel_definitions = NULL; seat->wheel_labels = NULL;
    }
    return true;
}
void frontend_seats_rebind(qa_frontend *owned, qa_frontend *destination)
{
    for (unsigned i = 0; i < owned->options.seats; ++i) owned->seats[i].frontend = destination;
}

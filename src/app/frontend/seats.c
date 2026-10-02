#include "source_prompt.h"
#include "remote_q1_client.h"
#include "remote_q1_hud.h"
#include "startup_rotation.h"
#include "internal.h"
#include "startup_server_browser.h"
#include "qa/ui_menu_save.h"
#include "qa/ui_save.h"
#include "qa/binary.h"
#include "seat_save.h"
#include "source_restore.h"
#include "chat.h"
#include "accessibility.h"
#include "save_menu.h"
#include "menu_fonts.h"
#include "ui_features.h"
#include "campaign_menu.h"
#include "campaign_cinematic.h"
#include "network_recipient.h"
#include <stdio.h>

static double now_ms(void *context) { frontend_seat *seat = context; return (double)seat->frontend->time_ns / 1000000.0; }
static double input_now_ms(void *context) { frontend_seat *seat = context; return (double)seat->frontend->wall_time_ns / 1000000.0; }
static bool connected(void *context)
{
    frontend_seat *seat = context;
    uint32_t launch_seat;
    return frontend_network_remote(seat->frontend) ? seat->id == 0 && frontend_network_client_ready(seat->frontend) :
        frontend_seat_launch_id_read(seat->frontend,seat->id,&launch_seat) &&
        qa_application_player_actor(seat->frontend->application, launch_seat, &seat->actor);
}
static bool focus(void *context, qa_input_focus kind, bool team, qa_error *error)
{
    frontend_seat *seat = context;
    if (!qa_input_seat_set_focus(seat->input, kind, input_now_ms(seat), error)) return false;
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
    return frontend_chat_send(context, text, team, targeted, target, error);
}
static bool source_input(void *context, qa_input_seat *input, const qa_input_event *event, bool *consumed, qa_error *error)
{
    frontend_seat *seat = context;
    if (!frontend_cinematic_input(seat->frontend,seat->id,qa_input_seat_focus(input),event,consumed,error)) return false;
    if (*consumed) return true;
    if (!frontend_source_prompt_input(seat->source_prompt,event,consumed,error)) return false;
    if (*consumed) return true;
    uint32_t launch_seat;
    return !frontend_seat_launch_id_read(seat->frontend,seat->id,&launch_seat) ||
        qa_application_guest_input(seat->frontend->application, launch_seat, event, consumed, error);
}
bool frontend_menu_open(frontend_seat *seat, qa_ui_id menu, qa_error *error)
{
    bool handled; uint32_t launch_seat;
    if (menu == FRONTEND_MODS) {
        if (!seat->mods && !qa_ui_mods_create(seat->ui, seat->frontend->application, FRONTEND_MODS, &seat->mods, error)) return false;
        if (!qa_ui_mods_cancel(seat->mods, error)) return false;
    }
    return (!frontend_seat_launch_id_read(seat->frontend,seat->id,&launch_seat) ||
        qa_application_guest_menu_set(seat->frontend->application, launch_seat,
            QA_APPLICATION_GUEST_MENU_NONE, &handled, error)) &&
        qa_ui_open(seat->ui, menu, now_ms(seat), error);
}
bool frontend_game_menu(frontend_seat *seat, qa_error *error)
{
    bool handled = false;
    qa_application_guest_menu menu = qa_application_launch(seat->frontend->application)
        ? QA_APPLICATION_GUEST_MENU_INGAME : QA_APPLICATION_GUEST_MENU_MAIN;
    uint32_t launch_seat;
    if (frontend_seat_launch_id_read(seat->frontend,seat->id,&launch_seat) &&
        !qa_application_guest_menu_set(seat->frontend->application, launch_seat, menu, &handled, error)) return false;
    return handled ? qa_ui_close_all(seat->ui, now_ms(seat), error) : frontend_menu_open(seat, FRONTEND_HOME, error);
}
static bool hud_data(void *context, const qa_hud_frame *frame, qa_hud_data *out, qa_error *error)
{
    frontend_seat *seat = context;
    for (size_t i=0;i<frontend_remote_q1_count(seat->frontend);++i) {
        frontend_remote_q1 *row=frontend_remote_q1_at(seat->frontend,i);
        frontend_remote_q1_view received;
        if (!frontend_remote_q1_metadata_read(row,&received,error)) return false;
        if (!received.retired && received.bound && received.domain.physical_seat==seat->id)
            return frontend_remote_q1_hud_read(row,frame,out,error);
    }
    qa_application_presentation_view source = {0};
    qa_ui_preferences preferences;
    uint32_t launch_seat;
    bool published=frontend_seat_launch_id_read(seat->frontend,seat->id,&launch_seat);
    if (!qa_ui_preferences_read(qa_application_cvars(seat->frontend->application), seat->id, &preferences, error)) return false;
    if (published) (void)qa_application_presentation_read(seat->frontend->application, launch_seat, &source);
    out->source_vitals = source.source_hud;
    out->crosshair_visible = !source.source_hud && preferences.crosshair;
    out->crosshair_size = preferences.crosshair_size;
    out->crosshair_color = preferences.color_mode == QA_UI_COLOR_BLUE_YELLOW ?
        (qa_scene_vec4){1, .9f, .2f, 1} : (qa_scene_vec4){1, 1, 1, 1};
    if (!frontend_ui_features_captions(seat, &out->captions, &out->caption_count, error)) return false;
    uint32_t total, killed;
    if (published && !source.source_hud && frame->show_scores &&
        qa_application_q1_monster_counts(seat->frontend->application, launch_seat, &total, &killed)) {
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
    case 9: return frontend_menu_open(seat, FRONTEND_ACCESSIBILITY, error);
    case 10: return frontend_menu_open(seat, FRONTEND_SAVES, error);
    case 11: return frontend_menu_open(seat, FRONTEND_ARENA_PROGRESS, error);
    case 12: return frontend_startup_server_browser_open(seat->server_browser,error);
    case 13: return frontend_startup_rotation_open(seat->rotation_menu,error);
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
    const char *labels[] = {"Play a game", "Mods", "Settings", "Ranking account", "Resume", "Quit", "Assistance", "Controls", "Accessibility", "Save / load", "Arena progress", "Servers", "Map rotation"};
    for (size_t i = 0; i < 13; ++i) seat->controls[i] = button(seat, i + 1, labels[i], 64 + (float)i * 34);
    bool live = qa_application_launch(seat->frontend->application) != NULL;
    seat->controls[1].enabled = live; seat->controls[4].enabled = live;
    bool campaign=false;
    if (!frontend_campaign_menu_available(seat,&campaign,error)) return false;
    seat->controls[10].enabled=campaign;
    *out = (qa_ui_menu){.id = FRONTEND_HOME, .title = "Quake Anthology", .controls = seat->controls,
        .count = 13, .fullscreen = !live};
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
static bool seat_services_create(frontend_seat *seat, bool restoring, qa_error *error)
{
    qa_frontend *frontend = seat->frontend;
    unsigned i = seat->id;
    qa_cvars *cvars = qa_application_cvars(frontend->application);
    qa_command_context command = {.seat = i, .origin = QA_COMMAND_SEAT, .dialect = QA_CONSOLE_Q1, .direct = true};
    (void)frontend_seat_launch_id_read(frontend,i,&command.seat);
    qa_input_seat_options input = {.seat=i,.context = command, .console = qa_application_console(frontend->application),
        .cvars = cvars, .gamepad = qa_gamepad_defaults(), .ui = input_handler, .ui_user = seat,
        .before_ui = source_input, .before_ui_user = seat,
        .context_ready=frontend_seat_context_ready,.context_user=seat};
    seat->input = qa_input_seat_create(&input, error);
    if (!seat->input || (!restoring && !qa_input_default_bindings(seat->input, (int32_t)i, error))) return false;
    qa_seat_console_options console = {.seat=i,.command = command, .commands = input.console,
        .context = seat,.context_ready=frontend_seat_context_ready,
        .now_ms = input_now_ms, .connected = connected, .clipboard = clipboard, .focus = focus, .chat = chat};
    seat->console = qa_seat_console_create(&console, error);
    return seat->console != NULL;
}
bool frontend_seat_client_recipient_ready_is(const qa_frontend *f,uint32_t physical,
    const qa_application_client_source *source)
{
    if (!f || !source || !f->seats || physical>=f->options.seats ||
        source->context.physical_seat!=physical || !source->context.console || !source->context.cvars ||
        !qa_application_client_associated(f->application,source)) return false;
    const frontend_seat *seat=f->seats+physical;
    return seat->frontend==f && seat->id==physical &&
        qa_input_seat_recipient_ready_is(seat->input,source->context.console,source->context.cvars,&source->context.command) &&
        qa_seat_console_recipient_ready_is(seat->console,source->context.console,&source->context.command);
}
bool frontend_seat_client_recipient_ready(qa_frontend *f,uint32_t physical,
    const qa_application_client_source *source,qa_error *error)
{
    if (!frontend_seat_client_recipient_ready_is(f,physical,source) ||
        !qa_application_client_current(f->application,source))
        return frontend_fail(error,QA_ERROR_ARGUMENT,"CLIENT input recipient lost its returned physical source");
    frontend_seat *seat=f->seats+physical;
    return qa_input_seat_recipient_ready(seat->input,source->context.console,source->context.cvars,
        &source->context.command,error) && qa_seat_console_recipient_ready(seat->console,
        source->context.console,&source->context.command,error);
}
void frontend_seat_client_recipient_publish(qa_frontend *f,uint32_t physical,
    const qa_application_client_source *source)
{
    frontend_seat *seat=f->seats+physical;
    qa_input_seat_recipient_publish(seat->input,source->context.console,source->context.cvars,&source->context.command);
    qa_seat_console_recipient_publish(seat->console,source->context.console,&source->context.command);
}
bool frontend_seats_recipients_restore(qa_frontend *f,qa_error *error)
{
    if (!f || !f->source_restoring)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input recipient import requires the actual restoring frontend");
    if (f->options.dedicated) return !f->seats ||
        frontend_fail(error,QA_ERROR_FORMAT,"Dedicated input import has unexpected physical seats");
    if (!f->seats || !f->options.seats || f->options.seats>QA_INPUT_LOCAL_SEATS)
        return frontend_fail(error,QA_ERROR_FORMAT,"Input import lost its actual physical seat roster");
    frontend_network_client_recipient recipients[QA_INPUT_LOCAL_SEATS]={0};
    bool present[QA_INPUT_LOCAL_SEATS]={0};
    for (uint32_t i=0;i<f->options.seats;++i) {
        if (!frontend_network_client_recipient_read(f,i,recipients+i,present+i,error)) return false;
        if (present[i] && (!recipients[i].ready || !frontend_seat_client_recipient_ready(f,i,&recipients[i].source,error)))
            return frontend_fail(error,QA_ERROR_FORMAT,"Saved input recipient has no completed physical CLIENT");
    }
    for (uint32_t i=0;i<f->options.seats;++i)
        if (present[i]) frontend_seat_client_recipient_publish(f,i,&recipients[i].source);
    return true;
}
bool frontend_seat_engine_recipient_ready(qa_frontend *f,uint32_t physical,qa_command_context *out,qa_error *error)
{
    if (!f || !f->application || !f->seats || physical>=f->options.seats || !out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"ENGINE input handoff lost its actual physical seat");
    frontend_seat *seat=f->seats+physical;
    if (seat->frontend!=f || seat->id!=physical || !seat->input || !seat->console)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"ENGINE input handoff lost its installed physical services");
    qa_command_context previous=qa_input_seat_context(seat->input);
    qa_command_context command={.seat=physical,.origin=QA_COMMAND_SEAT,.dialect=previous.dialect,.direct=true};
    (void)frontend_seat_launch_id_read(f,physical,&command.seat);
    qa_console *console=qa_application_console(f->application); qa_cvars *cvars=qa_application_cvars(f->application);
    if (!qa_input_seat_recipient_ready(seat->input,console,cvars,&command,error) ||
        !qa_seat_console_recipient_ready(seat->console,console,&command,error)) return false;
    *out=command; return true;
}
void frontend_seat_engine_recipient_publish(qa_frontend *f,uint32_t physical,const qa_command_context *command)
{
    frontend_seat *seat=f->seats+physical;
    qa_console *console=qa_application_console(f->application); qa_cvars *cvars=qa_application_cvars(f->application);
    qa_input_seat_recipient_publish(seat->input,console,cvars,command);
    qa_seat_console_recipient_publish(seat->console,console,command);
}
bool frontend_seats_prepare_restored(qa_frontend *frontend, qa_error *error)
{
    if (!frontend || !frontend->application || !frontend->seats || frontend->options.dedicated ||
        !frontend->options.seats || frontend->options.seats > QA_INPUT_LOCAL_SEATS || frontend->stepping)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "restored seat services require an isolated application");
    for (unsigned i = 0; i < frontend->options.seats; ++i)
        if (frontend->seats[i].input || frontend->seats[i].console || frontend->seats[i].ui)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "restored seat service destination is occupied");
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i]; seat->frontend = frontend; seat->id = i;
        if (!seat_services_create(seat, true, error)) return false;
    }
    return true;
}
static bool seats_create(qa_frontend *frontend, const bool *mods, bool restoring, qa_error *error)
{
    if (!frontend || !frontend->application || !frontend->seats || !frontend->classic ||
        !frontend->primary || !frontend->ui_images || frontend->stepping ||
        (restoring && !mods))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "seat construction requires actual restored resource owners");
    for (unsigned i = 0; i < frontend->options.seats; ++i)
        if (frontend->seats[i].ui || (restoring ?
            (!frontend->seats[i].input || !frontend->seats[i].console || frontend->seats[i].frontend != frontend || frontend->seats[i].id != i) :
            (frontend->seats[i].input != NULL || frontend->seats[i].console != NULL)))
            return frontend_fail(error, QA_ERROR_ARGUMENT, "local seat service destination is not qualified");
    qa_cvars *cvars = qa_application_cvars(frontend->application);
    if (!restoring && (!qa_input_settings_register(cvars, QA_MOVEMENT_NETQUAKE, error) ||
        !qa_input_device_settings_register(cvars, error))) return false;
    qa_launch_seat players[QA_INPUT_LOCAL_SEATS] = {0};
    char player_names[QA_INPUT_LOCAL_SEATS][32];
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        snprintf(player_names[i], sizeof(player_names[i]), "Player %u", i + 1);
        players[i] = (qa_launch_seat){.id = i, .name = player_names[i], .local = true, .input_device = i};
    }
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i]; seat->frontend = frontend; seat->id = i;
        if (!restoring && !seat_services_create(seat, false, error)) return false;
        qa_ui_preferences preferences;
        if (!seat->console || !qa_ui_preferences_read(qa_application_cvars(frontend->application), i, &preferences, error) ||
            !frontend_menu_font_selection(frontend, i, preferences.typeface == QA_UI_TYPEFACE_BOLD, &seat->fonts, error)) return false;
        qa_ui_options ui = {.seat = i, .input = seat->input, .fonts = seat->fonts,
            .white = qa_scene_white(frontend->ui_images), .context = seat, .clipboard = ui_clipboard, .localize = frontend_ui_localize,
            .binding = frontend_binding_capture, .binding_cancel = frontend_binding_cancel,
            .input_now_ms=input_now_ms};
        if (!qa_ui_create(&ui, &seat->ui, error) || !qa_ui_register(seat->ui,
            &(qa_ui_menu_registration){.id = FRONTEND_HOME, .context = seat, .factory = home}, error) ||
            !qa_ui_register(seat->ui, &(qa_ui_menu_registration){.id = FRONTEND_SETTINGS,
                .context = seat, .factory = settings, .open = settings_open}, error)) return false;
        frontend_startup_server_browser_menus browser={200,201,202};
        if (!frontend_startup_server_browser_create(seat,&browser,&seat->server_browser,error) ||
            !frontend_startup_rotation_create(seat,205,&seat->rotation_menu,error) ||
            !frontend_source_prompt_create(seat,206,&seat->source_prompt,error)) return false;
        if (!frontend_bindings_create(seat, error) || !frontend_accessibility_create(seat, error) ||
            !frontend_save_menu_create(seat, error) || !frontend_campaign_menu_create(seat,error)) return false;
        bool library = restoring ? qa_ui_library_create_restored(seat->ui, frontend->application,
            FRONTEND_LIBRARY, &seat->library, error) :
            qa_ui_library_create(seat->ui, frontend->application, FRONTEND_LIBRARY,
                players, frontend->options.seats, &seat->library, error);
        if (!library ||
            !qa_ui_rankings_create(seat->ui, frontend->application, FRONTEND_RANKINGS, -1, &seat->rankings, error) ||
            !qa_hud_create(&(qa_hud_options){.ui = seat->ui, .application = frontend->application, .seat = i,
                .context = seat, .read = hud_data}, &seat->hud, error) || !frontend_wheel_create(seat, error)) return false;
        if (restoring && !qa_ui_llm_create(seat->ui, frontend_tools_llm(frontend),
            FRONTEND_ASSISTANCE, &seat->assistance, error)) return false;
        if (restoring && mods[i] && !qa_ui_mods_create_restored(seat->ui, frontend->application,
            FRONTEND_MODS, &seat->mods, error)) return false;
    }
    return true;
}
bool frontend_seats_create(qa_frontend *frontend, qa_error *error)
{ return seats_create(frontend, NULL, false, error); }
bool frontend_seats_create_restored(qa_frontend *frontend, const bool *mods, qa_error *error)
{ return seats_create(frontend, mods, true, error); }
bool frontend_seats_destroy(qa_frontend *frontend, qa_error *error)
{
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i];
        if (!frontend_source_prompt_destroy(&seat->source_prompt,error) ||
            !frontend_startup_rotation_destroy(&seat->rotation_menu,error) ||
            !frontend_startup_server_browser_destroy(&seat->server_browser,error)) return false;
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

static bool saved_seat_ready(const frontend_seat *seat)
{
    return seat && seat->frontend && seat->frontend->application && !seat->frontend->stepping &&
        !seat->frontend->options.dedicated && seat->frontend->seats &&
        seat->id<seat->frontend->options.seats && seat==seat->frontend->seats+seat->id &&
        seat->input && seat->console;
}
static bool local_context(const qa_command_context *command, const frontend_seat *seat)
{
    return command->origin==QA_COMMAND_SEAT && command->direct &&
        !command->session && !command->owner && !command->client && !command->registry && !command->generation &&
        !command->actor.registry && !command->actor.generation && !command->actor.slot &&
        !command->script && !command->console_text &&
        frontend_seat_context_ready((void *)seat,seat->id,command,NULL);
}
static bool same_recipient_command(const qa_command_context *a,const qa_command_context *b)
{
    return a && b && a->owner==b->owner && a->session==b->session && a->client==b->client &&
        a->seat==b->seat && a->origin==b->origin && a->dialect==b->dialect && a->registry==b->registry &&
        a->generation==b->generation && a->direct==b->direct && a->console_text==b->console_text &&
        !a->script && !b->script && qa_actor_id_equal(a->actor,b->actor);
}
static bool recipient_services(const frontend_seat *seat,const qa_console *console,const qa_cvars *cvars,
    const qa_command_context *command,bool *client)
{
    if (!seat || !seat->frontend || !console || !command || !client) return false;
    if (!command->owner) {
        *client=false;
        return console==qa_application_console(seat->frontend->application) &&
            (!cvars || cvars==qa_application_cvars(seat->frontend->application)) && local_context(command,seat);
    }
    frontend_network_client_recipient recipient; bool present;
    *client=true;
    return frontend_network_client_recipient_read(seat->frontend,seat->id,&recipient,&present,NULL) &&
        present && recipient.ready && console==recipient.source.context.console &&
        (!cvars || cvars==recipient.source.context.cvars) &&
        same_recipient_command(command,&recipient.source.context.command);
}
bool frontend_seat_ui_clock_ready(void *context,const qa_ui *ui,double (*clock)(void *),
    void *clock_context,qa_error *error)
{
    frontend_seat *seat=context;
    return (saved_seat_ready(seat) && ui==seat->ui && qa_ui_idle(ui) &&
        clock==input_now_ms && clock_context==seat) ||
        frontend_fail(error,QA_ERROR_FORMAT,"UI physical clock differs from its actual frontend seat factory");
}
static bool input_services_encode(void *context, const qa_input_seat_options *options,
    uint64_t *out, qa_error *error)
{
    frontend_seat *seat=context;
    bool client=false;
    if (!saved_seat_ready(seat) || !options || !out || options->seat!=seat->id ||
        !recipient_services(seat,options->console,options->cvars,&options->context,&client) ||
        options->context.script || options->context.console_text ||
        options->ui!=input_handler || options->ui_user!=seat ||
        options->before_ui!=source_input || options->before_ui_user!=seat ||
        options->context_ready!=frontend_seat_context_ready || options->context_user!=seat)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Saved input services differ from their actual frontend seat binding");
    *out=((uint64_t)seat->id+1)|(client?UINT64_C(256):0)|((uint64_t)options->context.dialect<<16); return true;
}
static bool input_services_decode(void *context, uint64_t key, qa_input_seat_options *out, qa_error *error)
{
    frontend_seat *seat=context;
    bool client=(key&UINT64_C(256))!=0;
    uint64_t dialect=key>>16;
    if (!saved_seat_ready(seat) || !out || dialect<QA_CONSOLE_Q1 || dialect>QA_CONSOLE_Q3 ||
        (key&UINT64_C(65535)&~UINT64_C(256))!=(uint64_t)seat->id+1)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved input service descriptor names another prepared seat");
    qa_command_context command={.seat=seat->id,.origin=QA_COMMAND_SEAT,.dialect=(qa_console_dialect)dialect,.direct=true};
    qa_console *console=qa_application_console(seat->frontend->application);
    qa_cvars *cvars=qa_application_cvars(seat->frontend->application);
    if (client) {
        frontend_network_client_recipient recipient; bool present;
        if (!frontend_network_client_recipient_read(seat->frontend,seat->id,&recipient,&present,error) ||
            !present || !recipient.ready || recipient.source.context.command.dialect!=(qa_console_dialect)dialect)
            return frontend_fail(error,QA_ERROR_FORMAT,"Saved input CLIENT recipient is absent or incomplete");
        command=recipient.source.context.command; console=recipient.source.context.console; cvars=recipient.source.context.cvars;
    }
    else (void)frontend_seat_launch_id_read(seat->frontend,seat->id,&command.seat);
    bool actual_client=false;
    if (!recipient_services(seat,console,cvars,&command,&actual_client) || actual_client!=client)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved input has no prepared current launch context");
    *out=(qa_input_seat_options){.seat=seat->id,.context=command,
        .console=console,.cvars=cvars,
        .gamepad=qa_gamepad_defaults(),.ui=input_handler,.ui_user=seat,.before_ui=source_input,.before_ui_user=seat,
        .context_ready=frontend_seat_context_ready,.context_user=seat};
    return true;
}
static bool input_ui_encode(void *context, qa_input_ui_handler handler, void *user, uint64_t *out, qa_error *error)
{
    frontend_seat *seat=context; qa_ui_input_binding binding;
    if (!saved_seat_ready(seat) || !out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input UI encoder lacks its actual stable seat");
    if (handler==input_handler && user==seat) { *out=1; return true; }
    if (seat->ui && qa_ui_input_binding_read(seat->ui,&binding) && binding.seat==seat->input &&
        handler==binding.handler && user==binding.context) { *out=2; return true; }
    return frontend_fail(error,QA_ERROR_FORMAT,"Input overlay is outside its real frontend controller owner");
}
static bool input_ui_decode(void *context, uint64_t key, qa_input_ui_handler *handler, void **user, qa_error *error)
{
    frontend_seat *seat=context; qa_ui_input_binding binding;
    if (!saved_seat_ready(seat) || !handler || !user)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input UI decoder lacks its actual stable seat");
    if (key==1) { *handler=input_handler; *user=seat; return true; }
    if (key==2 && seat->ui && qa_ui_input_binding_read(seat->ui,&binding) && binding.seat==seat->input && binding.handler) {
        *handler=binding.handler; *user=binding.context; return true;
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Saved input overlay lacks its prepared real controller binding");
}
static bool input_catcher_ready(void *context, uint64_t owner, qa_error *error)
{
    frontend_seat *seat=context;
    if (!saved_seat_ready(seat) || !owner)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Input catcher has no actual stable source seat");
    for (size_t i=0;i<frontend_source_group_count(seat->frontend);++i) {
        frontend_source_group_view group;
        if (!frontend_source_group_read(seat->frontend,i,&group))
            return frontend_fail(error,QA_ERROR_FORMAT,"Input catcher source group is incomplete");
        if (group.seat!=seat->id) continue;
        frontend_source_role_identity role;
        for (size_t j=0;frontend_source_group_role_read(seat->frontend,i,j,&role);++j)
            if (role.service_owner==owner) return true;
    }
    return frontend_fail(error,QA_ERROR_FORMAT,"Input catcher names no actual source role lease for this seat");
}
qa_input_checkpoint_refs frontend_seat_input_refs(frontend_seat *seat)
{
    return (qa_input_checkpoint_refs){seat,input_services_encode,input_services_decode,
        input_ui_encode,input_ui_decode,input_catcher_ready};
}
static bool console_services(const frontend_seat *seat, const qa_seat_console_options *options)
{
    bool client=false;
    return saved_seat_ready(seat) && options && options->seat==seat->id &&
        recipient_services(seat,options->commands,NULL,&options->command,&client) && options->context==seat &&
        options->context_ready==frontend_seat_context_ready &&
        options->now_ms==input_now_ms && options->connected==connected && options->clipboard==clipboard &&
        options->focus==focus && options->chat==chat;
}
static bool seat_console_encode(void *context, const qa_seat_console_options *options, qa_buffer *out, qa_error *error)
{
    frontend_seat *seat=context;
    bool client=false;
    if (!console_services(seat,options) ||
        !recipient_services(seat,options->commands,NULL,&options->command,&client) || !out || out->data || out->size)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"Seat console capture lacks its actual installed callbacks");
    uint8_t *data=malloc(12);
    if (!data) return frontend_fail(error,QA_ERROR_MEMORY,"Retaining seat console service descriptor");
    memcpy(data,"QFSC",4); qa_store_u32le(data+4,seat->id);
    qa_store_u32le(data+8,client?1u:0u);
    *out=(qa_buffer){data,12}; return true;
}
static bool seat_console_decode(void *context, const qa_seat_console_options *candidate, qa_bytes bytes,
    qa_command_context *command, qa_error *error)
{
    frontend_seat *seat=context;
    if (!console_services(seat,candidate) || !command || !bytes.data || bytes.size!=12 ||
        memcmp(bytes.data,"QFSC",4) || qa_load_u32le(bytes.data+4)!=seat->id || qa_load_u32le(bytes.data+8)>1)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved console descriptor differs from its prepared actual seat");
    bool client=false;
    if (!recipient_services(seat,candidate->commands,NULL,&candidate->command,&client) ||
        client!=(qa_load_u32le(bytes.data+8)!=0) || command->script)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved console command names another physical recipient");
    if (client) {
        if (command->dialect!=candidate->command.dialect || command->origin!=candidate->command.origin ||
            command->seat!=candidate->command.seat || command->direct!=candidate->command.direct ||
            command->console_text!=candidate->command.console_text)
            return frontend_fail(error,QA_ERROR_FORMAT,"Saved CLIENT console command differs from its actual namespace");
        *command=candidate->command;
    } else if (!local_context(command,seat))
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved ENGINE console template belongs to another physical seat");
    return true;
}
qa_seat_console_save_resolvers frontend_seat_console_refs(frontend_seat *seat)
{ return (qa_seat_console_save_resolvers){seat,seat_console_encode,seat_console_decode}; }
bool frontend_seat_hud_options(frontend_seat *seat, qa_hud_options *out, qa_error *error)
{
    if (!saved_seat_ready(seat) || !seat->ui || !out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"HUD restore lacks its actual prepared frontend seat");
    *out=(qa_hud_options){.ui=seat->ui,.application=seat->frontend->application,
        .seat=seat->id,.context=seat,.read=hud_data};
    return true;
}

#include "source_prompt.h"
#include "remote_q1_client.h"
#include "remote_q1_hud.h"
#include "startup_rotation.h"
#include "internal.h"
#include "startup_server_browser.h"
#include "startup_downloads.h"
#include "startup_menus.h"
#include "qa/ui_save.h"
#include "seat_save.h"
#include "source_restore.h"
#include "chat.h"
#include "accessibility.h"
#include "save_menu.h"
#include "menu_fonts.h"
#include "settings_menu.h"
#include "startup_menus.h"
#include "qa/ui_library.h"
#include "ui_features.h"
#include "campaign_menu.h"
#include "campaign_cinematic.h"
#include "network_recipient.h"
#include "equipment_media.h"
#include "view_settings.h"
#include "config_store.h"
#include "qa/application_network.h"
#include "qa/game_q1_ui.h"
#include "material_movies.h"
#include "qc_messages.h"
#include <stdio.h>

static double now_ms(void *context) { frontend_seat *seat = context; return (double)seat->frontend->time_ns / 1000000.0; }
static double input_now_ms(void *context) { frontend_seat *seat = context; return (double)seat->frontend->wall_time_ns / 1000000.0; }
static const qa_scene_image *hud_video_frame(void *context, uint64_t initial, double seconds, qa_error *error)
{
    frontend_seat *seat = context;
    return frontend_material_movies_frontend_resolve(seat->frontend, initial, seconds, error);
}
static bool connected(void *context)
{
    frontend_seat *seat = context;
    frontend_network_client_recipient recipient;
    bool present=false;
    if (!frontend_network_client_recipient_read(seat->frontend,seat->id,&recipient,&present,NULL)) return false;
    if (present) {
        const qa_net_client *client=qa_net_connections_get(qa_network_connections(recipient.source.runtime),
            recipient.source.client);
        return recipient.ready && client && client->phase==QA_NET_ACTIVE &&
            frontend_network_client_recipient_current(seat->frontend,seat->id,&recipient);
    }
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
    if (event->kind == QA_INPUT_EVENT_KEY &&
        (event->input.code == '`' || event->input.code == '~')) {
        *consumed = true;
        return !event->down ||
            ((event->repeat || qa_ui_close_all(seat->ui, now_ms(seat), error)) &&
             qa_seat_console_toggle(seat->console, true, event->repeat, error));
    }
    if (!frontend_cinematic_input(seat->frontend,seat->id,qa_input_seat_focus(input),event,consumed,error)) return false;
    if (*consumed) return true;
    if (seat->library && qa_input_seat_focus(input) == QA_INPUT_UI &&
        !qa_ui_library_input(seat->library, event, consumed, error)) return false;
    if (*consumed) return true;
    if (!frontend_source_prompt_input(seat->source_prompt,event,consumed,error)) return false;
    if (*consumed) return true;
    uint32_t launch_seat;
    return !frontend_seat_launch_id_read(seat->frontend,seat->id,&launch_seat) ||
        qa_application_guest_input(seat->frontend->application, launch_seat, event, consumed, error);
}
static bool player_sources(void *,uint32_t,qa_ui_menu *,qa_error *);
static bool player_sources_register(frontend_seat *seat,qa_error *error)
{
    if (seat->player_sources_registered) return true;
    qa_ui_menu_registration registration={.id=FRONTEND_PLAYER_SOURCES,.context=seat,.factory=player_sources};
    bool ok=qa_ui_register(seat->ui,&registration,error);
    if (ok) seat->player_sources_registered=true;
    return ok;
}
bool frontend_menu_open(frontend_seat *seat, qa_ui_id menu, qa_error *error)
{
    bool handled; uint32_t launch_seat;
    if (menu == FRONTEND_MODS) {
        if (!seat->mods && !qa_ui_mods_create(seat->ui, seat->frontend->application, FRONTEND_MODS, &seat->mods, error)) return false;
        if (!qa_ui_mods_cancel(seat->mods, error)) return false;
    }
    if (menu==FRONTEND_PLAYER_SOURCES && !player_sources_register(seat,error)) return false;
    qa_application_presentation_view source;
    if (frontend_seat_launch_id_read(seat->frontend,seat->id,&launch_seat) &&
        !(qa_application_presentation_read(seat->frontend->application,launch_seat,&source) && !source.source_menu) &&
        !qa_application_guest_menu_set(seat->frontend->application,launch_seat,
            QA_APPLICATION_GUEST_MENU_NONE,&handled,error)) return false;
    return qa_ui_open(seat->ui,menu,now_ms(seat),error);
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
static bool hud_presentation(void *context, qa_ui_presentation *out, qa_error *error)
{
    frontend_seat *seat = context;
    qa_ui_preferences preferences;
    if (!qa_ui_preferences_read(qa_application_cvars(seat->frontend->application), seat->id,
            &preferences, error) || !frontend_menu_font_selection(seat->frontend, seat->id,
            preferences.typeface == QA_UI_TYPEFACE_BOLD, &out->fonts, error)) return false;
    if (preferences.typeface != QA_UI_TYPEFACE_BOLD) out->fonts.primary = NULL;
    out->text_scale = preferences.text_scale; out->color_mode = preferences.color_mode;
    return true;
}
static bool hud_weapon_data(frontend_seat *seat, const qa_hud_frame *frame, qa_hud_data *out,
    bool native_status, bool aggregate, bool *source_slot, qa_error *error)
{
    *source_slot = false;
    if (!frame->actor.registry) return true;
    qa_application_equipment_view equipment = {0};
    if (!qa_application_equipment_source_read(seat->frontend->application, frame->actor, &equipment, source_slot, error)) return false;
    if (!*source_slot) {
        uint32_t logical; qa_actor_id local;
        if (!frontend_seat_launch_id_read(seat->frontend, seat->id, &logical) ||
            !qa_application_player_actor(seat->frontend->application, logical, &local) ||
            !qa_actor_id_equal(local, frame->actor)) return true;
        if (!qa_application_equipment_read(seat->frontend->application, frame->actor, &equipment, error)) return false;
    }
    out->selected_weapon = equipment.item;
    if (!equipment.has_weapon_status || frame->source_status_native || qa_input_seat_focus(seat->input) != QA_INPUT_GAME) return true;
    const qa_material *picture = NULL;
    if (*source_slot ? !frontend_equipment_media_source_icon_read(seat->frontend, &equipment, &picture, error) :
        !frontend_equipment_media_native_icon_read(seat->frontend, &equipment, &picture, error)) return false;
    qa_bytes provider = qa_strings_text(qa_session_strings(qa_application_session(seat->frontend->application)), equipment.provider);
    out->weapon = (qa_hud_weapon){.present = true, .label = equipment.label, .icon = picture,
        .ammo_count = equipment.ammo_count, .finite_ammo = equipment.finite_ammo,
        .has_ammo_to_start = equipment.has_ammo_to_start, .low_ammo = equipment.low_ammo,
        .native_status = native_status || frame->weapon_only,
        .suppress_active_warning = provider.size >= 3 && !memcmp(provider.data, "q3:", 3),
        .aggregate_low = aggregate && equipment.warning == QA_APPLICATION_AMMO_LOW,
        .aggregate_empty = aggregate && equipment.warning == QA_APPLICATION_AMMO_EMPTY};
    return true;
}
static bool hud_data(void *context, const qa_hud_frame *frame, qa_hud_data *out, qa_error *error)
{
    frontend_seat *seat = context;
    for (size_t i=0;i<frontend_remote_q1_count(seat->frontend);++i) {
        frontend_remote_q1 *row=frontend_remote_q1_at(seat->frontend,i);
        frontend_remote_q1_view received;
        if (!frontend_remote_q1_metadata_read(row,&received,error)) return false;
        if (!received.retired && received.bound && received.domain.physical_seat==seat->id) {
            bool source_slot = false;
            return frontend_remote_q1_hud_read(row,frame,out,error) &&
                hud_weapon_data(seat,frame,out,true,false,&source_slot,error);
        }
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
    if (seat->frontend->qc_messages && !frame->source_status_native) {
        qa_application_qc_client_presentation qc; bool found=false;
        if (!frontend_qc_messages_client_vitals(seat->frontend->qc_messages,frame->actor,&qc,&found,error)) return false;
        if (found) {
            out->source_values[0]=(qa_hud_value){.label="Health",.value=qc.health,.warning=qc.health<=25};
            out->source_values[1]=(qa_hud_value){.label="Armor",.value=qc.armor};
            out->vitals=out->source_values; out->vital_count=2; out->source_vitals=true;
        }
    }
    if (seat->q1_view_ready && qa_actor_id_equal(frame->actor,seat->q1_view_actor)) {
        frontend_config_legacy_view legacy; bool present;
        qa_q1_clientdata client; qa_actor_owner provider;
        if (!frontend_config_store_primary_legacy_read(seat->frontend->config_store,launch_seat,&legacy,&present,error)) return false;
        if (!present || legacy.product->family!=QA_GAME_Q1 ||
            !qa_application_provider_owner(seat->frontend->application,legacy.descriptor->selection.instance,&provider))
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Q1 face lost its actual primary CLIENT source");
        if (!qa_application_network_q1_clientdata(seat->frontend->application,frame->actor,&client,error) ||
            !frontend_equipment_media_q1_face_read(seat->frontend,provider,
                frontend_view_q1_face(client.health,client.items,seat->q1_view_motion.seconds,&seat->q1_view_motion),
                &out->health_icon,error)) return false;
        if (!strcmp(legacy.product->campaign,"rogue")) {
            const qa_cvar_view *teamplay=qa_cvars_find(legacy.registry,"teamplay");
            uint32_t clients,entities;
            if (!qa_application_network_q1_extents(seat->frontend->application,provider,&clients,&entities,error)) return false;
            if (teamplay && qa_q1_rogue_team_face_active(clients,teamplay->number)) {
                qa_application_network_q1_status_player players[255]; size_t count=0;
                if (!qa_application_network_q1_status(seat->frontend->application,provider,players,&count,error)) return false;
                for (size_t i=0;i<count;++i) if (qa_actor_id_equal(players[i].actor,frame->actor)) {
                    if (!frontend_equipment_media_q1_team_face_read(seat->frontend,provider,players[i].colors,
                        players[i].frags,&out->health_team_face,error)) return false;
                    break;
                }
            }
        }
    }
    bool source_slot = false;
    if (!hud_weapon_data(seat,frame,out,source.source_hud || out->source_vitals,
        !source.source_hud,&source_slot,error)) return false;
    if (source_slot) return true;
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
    if (state.menu == FRONTEND_PLAYER_SOURCES) {
        if (action->kind!=QA_UI_SELECT) return true;
        if (frontend->player_source_draft || qa_application_startup_pending(frontend->application))
            return true;
        if (!control || control>QA_INPUT_LOCAL_SEATS*3 || action->value.row>=seat->player_source_count)
            return frontend_fail(error,QA_ERROR_ARGUMENT,"Player source choice is no longer available");
        static const qa_launch_role roles[]={QA_ROLE_MOVEMENT,QA_ROLE_CHARACTER,QA_ROLE_ARSENAL};
        uint32_t physical=(uint32_t)((control-1)/3);
        const qa_product *product=qa_catalog_product(qa_application_catalog(frontend->application),
            seat->player_source_products[action->value.row]);
        return frontend_player_source_select(frontend,physical,roles[(control-1)%3],product,error);
    }
    if (state.menu == FRONTEND_OPTIONS) {
        if (action->kind != QA_UI_ACTIVATE) return true;
        switch (control) {
        case 1: return frontend_menu_open(seat, FRONTEND_DISPLAY, error);
        case 2: return frontend_menu_open(seat, FRONTEND_SOUND, error);
        case 3: return frontend_menu_open(seat, FRONTEND_CONTROLS, error);
        case 4: return frontend_menu_open(seat, FRONTEND_ACCESSIBILITY, error);
        case 5: return frontend_menu_open(seat, FRONTEND_ALL_OPTIONS, error);
        case 6: return frontend_menu_open(seat, FRONTEND_ASSISTANCE, error);
        case 7: return qa_ui_close(seat->ui, now_ms(seat), error);
        default: return true;
        }
    }
    if(state.menu==FRONTEND_HOME &&
        (qa_application_launch(frontend->application) || frontend_network_remote(frontend))) {
        if(action->kind!=QA_UI_ACTIVATE) return true;
        switch(control) {
        case 16: return qa_ui_close_all(seat->ui,now_ms(seat),error);
        case 17: return frontend_menu_open(seat,FRONTEND_SAVE,error);
        case 10: return frontend_menu_open(seat,FRONTEND_LOAD,error);
        case 18: return frontend_menu_open(seat,FRONTEND_ALL_OPTIONS,error);
        case 19: return qa_ui_close_all(seat->ui,now_ms(seat),error) &&
            qa_seat_console_toggle(seat->console,false,false,error);
        case 11: return frontend_menu_open(seat,FRONTEND_ARENA_PROGRESS,error);
        case 20: return frontend_startup_end_stage(seat,error);
        case 21: return frontend_menu_open(seat,FRONTEND_MATCH,error);
        default: return true;
        }
    }
    if (action->kind != QA_UI_ACTIVATE) return true;
    switch (control) {
    case 1: return frontend_menu_open(seat, FRONTEND_LIBRARY, error);
    case 2: return frontend_menu_open(seat, FRONTEND_MODS, error);
    case 3: return frontend_menu_open(seat, FRONTEND_OPTIONS, error);
    case 4: return frontend_menu_open(seat, FRONTEND_RANKINGS, error);
    case 5: return qa_ui_close_all(seat->ui, now_ms(seat), error);
    case 6: qa_application_request_stop(frontend->application); return true;
    case 7: return frontend_menu_open(seat, FRONTEND_ASSISTANCE, error);
    case 8: return frontend_menu_open(seat, FRONTEND_BINDINGS, error);
    case 9: return frontend_menu_open(seat, FRONTEND_ACCESSIBILITY, error);
    case 10: return frontend_menu_open(seat, FRONTEND_LOAD, error);
    case 11: return frontend_menu_open(seat, FRONTEND_ARENA_PROGRESS, error);
    case 12: return frontend_startup_server_browser_open(seat->server_browser,error);
    case 13: return frontend_startup_rotation_open(seat->rotation_menu,error);
    case 14: return frontend_menu_open(seat, FRONTEND_CONTENT_LIBRARY, error);
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
    frontend_seat *seat = context; (void)id;
    bool live=qa_application_launch(seat->frontend->application)!=NULL || frontend_network_remote(seat->frontend);
    if(live) {
        static const char *const labels[]={"Resume game","Save game","Load game","Options","Console"};
        static const qa_ui_id controls[]={16,17,10,18,19};
        size_t count=0;
        for(size_t i=0;i<5;++i) {
            seat->controls[count]=button(seat,controls[i],labels[i],92+(float)(i+1)*28);
            seat->controls[count++].rect=(qa_scene_rect_f){64,92+(float)(i+1)*28,512,28};
        }
        bool arena=false;
        if(!frontend_campaign_menu_available(seat,&arena,error)) return false;
        if(arena) {
            seat->controls[count]=button(seat,11,"Arena progress",260);
            seat->controls[count++].rect=(qa_scene_rect_f){64,260,512,28};
        }
        if(qa_input_seat_context(seat->input).dialect==QA_CONSOLE_Q3) {
            seat->controls[count]=button(seat,21,"Match controls",288);
            seat->controls[count].rect=(qa_scene_rect_f){64,288,512,28};
            ++count;
        }
        seat->controls[count]=button(seat,20,"End game",316);
        seat->controls[count++].rect=(qa_scene_rect_f){64,316,512,28};
        *out=(qa_ui_menu){.id=FRONTEND_HOME,.title="Paused",.controls=seat->controls,
            .count=count,.fullscreen=true}; return true;
    }
    const char *labels[] = {"Play a game", "Load Game", "Options", "Library", "Quit"};
    const qa_ui_id controls[] = {1, 10, 3, 14, 6};
    for (size_t i = 0; i < 5; ++i) {
        seat->controls[i] = button(seat, controls[i], labels[i], 118 + (float)i * 34);
        seat->controls[i].rect.x = 64;
        seat->controls[i].rect.width = 224;
        seat->controls[i].rect.height = 30;
    }
    *out = (qa_ui_menu){.id = FRONTEND_HOME, .title = "QUAKE", .controls = seat->controls,
        .count = 5, .fullscreen = true, .narrow = true};
    return true;
}
static bool settings_open(void *context, uint32_t id, qa_error *error)
{ (void)context; (void)id; (void)error; return true; }
static bool settings(void *context, uint32_t id, qa_ui_menu *out, qa_error *error)
{
    frontend_seat *seat = context; (void)id; (void)error;
    const char *labels[] = {"Display", "Sound", "Controls", "Accessibility", "All options", "LLM options", "Back"};
    size_t count = frontend_tools_llm(seat->frontend) ? 6 : 5;
    for (size_t i = 0; i < count; ++i) {
        seat->controls[i] = button(seat, i + 1, labels[i], 118 + (float)i * 34);
        seat->controls[i].rect = (qa_scene_rect_f){64, 118 + (float)i * 34, 512, 30};
    }
    seat->controls[count] = button(seat, 7, "Back", 424);
    seat->controls[count].rect = (qa_scene_rect_f){64, 424, 512, 30};
    *out = (qa_ui_menu){.id = FRONTEND_OPTIONS, .title = "Options", .controls = seat->controls, .count = count + 1};
    return true;
}
static bool player_sources(void *context,uint32_t id,qa_ui_menu *out,qa_error *error)
{
    frontend_seat *seat=context; (void)id;
    qa_frontend *f=seat->frontend;
    const qa_launch_snapshot *publication=qa_application_launch(f->application);
    const qa_launch_choices *choices=qa_launch_snapshot_choices(publication);
    qa_catalog *catalog=qa_application_catalog(f->application);
    size_t capacity=qa_catalog_count(catalog);
    if (capacity>seat->player_source_capacity) {
        if (capacity>SIZE_MAX/sizeof(*seat->player_source_titles) ||
            capacity>SIZE_MAX/sizeof(*seat->player_source_products))
            return frontend_fail(error,QA_ERROR_MEMORY,"Player source list is too large");
        const char **titles=malloc(capacity*sizeof(*titles));
        qa_product_id *products=malloc(capacity*sizeof(*products));
        if (!titles || !products) { free(titles); free(products);
            return frontend_fail(error,QA_ERROR_MEMORY,"Allocating player source choices"); }
        free(seat->player_source_titles); free(seat->player_source_products);
        seat->player_source_titles=titles; seat->player_source_products=products;
        seat->player_source_capacity=capacity;
    }
    seat->player_source_count=0;
    for (size_t i=0;i<capacity;++i) {
        const qa_product *product=qa_catalog_at(catalog,i);
        if (!product->builtin || product->program_kind!=QA_PROGRAM_BUILTIN ||
            product->availability!=QA_CONTENT_INSTALLED) continue;
        size_t row=seat->player_source_count++;
        seat->player_source_titles[row]=product->title; seat->player_source_products[row]=product->id;
    }
    static const qa_launch_role roles[]={QA_ROLE_MOVEMENT,QA_ROLE_CHARACTER,QA_ROLE_ARSENAL};
    static const char *const names[]={"movement","character","arsenal"};
    size_t count=0;
    for (uint32_t physical=0;physical<f->options.seats;++physical) {
        uint32_t player,logical; qa_actor_id actor;
        if (!frontend_local_seat_read(choices,physical,&player)) continue;
        bool pending=f->player_source_draft || qa_application_startup_pending(f->application);
        bool enabled=!frontend_network_remote(f) && (pending ||
            (frontend_seat_launch_id_read(f,physical,&logical) && logical==player &&
             qa_application_player_actor(f->application,logical,&actor)));
        for (size_t role=0;role<3;++role) {
            const qa_launch_binding *binding=qa_launch_binding_for(choices,
                (qa_launch_scope){.kind=QA_SCOPE_SEAT,.seat=player},roles[role],"");
            const qa_launch_instance *selected=binding?qa_launch_snapshot_find(publication,binding->instance):NULL;
            size_t index=SIZE_MAX;
            for (size_t i=0;selected && i<seat->player_source_count;++i)
                if (seat->player_source_products[i]==selected->selection.product) index=i;
            char *label=seat->player_source_labels[physical][role];
            snprintf(label,sizeof(seat->player_source_labels[physical][role]),"Player %u %s",physical+1,names[role]);
            qa_ui_control *control=seat->controls+count;
            *control=button(seat,(qa_ui_id)physical*3+role+1,label,54+(float)count*32);
            control->kind=QA_UI_CHOICE; control->rect.x=32; control->rect.width=576;
            control->enabled=enabled && seat->player_source_count!=0;
            control->value.choice.labels=seat->player_source_titles;
            control->value.choice.count=seat->player_source_count; control->value.choice.selected=index;
            ++count;
        }
    }
    *out=(qa_ui_menu){.id=FRONTEND_PLAYER_SOURCES,.title="Player sources",.controls=seat->controls,
        .count=count,.fullscreen=true};
    return true;
}
static bool seat_services_create(frontend_seat *seat, qa_error *error)
{
    qa_frontend *frontend = seat->frontend;
    unsigned i = seat->id;
    qa_cvars *cvars = qa_application_cvars(frontend->application);
    qa_command_context command = {.seat = i, .origin = QA_COMMAND_SEAT, .dialect = QA_CONSOLE_Q1, .direct = true};
    if (!frontend_startup_launch_seat_read(frontend,i,&command.seat))
        (void)frontend_seat_launch_id_read(frontend,i,&command.seat);
    qa_input_seat_options input = {.seat=i,.context = command, .console = qa_application_console(frontend->application),
        .cvars = cvars, .gamepad = qa_gamepad_defaults(), .ui = input_handler, .ui_user = seat,
        .before_ui = source_input, .before_ui_user = seat,
        .context_ready=frontend_seat_context_ready,.context_user=seat};
    seat->input = qa_input_seat_create(&input, error);
    if (!seat->input || !qa_input_default_bindings(seat->input, (int32_t)i, error)) return false;
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
    if (!f || !source || !f->seats || physical>=f->options.seats ||
        source->context.physical_seat!=physical || f->seats[physical].frontend!=f ||
        f->seats[physical].id!=physical ||
        (!qa_application_client_current(f->application,source) &&
            !(f->source_restoring && qa_application_client_retirement_current(f->application,source))))
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
static bool engine_recipient_command(qa_frontend *f,uint32_t physical,qa_command_context *out,qa_error *error)
{
    if (!f || !f->application || !f->seats || physical>=f->options.seats || !out)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"ENGINE input handoff lost its actual physical seat");
    frontend_seat *seat=f->seats+physical;
    if (seat->frontend!=f || seat->id!=physical || !seat->input || !seat->console)
        return frontend_fail(error,QA_ERROR_ARGUMENT,"ENGINE input handoff lost its installed physical services");
    qa_command_context previous=qa_input_seat_context(seat->input);
    qa_command_context command={.seat=physical,.origin=QA_COMMAND_SEAT,.dialect=previous.dialect,.direct=true};
    (void)frontend_seat_launch_id_read(f,physical,&command.seat);
    *out=command; return true;
}
bool frontend_seat_engine_recipient_ready(qa_frontend *f,uint32_t physical,qa_command_context *out,qa_error *error)
{
    qa_command_context command;
    if (!out || !engine_recipient_command(f,physical,&command,error)) return false;
    frontend_seat *seat=f->seats+physical;
    qa_console *console=qa_application_console(f->application); qa_cvars *cvars=qa_application_cvars(f->application);
    if (!qa_input_seat_recipient_ready(seat->input,console,cvars,&command,error) ||
        !qa_seat_console_recipient_ready(seat->console,console,&command,error)) return false;
    *out=command; return true;
}
bool frontend_seat_engine_recipient_retirement_ready(qa_frontend *f,uint32_t physical,const qa_input_release *release,
    qa_console_release_disposition disposition,qa_console_release_retirement_fn guard,void *context,
    qa_command_context *out,qa_error *error)
{
    qa_command_context command;
    if (!out || !engine_recipient_command(f,physical,&command,error)) return false;
    frontend_seat *seat=f->seats+physical;
    qa_console *console=qa_application_console(f->application); qa_cvars *cvars=qa_application_cvars(f->application);
    if (!qa_input_seat_recipient_retirement_ready(seat->input,release,console,cvars,&command,
        disposition,guard,context,error) ||
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
static bool seats_create(qa_frontend *frontend, unsigned first, qa_error *error)
{
    if (!frontend || !frontend->application || !frontend->seats || !frontend->classic ||
        !frontend->primary || !frontend->ui_images || frontend->stepping)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "seat construction requires its actual resource owners");
    for (unsigned i = first; i < frontend->options.seats; ++i)
        if (frontend->seats[i].ui || frontend->seats[i].input || frontend->seats[i].console)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "local seat service destination is not qualified");
    qa_cvars *cvars = qa_application_cvars(frontend->application);
    if (!qa_input_settings_register(cvars, QA_MOVEMENT_NETQUAKE, error) ||
        !qa_input_device_settings_register(cvars, error)) return false;
    qa_launch_seat players[QA_INPUT_LOCAL_SEATS] = {0};
    char player_names[QA_INPUT_LOCAL_SEATS][32];
    for (unsigned i = 0; i < frontend->options.seats; ++i) {
        snprintf(player_names[i], sizeof(player_names[i]), "Player %u", i + 1);
        players[i] = (qa_launch_seat){.id = i, .name = player_names[i], .local = true, .input_device = i};
    }
    for (unsigned i = first; i < frontend->options.seats; ++i) {
        frontend_seat *seat = &frontend->seats[i]; seat->frontend = frontend; seat->id = i;
        seat->client_clock_ns=frontend->wall_time_ns;
        if (!seat_services_create(seat, error)) return false;
        qa_ui_preferences preferences;
        if (!seat->console || !qa_ui_preferences_read(qa_application_cvars(frontend->application), i, &preferences, error) ||
            !frontend_menu_font_selection(frontend, i, preferences.typeface == QA_UI_TYPEFACE_BOLD, &seat->fonts, error)) return false;
        qa_ui_options ui = {.seat = i, .input = seat->input, .fonts = seat->fonts,
            .art = frontend->menu_art,
            .white = qa_scene_white(frontend->ui_images), .context = seat, .clipboard = ui_clipboard, .localize = frontend_ui_localize,
            .binding = frontend_binding_capture, .binding_cancel = frontend_binding_cancel,
            .input_now_ms=input_now_ms};
        if (!frontend_menu_font_selection(frontend, i, true, &ui.title_fonts, error)) return false;
        if (!qa_ui_create(&ui, &seat->ui, error) || !qa_ui_register(seat->ui,
            &(qa_ui_menu_registration){.id = FRONTEND_HOME, .context = seat, .factory = home}, error) ||
            !qa_ui_register(seat->ui, &(qa_ui_menu_registration){.id = FRONTEND_OPTIONS,
                .context = seat, .factory = settings, .open = settings_open}, error) ||
            !frontend_settings_create(seat, error)) return false;
        frontend_startup_server_browser_menus browser={200,201,202};
        if (!frontend_startup_server_browser_create(seat,&browser,&seat->server_browser,error) ||
            !frontend_startup_downloads_create(seat,203,204,&seat->downloads_menu,error) ||
            !frontend_startup_rotation_create(seat,205,&seat->rotation_menu,error) ||
            !frontend_source_prompt_create(seat,206,&seat->source_prompt,error)) return false;
        if (!frontend_bindings_create(seat, error) || !frontend_accessibility_create(seat, error) ||
            !frontend_save_menu_create(seat, error) || !frontend_campaign_menu_create(seat,error)) return false;
        bool library = qa_ui_library_create(seat->ui, frontend->application, FRONTEND_LIBRARY,
            players, frontend->options.seats, &seat->library, error);
        if (!library || !frontend_startup_menus_create(seat, error) ||
            !qa_ui_rankings_create(seat->ui, frontend->application, FRONTEND_RANKINGS, -1, &seat->rankings, error) ||
            !qa_hud_create(&(qa_hud_options){.ui = seat->ui, .application = frontend->application, .seat = i,
                .context = seat, .read = hud_data, .presentation = hud_presentation, .video_frame = hud_video_frame,
                .video_context = seat}, &seat->hud, error) || !frontend_wheel_create(seat, error)) return false;
        if (frontend->tools && !qa_ui_llm_create(seat->ui, frontend_tools_llm(frontend),
            FRONTEND_ASSISTANCE, &seat->assistance, error)) return false;
    }
    return true;
}
bool frontend_seats_create(qa_frontend *frontend, qa_error *error)
{ return seats_create(frontend, 0, error); }
bool frontend_seats_create_range(qa_frontend *frontend, unsigned first, qa_error *error)
{ return seats_create(frontend, first, error); }
bool frontend_seats_destroy_range(qa_frontend *frontend, unsigned first, unsigned last, qa_error *error)
{
    for (unsigned i = first; i < last; ++i) {
        frontend_seat *seat = &frontend->seats[i];
        if (!frontend_startup_menus_destroy(seat,error) ||
            !frontend_save_menu_destroy(seat,error)) return false;
        frontend_settings_destroy(seat);
        if (!frontend_source_prompt_destroy(&seat->source_prompt,error) ||
            !frontend_startup_rotation_destroy(&seat->rotation_menu,error) ||
            !frontend_startup_downloads_destroy(&seat->downloads_menu,error) ||
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
        if (!qa_ui_llm_destroy(seat->assistance,(double)frontend->time_ns/1000000.0,error)) return false;
        seat->assistance=NULL;
        if (!qa_ui_destroy(seat->ui, 0, error)) return false;
        seat->ui = NULL;
        seat->player_sources_registered=false;
        qa_seat_console_destroy(seat->console); seat->console = NULL;
        qa_input_seat_destroy(seat->input); seat->input = NULL;
        frontend_player_retire(seat);
        frontend_bindings_destroy(seat);
        free(seat->player_source_titles); free(seat->player_source_products);
        seat->player_source_titles=NULL; seat->player_source_products=NULL;
        SDL_free(seat->clipboard); seat->clipboard = NULL;
        free(seat->wheel_items); free(seat->wheel_definitions); free(seat->wheel_labels);
        *seat=(frontend_seat){.frontend=frontend,.id=i};
    }
    return true;
}
bool frontend_seats_destroy(qa_frontend *frontend, qa_error *error)
{ return frontend_seats_destroy_range(frontend, 0, QA_INPUT_LOCAL_SEATS, error); }
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
static bool retired_recipient_services(const frontend_seat *seat,const qa_console *console,
    const qa_cvars *cvars,const qa_command_context *command)
{
    qa_application_client_source source;
    return seat && seat->frontend && command && command->owner && command->owner<=UINT32_MAX &&
        qa_application_client_physical_read(seat->frontend->application,(qa_actor_owner)command->owner,
            command->seat,&source,NULL) && source.context.physical_seat==seat->id &&
        qa_application_client_retirement_current(seat->frontend->application,&source) &&
        console==source.context.console && (!cvars || cvars==source.context.cvars) &&
        same_recipient_command(command,&source.context.command);
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
static bool input_services_encode(void *context, const qa_input_seat_options *options,
    uint64_t *out, qa_error *error)
{
    frontend_seat *seat=context;
    bool client=false;
    if (!saved_seat_ready(seat) || !options || !out || options->seat!=seat->id ||
        (!recipient_services(seat,options->console,options->cvars,&options->context,&client) &&
            !((qa_input_seat_release_read(seat->input) || seat->frontend->source_restoring) &&
                retired_recipient_services(seat,options->console,options->cvars,&options->context))) ||
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
    if (!saved_seat_ready(seat) || !out || dialect>QA_CONSOLE_Q3 ||
        (key&UINT64_C(65535)&~UINT64_C(256))!=(uint64_t)seat->id+1)
        return frontend_fail(error,QA_ERROR_FORMAT,"Saved input service descriptor names another prepared seat");
    qa_command_context command={.seat=seat->id,.origin=QA_COMMAND_SEAT,.dialect=(qa_console_dialect)dialect,.direct=true};
    qa_console *console=qa_application_console(seat->frontend->application);
    qa_cvars *cvars=qa_application_cvars(seat->frontend->application);
    if (client) {
        frontend_network_client_recipient recipient; bool present;
        if (!frontend_network_client_retired_recipient_read(seat->frontend,seat->id,&recipient,&present,error)) return false;
        bool retired=present;
        if (!retired && !frontend_network_client_recipient_read(seat->frontend,seat->id,&recipient,&present,error)) return false;
        if (!present || (!retired && !recipient.ready) || recipient.source.context.command.dialect!=(qa_console_dialect)dialect)
            return frontend_fail(error,QA_ERROR_FORMAT,"Saved input CLIENT recipient is absent or incomplete");
        command=recipient.source.context.command; console=recipient.source.context.console; cvars=recipient.source.context.cvars;
    }
    else (void)frontend_seat_launch_id_read(seat->frontend,seat->id,&command.seat);
    bool actual_client=false;
    if ((!recipient_services(seat,console,cvars,&command,&actual_client) &&
            !(client && seat->frontend->source_restoring && retired_recipient_services(seat,console,cvars,&command))) ||
        actual_client!=client)
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
static bool input_recipient_fields(void *context,qa_source_save_io *io,qa_input_seat_options *options)
{
    frontend_seat *seat=context;
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    qa_command_context command=reading?(qa_command_context){0}:options->context;
    uint32_t physical=reading?0:options->seat,dialect=command.dialect,origin=command.origin;
    uint64_t registry=reading?0:command.registry,generation=0;
    qa_sha256_digest identity={{0}};
    qa_application_client_source source;
    if (!saved_seat_ready(seat) || (!reading &&
        (!command.owner || command.owner>UINT32_MAX ||
         !qa_application_client_physical_read(seat->frontend->application,(qa_actor_owner)command.owner,
             command.seat,&source,io->error) || !qa_application_client_associated(seat->frontend->application,&source) ||
         source.context.physical_seat!=seat->id || options->console!=source.context.console ||
         options->cvars!=source.context.cvars || !same_recipient_command(&command,&source.context.command)))) return false;
    if (!reading) { identity=source.descriptor->identity; generation=source.configuration_generation; }
    if (!qa_source_save_u32(io,&physical) || physical!=seat->id ||
        !qa_source_save_bytes(io,&identity,sizeof(identity)) || !qa_source_save_u64(io,&generation) ||
        !qa_source_save_u64(io,&registry) || !registry ||
        !qa_source_save_u64(io,&command.owner) || !command.owner || command.owner>UINT32_MAX ||
        !qa_source_save_u64(io,&command.session) || !qa_source_save_u64(io,&command.client) ||
        !qa_source_save_u32(io,&command.seat) || !qa_source_save_u32(io,&dialect) || dialect>QA_CONSOLE_Q3 ||
        !qa_source_save_u32(io,&origin) || origin!=QA_COMMAND_SEAT ||
        !qa_source_save_bool(io,&command.direct) || !qa_source_save_bool(io,&command.console_text) || command.console_text ||
        !qa_source_save_u64(io,&command.registry) || command.registry!=registry ||
        !qa_source_save_u64(io,&command.generation) || !qa_source_save_u64(io,&command.actor.registry) ||
        !qa_source_save_u64(io,&command.actor.generation) || !qa_source_save_u32(io,&command.actor.slot) ||
        command.actor.registry || command.actor.generation || command.actor.slot) return false;
    if (reading) {
        command.dialect=(qa_console_dialect)dialect; command.origin=(qa_command_origin)origin;
        if (!seat->frontend->source_restoring ||
            !qa_application_client_physical_read(seat->frontend->application,(qa_actor_owner)command.owner,
                command.seat,&source,io->error) || !qa_application_client_associated(seat->frontend->application,&source) ||
            source.context.physical_seat!=seat->id || source.configuration_generation!=generation ||
            !qa_sha256_equal(&source.descriptor->identity,&identity)) return false;
        command.registry=source.context.command.registry;
        if (!same_recipient_command(&command,&source.context.command)) return false;
        *options=(qa_input_seat_options){.seat=physical,.context=source.context.command,
            .console=source.context.console,.cvars=source.context.cvars};
    }
    return true;
}
static bool input_release_ready(void *context,const qa_input_seat_options *options,
    const qa_input_release *release,qa_error *error)
{
    frontend_seat *seat=context;
    return (saved_seat_ready(seat) && options && release && options->seat==seat->id &&
        options->console==qa_input_release_console(release) &&
        retired_recipient_services(seat,options->console,options->cvars,&options->context)) ||
        frontend_fail(error,QA_ERROR_FORMAT,"Input release lacks its retained physical Source retirement custody");
}
qa_input_checkpoint_refs frontend_seat_input_refs(frontend_seat *seat)
{
    return (qa_input_checkpoint_refs){seat,input_services_encode,input_services_decode,
        input_ui_encode,input_ui_decode,input_catcher_ready,input_recipient_fields,input_release_ready};
}

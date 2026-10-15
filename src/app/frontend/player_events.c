#include "source_prompt.h"
#include "internal.h"
#include "qa/application_q1_composition.h"
#include "inventory_menu.h"

static bool scores(frontend_seat *seat, const qa_q2_player_event *event,
    uint64_t id, qa_error *error)
{
    if (event->score_count > SIZE_MAX / sizeof(qa_hud_score))
        return frontend_fail(error, QA_ERROR_MEMORY, "Q2 score list overflow");
    qa_event_lease *record = event->score_count ?
        qa_application_event_retain(seat->frontend->application, id) : NULL;
    if (event->score_count && !record)
        return frontend_fail(error, QA_ERROR_NOT_FOUND, "Q2 score event is unavailable");
    const qa_actor_record *local = qa_actors_get(qa_world_actors(qa_application_world(seat->frontend->application)), event->actor);
    bool local_source = local && local->has_source;
    uint32_t local_slot = local_source ? local->source_slot : 0;
    if (seat->q2_score_record) qa_event_lease_release(seat->q2_score_record);
    seat->q2_score_record = record;
    seat->q2_scores = event->score_count ? event->scores : NULL;
    seat->q2_score_count = event->score_count;
    seat->q2_score_local = local_source;
    seat->q2_score_local_slot = local_slot;
    return true;
}
static void help_line(frontend_seat *seat, unsigned slot, qa_string_id name)
{
    qa_strings *strings = qa_session_strings(qa_application_session(seat->frontend->application));
    const char *text = qa_strings_cstr(strings, name);
    seat->q2_help_text[slot] = name;
    seat->q2_help_lines[slot] = text ? text : "";
}
void frontend_player_retire(frontend_seat *seat)
{
    frontend_inventory_menu_clear(seat,seat);
    seat->light_level_known = false;
    qa_font_library_destroy(seat->q2_hud_fonts);
    seat->q2_hud_fonts = NULL; seat->q2_hud_classic = NULL;
    seat->q2_hud_font_provider = 0; seat->q2_hud_active = false;
    seat->q2_hud = (qa_application_native_q2_hud){0};
    qa_hud_q2_names_clear(&seat->q2_hud_names);
    seat->q2_hud_image_count=0;
    qa_hud_q2_program_clear(seat->q2_hud_layout);
    seat->q2_hud_status_text=NULL;seat->q2_hud_config_revision=seat->q2_hud_layout_revision=0;
    seat->q2_hud_pictures=(qa_hud_q2_picture_names){0};
    if (seat->q2_score_record) qa_event_lease_release(seat->q2_score_record);
    seat->q2_scores = NULL; seat->q2_score_record = NULL; seat->q2_score_count = 0;
    seat->q2_score_local = false; seat->q2_score_local_slot = 0;
    for (unsigned i = 0; i < 2; ++i) { seat->q2_help_text[i] = QA_STRING_NONE; seat->q2_help_lines[i] = ""; }
    seat->q2_actor = (qa_actor_id){0}; seat->q2_view = (qa_q2_player_view){0};
    seat->q2_view_ready = seat->q2_inventory = seat->q2_help = false;
    seat->q2_hit_marker = (qa_hud_q2_marker_state){0};
}
bool frontend_player_events(qa_frontend *frontend, qa_error *error)
{
    qa_application_map_view map;
    uint64_t revision = qa_application_map_read(frontend->application, &map) ? map.revision : 0;
    for (unsigned i = 0; i < frontend->options.seats && !frontend->options.dedicated; ++i) {
        frontend_seat *seat = &frontend->seats[i];
        qa_actor_id actor;
        if (seat->q2_player_map_revision != revision ||
            (seat->q2_actor.registry && (!frontend_seat_actor_read(frontend, i, &actor) ||
                !qa_actor_id_equal(actor, seat->q2_actor)))) frontend_player_retire(seat);
        seat->q2_player_map_revision = revision;
    }
    qa_application_event_range range = {.next = qa_application_events_local_first(frontend->application),
        .end = UINT64_MAX};
    qa_application_event_view output;
    while (qa_application_event_raw_next(frontend->application, &range,
        UINT64_C(1) << QA_APPLICATION_EVENT_BUILTIN, &output)) {
        qa_builtin_event event = *output.value.builtin;
        if (event.kind==QA_BUILTIN_SOURCE_PROMPT || event.kind==QA_BUILTIN_CLEAR_PROMPT) {
            for (unsigned seat=0;seat<frontend->options.seats && !frontend->options.dedicated;++seat)
                if (!frontend_network_local_input_owned(frontend,seat) &&
                    !frontend_source_prompt_receive(frontend->seats[seat].source_prompt,&event,error)) return false;
            continue;
        }
        if ((event.kind!=QA_BUILTIN_CTF_STATUS && event.kind!=QA_BUILTIN_CTF_CAPTURE) ||
            !qa_application_provider_instance(frontend->application,event.provider)) continue;
        for (unsigned seat=0;seat<frontend->options.seats && !frontend->options.dedicated;++seat) {
            if (frontend_network_local_input_owned(frontend,seat)) continue;
            qa_actor_id actor; uint32_t launch_seat;
            if (!frontend_seat_launch_id_read(frontend,seat,&launch_seat) ||
                !qa_application_player_actor(frontend->application,launch_seat,&actor)) continue;
            if (event.kind==QA_BUILTIN_CTF_STATUS) {
                if (qa_actor_id_equal(actor,event.actor) &&
                    !qa_hud_ctf_status(frontend->seats[seat].hud,&event,error)) return false;
            } else {
                uint64_t source_time; bool found=false;
                if (!qa_application_q1_ctf_recipient_read(frontend->application,event.provider,
                    actor,&source_time,&found,error)) return false;
                if (found && !qa_hud_ctf_capture(frontend->seats[seat].hud,&event,actor,error)) return false;
            }
        }
    }
    range = (qa_application_event_range){.next = qa_application_events_local_first(frontend->application),
        .end = UINT64_MAX};
    while (qa_application_event_raw_next(frontend->application, &range,
        UINT64_C(1) << QA_APPLICATION_EVENT_Q2_PLAYER, &output)) {
        qa_application_q2_player_event observed = *output.value.q2_player;
        const qa_q2_player_event *event = &observed.event;
        for (unsigned j = 0; j < frontend->options.seats; ++j) {
            if (frontend_network_local_input_owned(frontend,j)) continue;
            frontend_seat *seat = &frontend->seats[j]; qa_actor_id actor; uint32_t launch_seat;
            if (!frontend_seat_launch_id_read(frontend,j,&launch_seat) ||
                !qa_application_player_actor(frontend->application,launch_seat,&actor) ||
                (event->actor.registry && !qa_actor_id_equal(actor, event->actor))) continue;
            if (seat->q2_actor.registry && !qa_actor_id_equal(actor, seat->q2_actor)) frontend_player_retire(seat);
            seat->q2_actor = actor;
            switch (event->kind) {
            case QA_Q2_PLAYER_VIEW: {
                qa_actor_owner character = qa_application_selected_owner(frontend->application,
                    actor, QA_ROLE_CHARACTER);
                if (!character || character != observed.provider) break;
                seat->q2_view = event->view; seat->q2_view_ready = true;
                if(!(event->view.layouts&2)) {
                    seat->q2_inventory=false;frontend_inventory_menu_clear(seat,seat);
                } else if(seat->q2_inventory)
                    frontend_inventory_menu_player(seat,actor,event->view.selected_item,true);
                if (event->view.hit_marker_damage > 0)
                    qa_hud_q2_marker_hit(&seat->q2_hit_marker,observed.time_ns,observed.time_ns);
                break;
            }
            case QA_Q2_PLAYER_SCOREBOARD:
                if (!scores(seat, event, range.id, error)) return false;
                seat->q2_inventory=false;frontend_inventory_menu_clear(seat,seat);break;
            case QA_Q2_PLAYER_INVENTORY:
                seat->q2_inventory = event->visible;
                frontend_inventory_menu_player(seat,actor,event->selected_item,event->visible);break;
            case QA_Q2_PLAYER_HELP:
                seat->q2_help = event->visible;
                if(event->visible) {
                    seat->q2_inventory=false;frontend_inventory_menu_clear(seat,seat);
                }
                break;
            case QA_Q2_PLAYER_PRINT:
                if (event->text && !qa_hud_notify(seat->hud, event->text, event->level == 3,
                        frontend->time_ns, UINT64_C(4000000000), error)) return false;
                break;
            case QA_Q2_PLAYER_STUFFTEXT: {
                if (!event->text) break;
                const qa_launch_snapshot *publication=qa_application_launch(frontend->application);
                const char *instance=qa_application_provider_instance(frontend->application,observed.provider);
                const qa_launch_instance *descriptor=instance?qa_launch_snapshot_find(publication,instance):NULL;
                qa_application_startup_source source;
                if (!qa_application_startup_source_read(frontend->application,publication,descriptor,&source,error)) return false;
                qa_command_context command=source.command;
                command.seat=launch_seat; command.actor=actor; command.script="q2:stufftext";
                if (!qa_application_capture_command_context(frontend->application, &command, &command, error) ||
                    !qa_console_append(source.console, &command, event->text, error)) return false;
                break;
            }
            default: break;
            }
        }
    }
    qa_strings *strings = qa_session_strings(qa_application_session(frontend->application));
    range = (qa_application_event_range){.next = qa_application_events_local_first(frontend->application),
        .end = UINT64_MAX};
    while (qa_application_event_raw_next(frontend->application, &range,
        UINT64_C(1) << QA_APPLICATION_EVENT_Q2_MAP, &output)) {
        qa_application_q2_map_event observed = *output.value.q2_map;
        const qa_q2_map_event *event = &observed.event;
        if (event->kind != QA_Q2_MAP_HELP && event->kind != QA_Q2_MAP_HELP_COMPUTER && event->kind != QA_Q2_MAP_STORY) continue;
        for (unsigned j = 0; j < frontend->options.seats; ++j) {
            if (frontend_network_local_input_owned(frontend,j)) continue;
            frontend_seat *seat = &frontend->seats[j]; qa_actor_id actor;
            if (!frontend_seat_actor_read(frontend,j,&actor) ||
                (event->recipient.registry && !qa_actor_id_equal(actor, event->recipient))) continue;
            const char *text = qa_strings_cstr(strings, event->text);
            if (event->kind == QA_Q2_MAP_HELP && event->slot >= 1 && event->slot <= 2) {
                help_line(seat, (unsigned)event->slot - 1, event->text);
            } else if (event->kind == QA_Q2_MAP_HELP_COMPUTER) {
                seat->q2_help = event->visible;
                if(event->visible) {
                    seat->q2_inventory=false;frontend_inventory_menu_clear(seat,seat);
                }
                help_line(seat, 0, event->text); help_line(seat, 1, event->resource);
            } else if (event->kind == QA_Q2_MAP_STORY && text &&
                !qa_hud_center_print(seat->hud, text, frontend->time_ns, UINT64_C(6000000000),(qa_hud_center_policy){.instant=false,.character_ns=UINT64_C(50000000)}, error)) return false;
        }
    }
    return true;
}

#include "source_prompt.h"
#include "internal.h"
#include "qa/application_q1_composition.h"

static bool scores(frontend_seat *seat, const qa_q2_player_event *event, qa_error *error)
{
    if (event->score_count > SIZE_MAX / sizeof(qa_hud_score))
        return frontend_fail(error, QA_ERROR_MEMORY, "Q2 score list overflow");
    size_t bytes = 0;
    for (size_t i = 0; i < event->score_count; ++i) {
        size_t length = strlen(event->scores[i].name ? event->scores[i].name : "") + 1;
        if (length > SIZE_MAX - bytes) return frontend_fail(error, QA_ERROR_MEMORY, "Q2 score names overflow");
        bytes += length;
    }
    qa_hud_score *rows = event->score_count ? calloc(event->score_count, sizeof(*rows)) : NULL;
    char *names = bytes ? malloc(bytes) : NULL;
    if ((event->score_count && !rows) || (bytes && !names)) {
        free(rows); free(names); return frontend_fail(error, QA_ERROR_MEMORY, "retaining Q2 scores");
    }
    const qa_actor_record *local = qa_actors_get(qa_world_actors(qa_application_world(seat->frontend->application)), event->actor);
    size_t offset = 0;
    for (size_t i = 0; i < event->score_count; ++i) {
        const qa_q2_score_row *source = &event->scores[i];
        const char *name = source->name ? source->name : "";
        size_t length = strlen(name) + 1; memcpy(names + offset, name, length);
        rows[i] = (qa_hud_score){.name = names + offset, .score = source->score, .ping = source->ping,
            .local = local && local->has_source && local->source_slot == source->slot, .spectator = source->spectator};
        offset += length;
    }
    free(seat->q2_scores); free(seat->q2_score_names);
    seat->q2_scores = rows; seat->q2_score_names = names; seat->q2_score_count = event->score_count;
    return true;
}
static bool help_line(frontend_seat *seat, unsigned slot, const char *text, qa_error *error)
{
    char *copy = NULL;
    if (text && *text) {
        size_t size = strlen(text) + 1; copy = malloc(size);
        if (!copy) return frontend_fail(error, QA_ERROR_MEMORY, "retaining Q2 help text");
        memcpy(copy, text, size);
    }
    free(seat->q2_help_text[slot]); seat->q2_help_text[slot] = copy;
    seat->q2_help_lines[slot] = copy ? copy : "";
    return true;
}
void frontend_player_retire(frontend_seat *seat)
{
    qa_font_library_destroy(seat->q2_hud_fonts);
    seat->q2_hud_fonts = NULL; seat->q2_hud_classic = NULL;
    seat->q2_hud_font_provider = 0; seat->q2_hud_active = false;
    seat->q2_hud = (qa_application_native_q2_hud){0};
    free(seat->q2_scores); free(seat->q2_score_names);
    seat->q2_scores = NULL; seat->q2_score_names = NULL; seat->q2_score_count = 0;
    for (unsigned i = 0; i < 2; ++i) { free(seat->q2_help_text[i]); seat->q2_help_text[i] = NULL; seat->q2_help_lines[i] = ""; }
    seat->q2_actor = (qa_actor_id){0}; seat->q2_view = (qa_q2_player_view){0};
    seat->q2_view_ready = seat->q2_inventory = seat->q2_help = false;
}
bool frontend_player_events(qa_frontend *frontend, qa_error *error)
{
    for (uint64_t i=qa_application_events_local_first(frontend->application);
        i<qa_application_events_next(frontend->application);++i) {
        qa_application_event_view output;
        if (!qa_application_event_read(frontend->application, &(qa_application_event_cursor){.id = i, .projection = 0}, &output) ||
            output.kind != QA_APPLICATION_EVENT_BUILTIN) continue;
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
    for (uint64_t i = qa_application_events_local_first(frontend->application);
        i < qa_application_events_next(frontend->application); ++i) {
        qa_application_event_view output;
        if (!qa_application_event_read(frontend->application, &(qa_application_event_cursor){.id = i, .projection = 0}, &output) ||
            output.kind != QA_APPLICATION_EVENT_Q2_PLAYER) continue;
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
                if (event->view.hit_marker_damage > 0) {
                    const uint64_t duration = UINT64_C(150000000);
                    if (observed.time_ns > UINT64_MAX - duration)
                        return frontend_fail(error, QA_ERROR_ARGUMENT, "Q2 hit-marker deadline overflow");
                    qa_hud_hit_marker(seat->hud, (float)event->view.hit_marker_damage,
                        observed.time_ns + duration);
                }
                break;
            }
            case QA_Q2_PLAYER_SCOREBOARD: if (!scores(seat, event, error)) return false; break;
            case QA_Q2_PLAYER_INVENTORY: seat->q2_inventory = event->visible; break;
            case QA_Q2_PLAYER_HELP: seat->q2_help = event->visible; break;
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
    for (uint64_t i = qa_application_events_local_first(frontend->application);
        i < qa_application_events_next(frontend->application); ++i) {
        qa_application_event_view output;
        if (!qa_application_event_read(frontend->application, &(qa_application_event_cursor){.id = i, .projection = 0}, &output) ||
            output.kind != QA_APPLICATION_EVENT_Q2_MAP) continue;
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
                if (!help_line(seat, (unsigned)event->slot - 1, text, error)) return false;
            } else if (event->kind == QA_Q2_MAP_HELP_COMPUTER) {
                seat->q2_help = event->visible;
                if (!help_line(seat, 0, text, error) || !help_line(seat, 1, qa_strings_cstr(strings, event->resource), error)) return false;
            } else if (event->kind == QA_Q2_MAP_STORY && text &&
                !qa_hud_center_print(seat->hud, text, frontend->time_ns, UINT64_C(6000000000),(qa_hud_center_policy){.instant=false,.character_ns=UINT64_C(50000000)}, error)) return false;
        }
    }
    return true;
}

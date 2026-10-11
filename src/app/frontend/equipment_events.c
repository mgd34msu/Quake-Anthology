#include "equipment_events.h"
#include "qa/application_ui_names.h"
#include <stdio.h>

struct frontend_equipment_events {
    qa_frontend *frontend;
    qa_actor_owner queue_owner;
    uint64_t generation;
    uint64_t cursor;
    size_t projection;
    uint32_t hud_delivered, console_delivered;
    bool printed, busy;
};

bool frontend_equipment_events_create(qa_frontend *frontend, frontend_equipment_events **out, qa_error *error)
{
    if (!frontend || !frontend->application || !out)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear presentation requires its actual frontend");
    frontend_equipment_events *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating gear event consumer");
    owner->frontend = frontend; *out = owner; return true;
}
bool frontend_equipment_events_idle(const frontend_equipment_events *owner)
{ return !owner || !owner->busy; }
bool frontend_equipment_events_destroy(frontend_equipment_events *owner, qa_error *error)
{
    if (!frontend_equipment_events_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear presentation retains event delivery");
    free(owner);
    return true;
}
void frontend_equipment_events_rebind(frontend_equipment_events *owner, qa_frontend *destination)
{ if (owner) owner->frontend = destination; }

static bool destination(const frontend_equipment_events *owner, const qa_application_equipment_event *event,
    uint32_t ordinal)
{
    if (event->index < 0) return true;
    if (!event->recipient.registry) return false;
    uint32_t seat; qa_actor_id actor;
    return frontend_seat_launch_id_read(owner->frontend, ordinal, &seat) &&
        qa_application_player_actor(owner->frontend->application, seat, &actor) &&
        qa_actor_id_equal(actor, event->recipient);
}
static bool command(frontend_equipment_events *owner, const qa_application_equipment_event *event, qa_error *error)
{
    qa_command_tokens tokens = {0};
    if (!qa_command_tokenize(event->text, QA_RULESET_Q3, false, &tokens,
        qa_arena_alloc_callback, &owner->frontend->frame.storage, error)) return false;
    bool okay = true;
    if (tokens.count >= 2) {
        const char *text = tokens.values[1];
        qa_string_id name = qa_strings_find(qa_session_strings(
            qa_application_session(owner->frontend->application)),
            (qa_bytes){(const uint8_t *)tokens.values[0], strlen(tokens.values[0])});
        const qa_application_ui_names *names = qa_application_ui_names_read(owner->frontend->application);
        bool center = name == names->cp, chat = name == names->chat || name == names->tchat;
        bool print = chat || name == names->print;
        for (uint32_t ordinal = 0; okay && (center || print) && !owner->frontend->options.dedicated &&
            ordinal < owner->frontend->options.seats; ++ordinal) {
            if (!destination(owner, event, ordinal)) continue;
            uint32_t bit = UINT32_C(1) << ordinal;
            frontend_seat *seat = &owner->frontend->seats[ordinal];
            if ((center || chat) && !(owner->hud_delivered & bit)) {
                okay = center ? qa_hud_center_print(seat->hud, text, event->time_ns,
                    UINT64_C(3000000000),(qa_hud_center_policy){.instant=true,.source_layout=true,.y=143,.character_width=16,.fade_ns=UINT64_C(200000000)}, error) : qa_hud_notify(seat->hud, text, true,
                    event->time_ns, UINT64_C(3000000000), error);
                if (okay) owner->hud_delivered |= bit;
            }
            if (okay && print && !(owner->console_delivered & bit)) {
                okay = qa_seat_console_print(seat->console, text, error);
                if (okay) owner->console_delivered |= bit;
            }
            if (okay && print && !owner->printed) { fputs(text, stdout); owner->printed = true; }
        }
    }
    qa_command_tokens_free(&tokens); return okay;
}
static void clear_delivery(frontend_equipment_events *owner)
{ owner->cursor = qa_application_events_local_first(owner->frontend->application);
  owner->projection = 0; owner->hud_delivered = owner->console_delivered = 0; owner->printed = false; }

bool frontend_equipment_events_drain(frontend_equipment_events *owner, qa_error *error)
{
    if (!owner || owner->busy || !owner->frontend || !owner->frontend->application ||
        owner->frontend->options.seats > QA_INPUT_LOCAL_SEATS)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear event delivery lacks its actual idle recipient owners");
    qa_application *application = owner->frontend->application;
    qa_actor_owner queue_owner = qa_application_equipment_events_owner(application);
    uint64_t generation = qa_application_protocol_events_generation(application);
    if (owner->queue_owner != queue_owner) {
        clear_delivery(owner); owner->queue_owner = queue_owner;
        owner->generation = generation;
    } else if (owner->generation != generation) {
        clear_delivery(owner); owner->generation = generation;
    }
    uint64_t first = qa_application_events_local_first(application), next = qa_application_events_next(application);
    if (owner->cursor < first) { clear_delivery(owner); owner->cursor = first; }
    owner->busy = true; bool okay = true;
    qa_application_event_cursor event_cursor = {.id = owner->cursor, .projection = owner->projection};
    while (okay && owner->cursor < next) {
        qa_application_event_view output;
        if (!qa_application_event_read(application, &event_cursor, &output) || !output.equipment) {
            ++owner->cursor; owner->projection = 0;
            owner->hud_delivered = owner->console_delivered = 0; owner->printed = false;
            event_cursor = (qa_application_event_cursor){.id = owner->cursor};
            continue;
        }
        const qa_application_equipment_event *event = output.equipment;
        if (qa_application_equipment_event_source_current(application, event->provider,
            event->selected_provider, event->service_owner)) {
            switch (event->kind) {
            case QA_APPLICATION_EQUIPMENT_CONFIGSTRING: break;
            case QA_APPLICATION_EQUIPMENT_SERVER_COMMAND: okay = command(owner, event, error); break;
            }
        }
        if (okay) {
            ++owner->projection; owner->hud_delivered = owner->console_delivered = 0; owner->printed = false;
        }
    }
    owner->busy = false; return okay;
}

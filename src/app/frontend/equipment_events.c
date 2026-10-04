#include "equipment_events.h"
#include "save_private.h"
#include <stdio.h>

typedef struct equipment_namespace {
    struct equipment_namespace *next;
    qa_actor_owner provider, selected;
    qa_string_id service;
    char *configstrings[1024];
} equipment_namespace;

struct frontend_equipment_events {
    qa_frontend *frontend;
    equipment_namespace *sources;
    qa_actor_owner queue_owner;
    uint64_t generation;
    size_t cursor;
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
static void dispose_namespaces(frontend_equipment_events *owner)
{
    while (owner->sources) {
        equipment_namespace *source = owner->sources; owner->sources = source->next;
        for (size_t i = 0; i < 1024; ++i) free(source->configstrings[i]);
        free(source);
    }
}
bool frontend_equipment_events_destroy(frontend_equipment_events *owner, qa_error *error)
{
    if (!frontend_equipment_events_idle(owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear presentation retains event delivery");
    if (owner) { dispose_namespaces(owner); free(owner); }
    return true;
}
void frontend_equipment_events_rebind(frontend_equipment_events *owner, qa_frontend *destination)
{ if (owner) owner->frontend = destination; }

static bool namespace_current(const frontend_equipment_events *owner, const equipment_namespace *source)
{
    return qa_application_equipment_event_source_current(owner->frontend->application,
        source->provider, source->selected, source->service);
}
static equipment_namespace *find_namespace(const frontend_equipment_events *owner, qa_actor_owner provider)
{
    for (equipment_namespace *source = owner->sources; source; source = source->next)
        if (source->provider == provider) return source;
    return NULL;
}
static bool configstring(frontend_equipment_events *owner, const qa_application_equipment_event *event, qa_error *error)
{
    equipment_namespace *source = find_namespace(owner, event->provider);
    if (source && (source->selected != event->selected_provider || source->service != event->service_owner))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear configstring namespace changed its source owner");
    size_t length = strlen(event->text);
    if (length == SIZE_MAX) return frontend_fail(error, QA_ERROR_MEMORY, "Gear configstring text overflows");
    char *text = malloc(length + 1);
    if (!text) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining reached gear configstring");
    memcpy(text, event->text, length + 1);
    if (!source) {
        source = calloc(1, sizeof(*source));
        if (!source) { free(text); return frontend_fail(error, QA_ERROR_MEMORY, "Retaining reached gear namespace"); }
        source->provider = event->provider; source->selected = event->selected_provider; source->service = event->service_owner;
        source->next = owner->sources; owner->sources = source;
    }
    free(source->configstrings[event->index]); source->configstrings[event->index] = text;
    return true;
}
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
    if (!qa_command_tokenize(event->text, QA_CONSOLE_Q3, false, &tokens, error)) return false;
    bool okay = true;
    if (tokens.count >= 2) {
        const char *name = tokens.values[0], *text = tokens.values[1];
        bool center = !strcmp(name, "cp"), chat = !strcmp(name, "chat") || !strcmp(name, "tchat");
        bool print = chat || !strcmp(name, "print");
        for (uint32_t ordinal = 0; okay && (center || print) && !owner->frontend->options.dedicated &&
            ordinal < owner->frontend->options.seats; ++ordinal) {
            if (!destination(owner, event, ordinal)) continue;
            uint32_t bit = UINT32_C(1) << ordinal;
            frontend_seat *seat = &owner->frontend->seats[ordinal];
            if ((center || chat) && !(owner->hud_delivered & bit)) {
                okay = center ? qa_hud_center_print(seat->hud, text, event->time_ns,
                    UINT64_C(3000000000), true, 0, error) : qa_hud_notify(seat->hud, text, true,
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
{ owner->cursor = 0; owner->hud_delivered = owner->console_delivered = 0; owner->printed = false; }

bool frontend_equipment_events_drain(frontend_equipment_events *owner, qa_error *error)
{
    if (!owner || owner->busy || !owner->frontend || !owner->frontend->application ||
        owner->frontend->options.seats > QA_INPUT_LOCAL_SEATS)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear event delivery lacks its actual idle recipient owners");
    qa_application *application = owner->frontend->application;
    qa_actor_owner queue_owner = qa_application_equipment_events_owner(application);
    uint64_t generation = qa_application_equipment_events_generation(application);
    if (owner->queue_owner != queue_owner) {
        dispose_namespaces(owner); clear_delivery(owner); owner->queue_owner = queue_owner;
        owner->generation = generation;
    } else if (owner->generation != generation) {
        clear_delivery(owner); owner->generation = generation;
    }
    size_t count = qa_application_equipment_event_count(application);
    if (owner->cursor > count) return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear delivery cursor exceeds its actual pending queue");
    owner->busy = true; bool okay = true;
    while (okay && owner->cursor < count) {
        qa_application_equipment_event event;
        okay = qa_application_equipment_event_at(application, owner->cursor, &event, error);
        if (!okay) break;
        switch (event.kind) {
        case QA_APPLICATION_EQUIPMENT_CONFIGSTRING: okay = configstring(owner, &event, error); break;
        case QA_APPLICATION_EQUIPMENT_SERVER_COMMAND: okay = command(owner, &event, error); break;
        }
        if (okay && (owner->frontend->application != application ||
            qa_application_equipment_events_owner(application) != queue_owner ||
            qa_application_equipment_events_generation(application) != generation ||
            qa_application_equipment_event_count(application) != count))
            okay = frontend_fail(error, QA_ERROR_ARGUMENT, "Gear pending queue changed during recipient delivery");
        if (okay) {
            ++owner->cursor; owner->hud_delivered = owner->console_delivered = 0; owner->printed = false;
        }
    }
    owner->busy = false; return okay;
}
bool frontend_equipment_events_configstring(const frontend_equipment_events *owner, qa_actor_owner provider,
    uint32_t index, const char **text, bool *present, qa_error *error)
{
    if (!owner || !owner->frontend || !owner->frontend->application || !text || !present || index >= 1024)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Reached gear configstring requires its private namespace");
    equipment_namespace *source = find_namespace(owner, provider);
    if (source && !namespace_current(owner, source))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Reached gear namespace has retired");
    *present = source && source->configstrings[index]; *text = *present ? source->configstrings[index] : NULL;
    return true;
}

static bool header(qa_source_save_io *io, frontend_equipment_events *owner, size_t *count)
{
    uint8_t magic[4] = {'Q','F','G','E'}; return qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QFGE", 4) &&
        qa_source_save_string(io, &owner->queue_owner) &&
        qa_source_save_u64(io, &owner->generation) && qa_source_save_count(io, &owner->cursor, SIZE_MAX) &&
        qa_source_save_u32(io, &owner->hud_delivered) && qa_source_save_u32(io, &owner->console_delivered) &&
        qa_source_save_bool(io, &owner->printed) && qa_source_save_count(io, count, SIZE_MAX);
}
static bool namespace_fields(qa_source_save_io *io, equipment_namespace *source)
{
    if (!qa_source_save_string(io, &source->provider) || !source->provider ||
        !qa_source_save_string(io, &source->selected) || !source->selected ||
        !qa_source_save_string(io, &source->service) || !source->service) return false;
    for (size_t i = 0; i < 1024; ++i)
        if (!qa_source_save_owned_text(io, &source->configstrings[i])) return false;
    return true;
}
static bool saved_current(const frontend_equipment_events *owner, qa_error *error)
{
    const qa_application *application = owner->frontend->application;
    uint32_t seats = owner->frontend->options.seats;
    if (seats > QA_INPUT_LOCAL_SEATS)
        return frontend_fail(error, QA_ERROR_FORMAT, "Saved gear delivery exceeds its actual seat count");
    uint32_t mask = (UINT32_C(1) << seats) - 1;
    if (owner->queue_owner != qa_application_equipment_events_owner(application) ||
        owner->generation != qa_application_equipment_events_generation(application) ||
        owner->cursor > qa_application_equipment_event_count(application) ||
        ((owner->hud_delivered | owner->console_delivered) & ~mask))
        return frontend_fail(error, QA_ERROR_FORMAT, "Saved gear delivery differs from its actual pending queue");
    if (owner->cursor == qa_application_equipment_event_count(application) &&
        (owner->hud_delivered || owner->console_delivered || owner->printed))
        return frontend_fail(error, QA_ERROR_FORMAT, "Completed gear delivery retains partial recipients");
    if (owner->hud_delivered || owner->console_delivered || owner->printed) {
        qa_application_equipment_event event;
        if (!qa_application_equipment_event_at(application, owner->cursor, &event, error) ||
            event.kind != QA_APPLICATION_EQUIPMENT_SERVER_COMMAND)
            return frontend_fail(error, QA_ERROR_FORMAT, "Partial gear delivery lost its actual command");
        qa_command_tokens tokens = {0};
        if (!qa_command_tokenize(event.text, QA_CONSOLE_Q3, false, &tokens, error)) return false;
        bool center = tokens.count >= 2 && !strcmp(tokens.values[0], "cp");
        bool chat = tokens.count >= 2 && (!strcmp(tokens.values[0], "chat") || !strcmp(tokens.values[0], "tchat"));
        bool print = chat || (tokens.count >= 2 && !strcmp(tokens.values[0], "print"));
        qa_command_tokens_free(&tokens);
        if ((owner->hud_delivered && !(center || chat)) ||
            (owner->console_delivered && !print) ||
            (owner->printed != (owner->console_delivered != 0)) ||
            (chat && (owner->console_delivered & ~owner->hud_delivered)))
            return frontend_fail(error, QA_ERROR_FORMAT, "Partial gear delivery differs from its actual command outputs");
        if (owner->frontend->options.dedicated)
            return frontend_fail(error, QA_ERROR_FORMAT, "Dedicated gear delivery retains local outputs");
        for (uint32_t ordinal = 0; ordinal < seats; ++ordinal)
            if (((owner->hud_delivered | owner->console_delivered) & (UINT32_C(1) << ordinal)) &&
                !destination(owner, &event, ordinal))
                return frontend_fail(error, QA_ERROR_FORMAT, "Partial gear delivery lost its actual recipient");
    }
    for (equipment_namespace *source = owner->sources; source; source = source->next)
        if (!namespace_current(owner, source)) return frontend_fail(error, QA_ERROR_FORMAT, "Saved reached gear namespace has retired");
    return true;
}
bool frontend_equipment_events_checkpoint(const frontend_equipment_events *owner, qa_buffer *out, qa_error *error)
{
    if (!owner || !owner->frontend || !owner->frontend->application || !frontend_equipment_events_idle(owner) ||
        !out || out->data || out->size || owner->frontend->stepping)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear event capture requires its returned actual frontend");
    frontend_equipment_events saved = *owner; size_t count = 0;
    qa_actor_owner current_owner = qa_application_equipment_events_owner(owner->frontend->application);
    uint64_t generation = qa_application_equipment_events_generation(owner->frontend->application);
    /* An actual completed clear retires delivery progress. A different real
     * gear namespace also retires its reached rows. Capture performs no output. */
    if (saved.queue_owner != current_owner) {
        saved.sources = NULL; saved.queue_owner = current_owner;
        clear_delivery(&saved); saved.generation = generation;
    } else if (saved.generation != generation) {
        clear_delivery(&saved); saved.generation = generation;
    }
    if (!saved_current(&saved, error)) return false;
    for (equipment_namespace *source = saved.sources; source; source = source->next) ++count;
    qa_source_save_io io = {0};
    bool okay = qa_source_save_writer(&io, qa_application_session(owner->frontend->application), error) && header(&io, &saved, &count);
    for (equipment_namespace *source = saved.sources; okay && source; source = source->next)
        okay = namespace_fields(&io, source);
    if (okay) okay = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return okay;
}
bool frontend_equipment_events_restore(frontend_equipment_events *owner, qa_bytes bytes, qa_error *error)
{
    if (!owner || !owner->frontend || !owner->frontend->application || !frontend_equipment_events_idle(owner) ||
        owner->sources || owner->queue_owner || owner->generation || owner->cursor || owner->hud_delivered ||
        owner->console_delivered || owner->printed || owner->frontend->stepping)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Gear event import requires its empty actual candidate");
    frontend_equipment_events candidate = {.frontend = owner->frontend}; qa_source_save_io io = {0}; size_t count = 0;
    bool okay = qa_source_save_reader(&io, qa_application_session(owner->frontend->application), bytes, error) &&
        header(&io, &candidate, &count);
    if (okay && count > (bytes.size - io.offset) / 1024) okay = false;
    equipment_namespace **tail = &candidate.sources;
    for (size_t i = 0; okay && i < count; ++i) {
        equipment_namespace *source = calloc(1, sizeof(*source));
        if (!source) { okay = frontend_fail(error, QA_ERROR_MEMORY, "Decoding reached gear namespace"); break; }
        *tail = source; tail = &source->next;
        okay = namespace_fields(&io, source);
        for (equipment_namespace *previous = candidate.sources; okay && previous != source; previous = previous->next)
            if (previous->provider == source->provider) okay = false;
    }
    if (okay) okay = qa_source_save_finish(&io, NULL) && saved_current(&candidate, error);
    qa_source_save_dispose(&io);
    if (okay) *owner = candidate;
    else dispose_namespaces(&candidate);
    if (!okay && (!error || error->code == QA_OK)) frontend_fail(error, QA_ERROR_FORMAT, "Invalid reached gear presentation continuation");
    return okay;
}

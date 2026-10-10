#include "equipment_events.h"
#include "equipment_runtime.h"
#include "event_stream.h"
#include "qa/application_network.h"
#include "qa/source_save.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool valid(const application_equipment_events *queue,
    const qa_application_equipment_event *event, qa_error *error)
{
    qa_strings *strings = queue ? qa_session_strings(queue->session) : NULL;
    const char *provider = strings && event ? qa_strings_cstr(strings, event->provider) : NULL;
    const char *selected = strings && event ? qa_strings_cstr(strings, event->selected_provider) : NULL;
    const char *service = strings && event ? qa_strings_cstr(strings, event->service_owner) : NULL;
    if (!event || !provider || !*provider || !selected || !*selected || !service || !*service ||
        event->provider == event->selected_provider || !event->text ||
        event->time_ns > (uint64_t)INT32_MAX * UINT64_C(1000000) ||
        event->time_ns % UINT64_C(1000000) ||
        (!event->recipient.registry && (event->recipient.generation || event->recipient.slot)))
        return application_fail(error, QA_ERROR_FORMAT, "Gear event lost its actual source identity");
    switch (event->kind) {
    case QA_APPLICATION_EQUIPMENT_CONFIGSTRING:
        if (event->index >= 0 && event->index < 1024 && !event->recipient.registry) return true;
        break;
    case QA_APPLICATION_EQUIPMENT_SERVER_COMMAND:
        if (event->index < 64 && (event->index >= 0 || !event->recipient.registry)) return true;
        break;
    }
    return application_fail(error, QA_ERROR_FORMAT, "Gear event has an invalid source destination");
}

bool application_equipment_events_create(qa_application *application, qa_session *session,
    application_equipment_events **out, qa_error *error)
{
    if (!application || !session || !out)
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear events lack their session");
    application_equipment_events *queue = calloc(1, sizeof(*queue));
    if (!queue) return application_fail(error, QA_ERROR_MEMORY, "Allocating gear event custody");
    queue->application = application; queue->session = session;
    queue->identity = ++application->equipment_event_owner_next;
    *out = queue; return true;
}

void application_equipment_events_destroy(application_equipment_events *queue)
{ free(queue); }

static bool capacity(application_equipment_events *queue, const application_event_write *write,
    const qa_application_equipment_event *event, qa_error *error)
{
    return event->recipient.registry ? application_event_stream_close_recipients(queue->application,
        write, event->recipient, NULL, QA_APPLICATION_OUTPUT_UNIFIED, error) :
        application_event_stream_close_subscribers(queue->application, write, error);
}

bool application_equipment_events_publish(application_equipment_events *queue,
    const qa_application_equipment_event *event, qa_error *error)
{
    if (!queue || !valid(queue, event, error)) return false;
    if (event->recipient.registry && !qa_actors_get(qa_session_actors(queue->session), event->recipient))
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear command recipient retired before publication");
    qa_application *app = queue->application;
    application_event_write own_write;
    bool own = app->event_write == NULL;
    if (own && !application_event_stream_begin(app, QA_APPLICATION_EVENT_EQUIPMENT, &own_write, error))
        return capacity(queue, &own_write, event, error);
    application_event_write *write = app->event_write;
    application_equipment_event_record *record = own ? &write->envelope->raw.equipment :
        application_event_stream_alloc(app, sizeof(*record), _Alignof(application_equipment_event_record), error);
    if (!record) goto abort;
    size_t length = strlen(event->text);
    char *text = application_event_stream_alloc(app, length + 1, 1, error);
    if (!text) goto abort;
    memcpy(text, event->text, length + 1);
    *record = (application_equipment_event_record){.event = *event, .owner = queue->identity};
    record->event.text = text;
    if (write->envelope->last_equipment) write->envelope->last_equipment->next = record;
    else write->envelope->equipment = record;
    write->envelope->last_equipment = record;
    return !own || application_event_stream_commit(app, write, error) || capacity(queue, write, event, error);
abort:
    if (!own) return false;
    application_event_stream_abort(app, write, error);
    return capacity(queue, write, event, error);
}

static bool header(qa_source_save_io *io, uint64_t *generation, size_t *count)
{
    uint8_t magic[8] = {'Q','A','G','E','V','T',0,0};
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "QAGEVT\0", sizeof(magic)) &&
        qa_source_save_u64(io, generation) &&
        qa_source_save_count(io, count, SIZE_MAX / sizeof(qa_application_equipment_event));
}

static bool fields(qa_source_save_io *io, const application_equipment_events *queue,
    qa_application_equipment_event *event)
{
    uint32_t kind = event->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_APPLICATION_EQUIPMENT_SERVER_COMMAND ||
        !qa_source_save_string(io, &event->provider) || !qa_source_save_string(io, &event->selected_provider) ||
        !qa_source_save_string(io, &event->service_owner) || !qa_source_save_u64(io, &event->time_ns) ||
        !qa_source_save_i32(io, &event->index) || !qa_source_save_actor(io, &event->recipient)) return false;
    event->kind = (qa_application_equipment_event_kind)kind;
    size_t length = io->direction == QA_SOURCE_SAVE_WRITE && event->text ? strlen(event->text) : 0;
    if (!qa_source_save_count(io, &length, SIZE_MAX - 1)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (io->offset > io->input.size || length > io->input.size - io->offset)
            return application_fail(io->error, QA_ERROR_FORMAT, "Truncated gear event text");
        char *text = malloc(length + 1);
        if (!text) return application_fail(io->error, QA_ERROR_MEMORY, "Decoding gear event text");
        event->text = text;
        if (!qa_source_save_bytes(io, text, length)) return false;
        text[length] = 0;
        if (memchr(text, 0, length)) return application_fail(io->error, QA_ERROR_FORMAT, "Gear event text contains NUL");
    } else if (!qa_source_save_bytes(io, (void *)event->text, length)) return false;
    return valid(queue, event, io->error);
}

bool application_equipment_events_capture(const application_equipment_events *queue, qa_buffer *out,
    qa_error *error)
{
    if (!queue || !out || out->data || out->size || !qa_session_safe(queue->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear event capture requires its idle owner and empty output");
    const qa_application *app = queue->application;
    uint64_t first = qa_application_events_local_first(app), next = qa_application_events_next(app);
    size_t count = 0;
    for (uint64_t id = first; id < next; ++id) {
        qa_application_event_view output;
        qa_application_event_cursor event_cursor = {.id = id};
        while ( qa_application_event_read(app, &event_cursor, &output) && output.equipment)
            if (output.equipment_owner == queue->identity) ++count;
    }
    qa_source_save_io io = {0}; uint64_t generation = qa_application_protocol_events_generation(app);
    bool okay = qa_source_save_writer(&io, queue->session, error) && header(&io, &generation, &count);
    for (uint64_t id = first; okay && id < next; ++id) {
        qa_application_event_view output;
        qa_application_event_cursor event_cursor = {.id = id};
        while ( okay && qa_application_event_read(app, &event_cursor, &output) && output.equipment) {
            if (output.equipment_owner != queue->identity) continue;
            qa_application_equipment_event event = *output.equipment;
            okay = fields(&io, queue, &event);
        }
    }
    if (okay) okay = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!okay && (!error || error->code == QA_OK)) application_fail(error, QA_ERROR_FORMAT, "Invalid gear event checkpoint");
    return okay;
}

bool application_equipment_events_restore(application_equipment_events *queue, qa_bytes bytes, qa_error *error)
{
    if (!queue || !qa_session_safe(queue->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear event import requires its isolated owner");
    qa_source_save_io io = {0}; size_t count = 0; uint64_t generation = 0;
    bool okay = qa_source_save_reader(&io, queue->session, bytes, error) && header(&io, &generation, &count);
    for (size_t i = 0; okay && i < count; ++i) {
        qa_application_equipment_event event = {0};
        okay = fields(&io, queue, &event) && application_equipment_events_publish(queue, &event, error);
        free((void *)event.text);
    }
    if (okay) okay = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (!okay && (!error || error->code == QA_OK)) application_fail(error, QA_ERROR_FORMAT, "Invalid gear event import");
    return okay;
}

qa_actor_owner qa_application_equipment_events_owner(const qa_application *application)
{
    size_t count = application ? application_equipment_runtime_source_count(application->equipment_runtime) : 0;
    for (size_t i = 0; i < count; ++i) {
        application_equipment_runtime_source source;
        if (!application_equipment_runtime_source_at(application->equipment_runtime, i, &source, NULL)) return 0;
        if (source.gear && qa_application_equipment_event_source_current(application, source.gear_owner,
            source.selected_owner, source.service_owner)) return source.gear_owner;
    }
    return 0;
}
bool qa_application_equipment_event_source_current(const qa_application *application,
    qa_actor_owner provider, qa_actor_owner selected, qa_string_id service)
{
    qa_application_equipment_event event = {.provider = provider, .selected_provider = selected, .service_owner = service};
    return application && application_equipment_runtime_event_current(application->equipment_runtime, &event);
}

#include "equipment_events.h"
#include "equipment_runtime.h"
#include "qa/source_save.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct application_equipment_events {
    qa_session *session;
    qa_application_equipment_event *rows;
    size_t count, capacity;
    uint64_t generation;
};

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

bool application_equipment_events_create(qa_session *session, application_equipment_events **out,
    qa_error *error)
{
    if (!session || !out) return application_fail(error, QA_ERROR_ARGUMENT, "Gear event queue lacks its session");
    application_equipment_events *queue = calloc(1, sizeof(*queue));
    if (!queue) return application_fail(error, QA_ERROR_MEMORY, "Allocating gear event queue");
    queue->session = session; *out = queue; return true;
}

static void discard(application_equipment_events *queue)
{
    for (size_t i = 0; i < queue->count; ++i) free((void *)queue->rows[i].text);
    queue->count = 0;
}

void application_equipment_events_destroy(application_equipment_events *queue)
{
    if (!queue) return;
    discard(queue); free(queue->rows); free(queue);
}

bool application_equipment_events_publish(void *context,
    const application_equipment_source_event *source, qa_error *error)
{
    application_equipment_events *queue = context;
    if (!queue || !source) return application_fail(error, QA_ERROR_ARGUMENT, "Gear event producer is absent");
    qa_application_equipment_event event = {.provider = source->provider,
        .selected_provider = source->selected_provider, .service_owner = source->service_owner,
        .time_ns = source->time_ns, .index = source->index, .recipient = source->recipient, .text = source->text};
    switch (source->kind) {
    case APPLICATION_EQUIPMENT_CONFIGSTRING: event.kind = QA_APPLICATION_EQUIPMENT_CONFIGSTRING; break;
    case APPLICATION_EQUIPMENT_SERVER_COMMAND: event.kind = QA_APPLICATION_EQUIPMENT_SERVER_COMMAND; break;
    default: return application_fail(error, QA_ERROR_ARGUMENT, "Unknown gear source event");
    }
    if (!valid(queue, &event, error)) return false;
    if (event.recipient.registry && !qa_actors_get(qa_session_actors(queue->session), event.recipient))
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear command recipient retired before publication");
    size_t length = strlen(event.text);
    if (length == SIZE_MAX) return application_fail(error, QA_ERROR_MEMORY, "Gear event text overflows");
    char *text = malloc(length + 1);
    if (!text) return application_fail(error, QA_ERROR_MEMORY, "Retaining gear event text");
    memcpy(text, event.text, length + 1);
    if (queue->count == queue->capacity) {
        size_t capacity = queue->capacity ? queue->capacity : 32;
        if (queue->capacity > SIZE_MAX / 2) {
            free(text); return application_fail(error, QA_ERROR_MEMORY, "Gear event queue extent overflows");
        }
        if (queue->capacity) capacity *= 2;
        if (capacity > SIZE_MAX / sizeof(*queue->rows)) {
            free(text); return application_fail(error, QA_ERROR_MEMORY, "Gear event queue extent overflows");
        }
        void *rows = realloc(queue->rows, capacity * sizeof(*queue->rows));
        if (!rows) { free(text); return application_fail(error, QA_ERROR_MEMORY, "Growing gear event queue"); }
        queue->rows = rows; queue->capacity = capacity;
    }
    event.text = text; queue->rows[queue->count++] = event; return true;
}

size_t application_equipment_events_count(const application_equipment_events *queue)
{ return queue ? queue->count : 0; }
uint64_t application_equipment_events_generation(const application_equipment_events *queue)
{ return queue ? queue->generation : 0; }
bool application_equipment_events_at(const application_equipment_events *queue, size_t index,
    qa_application_equipment_event *out)
{
    if (!queue || !out || index >= queue->count) return false;
    *out = queue->rows[index]; return true;
}
bool application_equipment_events_clear_ready(const application_equipment_events *queue, qa_error *error)
{
    return !queue || queue->generation != UINT64_MAX ||
        application_fail(error, QA_ERROR_ARGUMENT, "Gear event consumption generation exhausted");
}
void application_equipment_events_clear(application_equipment_events *queue)
{
    if (queue) { discard(queue); ++queue->generation; }
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
    qa_source_save_io io = {0}; uint64_t generation = queue->generation; size_t count = queue->count;
    bool okay = qa_source_save_writer(&io, queue->session, error) && header(&io, &generation, &count);
    for (size_t i = 0; okay && i < count; ++i) {
        qa_application_equipment_event event = queue->rows[i];
        okay = valid(queue, &event, error) && fields(&io, queue, &event);
    }
    if (okay) okay = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    if (!okay && (!error || error->code == QA_OK)) application_fail(error, QA_ERROR_FORMAT, "Invalid gear event checkpoint");
    return okay;
}

bool application_equipment_events_restore(application_equipment_events *queue, qa_bytes bytes, qa_error *error)
{
    if (!queue || queue->count || !qa_session_safe(queue->session))
        return application_fail(error, QA_ERROR_ARGUMENT, "Gear event import requires its empty isolated owner");
    application_equipment_events candidate = {.session = queue->session}; qa_source_save_io io = {0};
    size_t count = 0;
    bool okay = qa_source_save_reader(&io, queue->session, bytes, error) && header(&io, &candidate.generation, &count);
    if (okay && count > (bytes.size - io.offset) / 64) okay = false;
    if (okay && count) {
        candidate.rows = calloc(count, sizeof(*candidate.rows)); candidate.capacity = count;
        if (!candidate.rows) okay = application_fail(error, QA_ERROR_MEMORY, "Allocating restored gear events");
    }
    for (size_t i = 0; okay && i < count; ++i) {
        candidate.count = i + 1;
        okay = fields(&io, &candidate, &candidate.rows[i]);
    }
    if (okay) okay = qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    if (okay) { free(queue->rows); *queue = candidate; }
    else { discard(&candidate); free(candidate.rows); }
    if (!okay && (!error || error->code == QA_OK)) application_fail(error, QA_ERROR_FORMAT, "Invalid gear event import");
    return okay;
}

size_t qa_application_equipment_event_count(const qa_application *application)
{ return application ? application_equipment_events_count(application_equipment_runtime_events(application->equipment_runtime)) : 0; }
uint64_t qa_application_equipment_events_generation(const qa_application *application)
{ return application ? application_equipment_events_generation(application_equipment_runtime_events(application->equipment_runtime)) : 0; }
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
bool qa_application_equipment_event_at(const qa_application *application, size_t index,
    qa_application_equipment_event *out, qa_error *error)
{
    qa_application_equipment_event event;
    if (!application || !out || !application_equipment_events_at(
        application_equipment_runtime_events(application->equipment_runtime), index, &event))
        return application_fail(error, QA_ERROR_NOT_FOUND, "Gear event index is outside its current runtime");
    if (!application_equipment_runtime_event_current(application->equipment_runtime, &event))
        return application_fail(error, QA_ERROR_FORMAT, "Gear event namespace differs from its retained source");
    *out = event; return true;
}

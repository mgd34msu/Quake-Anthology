#include "native_q2_protocol_resources.h"
#include "unified_events.h"
#include "qa/network_q2_messages.h"
#include "qa/native_host_q2_wire.h"
#include "qa/application_network_q2.h"
#include "event_stream.h"

typedef struct resource_capture {
    struct application_native_q2 *engine;
    application_native_q2_protocol_resources *out;
    size_t ordinal;
    qa_bytes payload;
    bool counting;
} resource_capture;

static bool retain_entity(resource_capture *capture, size_t offset, uint32_t source_slot,
    bool packed_sound, qa_error *error)
{
    application_native_q2_protocol_resources *out = capture->out;
    if (capture->counting) { ++out->reference_capacity; return true; }
    for (size_t i = 0; i < out->reference_count; ++i)
        if (out->references[i].offset == offset) return true;
    if (capture->payload.size < 2 || offset > capture->payload.size - 2)
        return application_fail(error, QA_ERROR_FORMAT, "Q2 emitted entity word exceeds its actual packet");
    qa_native_host_q2_entity actual;
    if (!qa_native_host_q2_wire_entity_import(capture->engine->provider->state.native.host,
        source_slot, &actual, error)) return false;
    uint32_t admitted_number;
    if (!qa_application_network_q2_entity_number(capture->engine->provider->application,
        capture->engine->provider->owner, actual.binding.actor, &admitted_number, error)) return false;
    out->references[out->reference_count++] = (qa_application_protocol_reference){
        .offset = offset, .actor = actual.binding.actor, .packed_sound = packed_sound};
    return true;
}

static bool capture_entities(resource_capture *capture, const qa_q2_server_record *record, qa_error *error)
{
    uintptr_t raw = (uintptr_t)record->raw.data, base = (uintptr_t)capture->payload.data;
    if (raw < base || raw - base > capture->payload.size ||
        record->raw.size > capture->payload.size - (size_t)(raw - base))
        return application_fail(error, QA_ERROR_FORMAT, "Q2 entity capture leaves its actual Source packet");
    size_t offset = (size_t)(raw - base);
    const qa_q2_server_event *event = &record->event;
    if (event->kind == QA_Q2_SVC_MUZZLEFLASH)
        return retain_entity(capture, offset + 1, event->data.muzzle.entity, false, error);
    if (event->kind == QA_Q2_SVC_SOUND && (event->data.sound.flags & 8u)) {
        uint8_t flags = event->data.sound.flags;
        offset += 2u + ((flags & 32u) ? 2u : 1u) + ((flags & 1u) != 0) +
            ((flags & 2u) != 0) + ((flags & 16u) != 0);
        return retain_entity(capture, offset, event->data.sound.entity, true, error);
    }
    if (event->kind != QA_Q2_SVC_TEMP_ENTITY) return true;
    const qa_q2_temp_entity *temporary = &event->data.temporary;
    for (size_t i = 0; i < temporary->field_count; ++i) {
        const qa_q2_temp_field *field = temporary->fields + i;
        bool entity = field->name == QA_Q2_TEMP_ENTITY1 || field->name == QA_Q2_TEMP_ENTITY2;
        bool actor_field = entity && temporary->type != QA_Q2_TE_STEAM && temporary->type != QA_Q2_TE_WIDOWBEAMOUT;
        if (actor_field && field->kind == QA_Q2_TEMP_INTEGER && field->value.integer >= 0 &&
            !retain_entity(capture, offset + 1u + field->offset,
                (uint32_t)field->value.integer, false, error)) return false;
    }
    return true;
}

static bool retain(resource_capture *capture, size_t ordinal, qa_native_host_resource_kind kind,
    uint32_t index, const char *name, qa_error *error)
{
    application_native_q2_protocol_resources *resources = capture->out;
    if (!name || index >= capture->engine->resource_limit[kind])
        return application_fail(error, QA_ERROR_FORMAT, "Q2 protocol resource leaves its actual Source table");
    if (capture->counting) { ++resources->capacity; return true; }
    qa_application_protocol_resource_reference row = {.record_ordinal = ordinal, .kind = kind, .source_index = index};
    size_t length = strlen(name);
    if (length == SIZE_MAX) return application_fail(error, QA_ERROR_MEMORY, "Q2 resource spelling extent overflows");
    char *owned = application_event_stream_alloc(capture->engine->provider->application, length + 1, 1, error);
    if (!owned) return false;
    memcpy(owned, name, length + 1); row.name = owned;
    bool found = false;
    if (*name && !application_unified_event_resource_lookup_receipt(capture->engine->provider->application,
        capture->engine->provider->owner, kind, name, row.resource_key, &row.resource_custody, &found, error)) {
        return false;
    }
    resources->rows[resources->count++] = row; return true;
}

static bool capture_record(void *context, const qa_q2_server_record *record, qa_error *error)
{
    resource_capture *capture = context;
    size_t ordinal = capture->ordinal++;
    if (!capture_entities(capture, record, error)) return false;
    const qa_q2_server_event *event = &record->event;
    if (event->kind == QA_Q2_SVC_SOUND || event->kind == QA_Q2_SVC_POI) {
        qa_native_host_resource_kind kind = event->kind == QA_Q2_SVC_SOUND ? QA_NATIVE_HOST_SOUND : QA_NATIVE_HOST_IMAGE;
        uint32_t index = event->kind == QA_Q2_SVC_SOUND ? event->data.sound.index : event->data.poi.image;
        if (event->kind == QA_Q2_SVC_POI && event->data.poi.time == UINT16_MAX) return true;
        if (index >= capture->engine->resource_limit[kind])
            return application_fail(error, QA_ERROR_FORMAT, "Q2 emitted resource index exceeds its Source namespace");
        const char *name = qa_strings_cstr(qa_session_strings(capture->engine->provider->application->session), capture->engine->configstrings[capture->engine->resource_base[kind] + index]);
        return retain(capture, ordinal, kind, index, name ? name : "", error);
    }
    if (event->kind == QA_Q2_SVC_CONFIGSTRING) {
        uint32_t slot = event->data.config.index;
        for (unsigned kind = 0; kind <= QA_NATIVE_HOST_IMAGE; ++kind) {
            uint32_t base = capture->engine->resource_base[kind], count = capture->engine->resource_limit[kind];
            if (slot >= base && slot - base < count)
                return retain(capture, ordinal, (qa_native_host_resource_kind)kind, slot - base, event->data.config.value, error);
        }
    }
    return true;
}

bool application_native_q2_protocol_resources_capture(struct application_native_q2 *engine,
    qa_bytes payload, const qa_application_protocol_reference *references, size_t reference_count,
    const qa_native_host_message *source, application_native_q2_protocol_resources *out, qa_error *error)
{
    if (!engine || !out || out->rows || out->count || out->capacity || out->references ||
        out->reference_count || out->reference_capacity || (reference_count && !references) ||
        reference_count > SIZE_MAX / sizeof(*references) ||
        (payload.size && !payload.data) || engine->profile == QA_NATIVE_Q2_CGAME_API2023)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 resource capture requires its actual Original GAME packet");
    qa_application *app = engine->provider->application;
    qa_q2_messages *decoder = engine->event_decoder;
    resource_capture capture = {.engine = engine, .out = out, .payload = payload, .counting = true};
    qa_q2_messages_reset(decoder);
    if (!qa_q2_messages_read(decoder, payload, capture_record, &capture, error)) return false;
    size_t provided = source ? source->reference_count : reference_count;
    if (out->reference_capacity > SIZE_MAX / sizeof(*out->references) - provided)
        return application_fail(error, QA_ERROR_MEMORY, "Q2 emitted entity receipt extent overflows");
    out->reference_capacity += provided;
    if (out->reference_capacity) {
        out->references = application_event_stream_alloc(app,
            out->reference_capacity * sizeof(*out->references), _Alignof(qa_application_protocol_reference), error);
        if (!out->references) return false;
        for (size_t i = 0; i < provided; ++i) {
            if (source) out->references[i] = (qa_application_protocol_reference){
                .offset = source->references[i].offset, .actor = source->references[i].actor};
            else out->references[i] = references[i];
        }
        out->reference_count = provided;
    }
    if (out->capacity) {
        out->rows = application_event_stream_alloc(app, out->capacity * sizeof(*out->rows),
            _Alignof(qa_application_protocol_resource_reference), error);
        if (!out->rows) return false;
    }
    capture.ordinal = 0; capture.counting = false;
    qa_q2_messages_reset(decoder);
    return qa_q2_messages_read(decoder, payload, capture_record, &capture, error);
}

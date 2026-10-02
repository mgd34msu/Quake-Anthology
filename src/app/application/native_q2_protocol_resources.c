#include "native_q2_protocol_resources.h"
#include "unified_events.h"
#include "qa/network_q2_messages.h"
#include "qa/native_host_q2_wire.h"
#include "qa/application_network_q2.h"

typedef struct resource_capture {
    struct application_native_q2 *engine;
    application_native_q2_protocol_resources *out;
    size_t ordinal;
    qa_bytes payload;
} resource_capture;

void application_native_q2_protocol_resources_dispose(application_native_q2_protocol_resources *resources)
{
    if (!resources) return;
    for (size_t i = 0; i < resources->count; ++i) free((void *)resources->rows[i].name);
    free(resources->rows); free(resources->references);
    *resources = (application_native_q2_protocol_resources){0};
}

static bool retain_entity(resource_capture *capture, size_t offset, uint32_t source_slot,
    bool packed_sound, qa_error *error)
{
    application_native_q2_protocol_resources *out = capture->out;
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
    if (out->reference_count == out->reference_capacity) {
        size_t capacity = out->reference_capacity ? out->reference_capacity * 2 : 8;
        if (capacity <= out->reference_capacity || capacity > SIZE_MAX / sizeof(*out->references))
            return application_fail(error, QA_ERROR_MEMORY, "Q2 emitted entity receipt extent overflows");
        void *references = realloc(out->references, capacity * sizeof(*out->references));
        if (!references) return application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 primitive Source entity receipt");
        out->references = references; out->reference_capacity = capacity;
    }
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
    offset += 2;
    for (size_t i = 0; i < temporary->field_count; ++i) {
        const qa_q2_temp_field *field = temporary->fields + i;
        bool entity = field->name == QA_Q2_TEMP_ENTITY1 || field->name == QA_Q2_TEMP_ENTITY2;
        bool actor_field = entity && temporary->type != QA_Q2_TE_STEAM && temporary->type != QA_Q2_TE_WIDOWBEAMOUT;
        if (actor_field && field->value.integer >= 0 &&
            !retain_entity(capture, offset, (uint32_t)field->value.integer, false, error)) return false;
        if (field->kind == QA_Q2_TEMP_VECTOR)
            offset += field->name == QA_Q2_TEMP_DIRECTION ? 1u :
                capture->engine->profile == QA_NATIVE_Q2_GAME_API3 ? 6u : 12u;
        else offset += entity ? 2u : field->name == QA_Q2_TEMP_TIME ? 4u : 1u;
    }
    return true;
}

static bool retain(resource_capture *capture, size_t ordinal, qa_native_host_resource_kind kind,
    uint32_t index, const char *name, qa_error *error)
{
    application_native_q2_protocol_resources *resources = capture->out;
    if (!name || index >= capture->engine->resource_limit[kind])
        return application_fail(error, QA_ERROR_FORMAT, "Q2 protocol resource leaves its actual Source table");
    qa_application_protocol_resource_reference row = {.record_ordinal = ordinal, .kind = kind, .source_index = index};
    size_t length = strlen(name);
    if (length == SIZE_MAX) return application_fail(error, QA_ERROR_MEMORY, "Q2 resource spelling extent overflows");
    char *owned = malloc(length + 1);
    if (!owned) return application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 emitted resource spelling");
    memcpy(owned, name, length + 1); row.name = owned;
    bool found = false;
    if (*name && !application_unified_event_resource_lookup_kind(capture->engine->provider->application,
        capture->engine->provider->owner, kind, name, row.resource_key, &found, error)) { free(owned); return false; }
    if (resources->count == resources->capacity) {
        size_t capacity = resources->capacity ? resources->capacity * 2 : 8;
        if (capacity <= resources->capacity || capacity > SIZE_MAX / sizeof(*resources->rows)) {
            free(owned); return application_fail(error, QA_ERROR_MEMORY, "Q2 emitted resource receipt extent overflows");
        }
        void *rows = realloc(resources->rows, capacity * sizeof(*resources->rows));
        if (!rows) { free(owned); return application_fail(error, QA_ERROR_MEMORY, "Retaining Q2 emitted resource receipts"); }
        resources->rows = rows; resources->capacity = capacity;
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
        const char *name = capture->engine->configstrings[capture->engine->resource_base[kind] + index];
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
    application_native_q2_protocol_resources *out, qa_error *error)
{
    if (!engine || !out || out->rows || out->count || out->capacity || out->references ||
        out->reference_count || out->reference_capacity || (reference_count && !references) ||
        reference_count > SIZE_MAX / sizeof(*references) ||
        (payload.size && !payload.data) || engine->profile == QA_NATIVE_Q2_CGAME_API2023)
        return application_fail(error, QA_ERROR_ARGUMENT, "Q2 resource capture requires its actual Original GAME packet");
    if (reference_count) {
        out->references = malloc(reference_count * sizeof(*references));
        if (!out->references) return application_fail(error, QA_ERROR_MEMORY, "Retaining exact Source WriteEntity receipts");
        memcpy(out->references, references, reference_count * sizeof(*references));
        out->reference_count = out->reference_capacity = reference_count;
    }
    qa_net_protocol_id protocol = {.kind = engine->profile == QA_NATIVE_Q2_GAME_API3 ? QA_NET_Q2_34 : QA_NET_Q2KEX_2023};
    qa_q2_message_options options = {.config_strings = engine->configstring_count, .inventory_slots = 256,
        .native_api2023 = engine->profile == QA_NATIVE_Q2_GAME_API2023};
    qa_q2_messages *decoder = NULL;
    resource_capture capture = {.engine = engine, .out = out, .payload = payload};
    bool ok = qa_q2_messages_create(protocol, &options, &decoder, error) &&
        qa_q2_messages_read(decoder, payload, capture_record, &capture, error);
    qa_q2_messages_destroy(decoder);
    if (!ok) application_native_q2_protocol_resources_dispose(out);
    return ok;
}

#include "session_internal.h"

bool q2_fail(qa_error *error, qa_status status, const char *message)
{ qa_error_set(error, status, 0, "%s", message); return false; }

static bool buffer_copy(qa_unified_frame_lease *lease, qa_bytes from, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || (from.size && !from.data))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Q2 retained bytes require an empty output");
    if (!from.size) return true;
    uint8_t *bytes = lease ? qa_unified_frame_lease_alloc(lease,from.size,1,
        _Alignof(max_align_t),error) : malloc(from.size);
    if (!bytes) return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 connection bytes");
    memcpy(bytes, from.data, from.size); *out = (qa_buffer){bytes, from.size}; return true;
}

void q2_game_state_free(q2_game_state *state)
{
    if (!state) return;
    if (state->configs)
        for (size_t i = 0; i < state->view.config_count; ++i) free((void *)state->configs[i].value);
    free(state->configs); free(state->baselines); memset(state, 0, sizeof(*state));
}

bool q2_game_state_clone(const qa_q2_game_state *from, q2_game_state *out, qa_error *error)
{
    if (!from || !out || out->configs || out->baselines || out->view.config_count ||
        from->config_count > UINT16_MAX || (from->config_count && !from->configs) ||
        from->baselines.count > UINT16_MAX || (from->baselines.count && !from->baselines.data) ||
        from->data.client_count > QA_Q2_MAX_SEATS ||
        !memchr(from->data.gamedir, 0, sizeof(from->data.gamedir)) ||
        !memchr(from->data.levelname, 0, sizeof(from->data.levelname)))
        return q2_fail(error, QA_ERROR_ARGUMENT, "Invalid Q2 source game-state snapshot");
    q2_game_state copy = {.view = *from};
    copy.view.configs = NULL; copy.view.config_count = 0; copy.view.baselines = (qa_q2_entity_span){0};
    if (from->config_count) {
        copy.configs = calloc(from->config_count, sizeof(*copy.configs));
        if (!copy.configs) goto memory;
        copy.view.configs = copy.configs;
    }
    for (size_t i = 0; i < from->config_count; ++i) {
        const qa_q2_config_entry *entry = from->configs + i;
        if (!entry->value || (i && entry->index <= from->configs[i - 1].index)) goto invalid;
        size_t length = strlen(entry->value);
        if (length == SIZE_MAX) goto invalid;
        char *text = malloc(length + 1);
        if (!text) goto memory;
        memcpy(text, entry->value, length + 1);
        copy.configs[i] = (qa_q2_config_entry){entry->index, text}; ++copy.view.config_count;
    }
    if (from->baselines.count) {
        copy.baselines = malloc(from->baselines.count * sizeof(*copy.baselines));
        if (!copy.baselines) goto memory;
        uint32_t previous = 0;
        for (size_t i = 0; i < from->baselines.count; ++i) {
            const qa_q2_entity *entity = from->baselines.data + i;
            if (!entity->number || entity->number > UINT16_MAX || entity->number <= previous) goto invalid;
            copy.baselines[i] = *entity; previous = entity->number;
        }
        copy.view.baselines = (qa_q2_entity_span){copy.baselines, from->baselines.count};
    }
    *out = copy; return true;
memory:
    q2_game_state_free(&copy); return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 source signon snapshot");
invalid:
    q2_game_state_free(&copy); return q2_fail(error, QA_ERROR_FORMAT, "Q2 source signon records are not unique and ordered");
}

static void record_free(q2_owned_record *owner)
{
    if (!owner->payload_pooled) {
        qa_buffer_free(&owner->raw); qa_buffer_free(&owner->text); qa_buffer_free(&owner->values);
    }
    if (owner->frame) { qa_q2_frame_free(owner->frame); if (!owner->frame_pooled) free(owner->frame); }
    memset(owner, 0, sizeof(*owner));
}

void q2_records_free(q2_records *batch)
{
    if (!batch) return;
    for (size_t i = 0; i < batch->count; ++i) record_free(batch->owned + i);
    if (batch->lease) qa_unified_frame_lease_release(batch->lease);
    else { free(batch->records); free(batch->owned); }
    memset(batch, 0, sizeof(*batch));
}

static bool text_copy(qa_unified_frame_lease *lease, const char *text, qa_buffer *out, qa_error *error)
{
    if (!text) return q2_fail(error, QA_ERROR_FORMAT, "Q2 decoded text is absent");
    size_t length = strlen(text);
    return length != SIZE_MAX && buffer_copy(lease,(qa_bytes){(const uint8_t *)text, length + 1}, out, error);
}

bool q2_record_retain(void *context, const qa_q2_server_record *record, qa_error *error)
{
    q2_records *batch = context;
    if (!batch || !record || record->seat > QA_Q2_MAX_SEATS)
        return q2_fail(error, QA_ERROR_ARGUMENT, "Invalid Q2 decoded record ownership");
    if (batch->count == batch->capacity) {
        size_t capacity = batch->capacity ? batch->capacity * 2 : 32;
        if (capacity < batch->capacity || capacity > SIZE_MAX / sizeof(*batch->records) ||
            capacity > SIZE_MAX / sizeof(*batch->owned))
            return q2_fail(error, QA_ERROR_MEMORY, "Q2 decoded record inventory overflows");
        qa_q2_server_record *records = batch->lease ? qa_unified_frame_lease_alloc(batch->lease,
            capacity,sizeof(*records),_Alignof(qa_q2_server_record),error) : calloc(capacity, sizeof(*records));
        q2_owned_record *owned = batch->lease ? qa_unified_frame_lease_alloc(batch->lease,
            capacity,sizeof(*owned),_Alignof(q2_owned_record),error) : calloc(capacity, sizeof(*owned));
        if (!records || !owned) {
            if (!batch->lease) { free(records); free(owned); }
            return q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 decoded records");
        }
        if (batch->count) {
            memcpy(records, batch->records, batch->count * sizeof(*records));
            memcpy(owned, batch->owned, batch->count * sizeof(*owned));
        }
        if (!batch->lease) { free(batch->records); free(batch->owned); }
        batch->records = records; batch->owned = owned; batch->capacity = capacity;
    }
    qa_q2_server_record copy = *record; q2_owned_record owner = {.payload_pooled=batch->lease!=NULL};
    if (!buffer_copy(batch->lease,record->raw, &owner.raw, error)) return false;
    copy.raw = (qa_bytes){owner.raw.data, owner.raw.size};
    switch (copy.event.kind) {
    case QA_Q2_SVC_PRINT: case QA_Q2_SVC_CENTERPRINT: case QA_Q2_SVC_COMMAND:
    case QA_Q2_SVC_LAYOUT: case QA_Q2_SVC_ACHIEVEMENT:
        if (!text_copy(batch->lease,record->event.data.print.text, &owner.text, error)) goto failure;
        copy.event.data.print.text = (const char *)owner.text.data; break;
    case QA_Q2_SVC_CONFIGSTRING:
        if (!text_copy(batch->lease,record->event.data.config.value, &owner.text, error)) goto failure;
        copy.event.data.config.value = (const char *)owner.text.data; break;
    case QA_Q2_SVC_FRAME:
        owner.frame_pooled=record->event.data.frame && record->event.data.frame->lease;
        owner.frame = owner.frame_pooled?qa_unified_frame_lease_alloc(record->event.data.frame->lease,
            1,sizeof(*owner.frame),_Alignof(qa_q2_wire_frame),error):calloc(1, sizeof(*owner.frame));
        if (!owner.frame) { q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 decoded frame"); goto failure; }
        if (!qa_q2_frame_clone(record->event.data.frame, owner.frame, error)) goto failure;
        copy.event.data.frame = owner.frame; break;
    case QA_Q2_SVC_INVENTORY: {
        size_t count = record->event.data.inventory.count;
        if (count > SIZE_MAX / sizeof(int16_t) ||
            !buffer_copy(batch->lease,(qa_bytes){(const uint8_t *)record->event.data.inventory.counts,
                count * sizeof(int16_t)}, &owner.values, error)) goto failure;
        copy.event.data.inventory.counts = (const int16_t *)owner.values.data; break;
    }
    case QA_Q2_SVC_DOWNLOAD:
        if (!buffer_copy(batch->lease,record->event.data.download.bytes, &owner.values, error)) goto failure;
        copy.event.data.download.bytes = (qa_bytes){owner.values.data, owner.values.size}; break;
    case QA_Q2_SVC_TEMP_ENTITY:
        if (!buffer_copy(batch->lease,record->event.data.temporary.raw, &owner.values, error)) goto failure;
        copy.event.data.temporary.raw = (qa_bytes){owner.values.data, owner.values.size}; break;
    case QA_Q2_SVC_PRIVATE:
        if (!text_copy(batch->lease,record->event.data.private_message.name, &owner.text, error) ||
            !buffer_copy(batch->lease,record->event.data.private_message.payload, &owner.values, error)) goto failure;
        copy.event.data.private_message.name = (const char *)owner.text.data;
        copy.event.data.private_message.payload = (qa_bytes){owner.values.data, owner.values.size}; break;
    default: break;
    }
    batch->records[batch->count] = copy; batch->owned[batch->count] = owner; ++batch->count; return true;
failure:
    record_free(&owner); return false;
}

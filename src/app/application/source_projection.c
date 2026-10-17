#include "source_projection.h"
#include "internal.h"
#include "qa/binary.h"
#include <limits.h>

typedef struct projection_record {
    const application_source_projection *source;
    uint64_t address;
    qa_q3_abi_record abi;
} projection_record;

static bool table(const application_source_projection *source,
    qa_q3_host_game_data *out, qa_error *error)
{
    return (qa_q3_host_game_data_read(source->host, out) && out->entities_address &&
        out->entity_stride == source->entity_stride) ||
        application_fail(error, QA_ERROR_FORMAT, "Source projection differs from its located entity table");
}

bool application_source_projection_pointer(const application_source_projection *source,
    uint32_t slot, uint64_t *out, qa_error *error)
{
    qa_q3_host_game_data data;
    if (!table(source, &data, error)) return false;
    uint64_t offset = (uint64_t)slot * data.entity_stride;
    if (slot >= data.entity_count || data.entities_address > UINT64_MAX - offset)
        return application_fail(error, QA_ERROR_FORMAT, "Source projection leaves its located entity table");
    *out = data.entities_address + offset;
    return true;
}

bool application_source_projection_slot(const application_source_projection *source,
    uint64_t address, uint32_t *out, qa_error *error)
{
    qa_q3_host_game_data data;
    if (!table(source, &data, error)) return false;
    if (address < data.entities_address || (address - data.entities_address) % data.entity_stride)
        return application_fail(error, QA_ERROR_FORMAT, "Source allocator returned a noncanonical entity pointer");
    uint64_t slot = (address - data.entities_address) / data.entity_stride;
    if (slot >= data.entity_count || slot >= QA_Q3_ENTITY_WORLD)
        return application_fail(error, QA_ERROR_FORMAT, "Source allocator returned an entity outside its located table");
    *out = (uint32_t)slot;
    return true;
}

static bool record_write(void *context, size_t offset, qa_bytes bytes, qa_error *error)
{
    projection_record *record = context;
    return record->source->write(record->source->context, record->address + offset, bytes, false, error);
}

static bool record_open(const application_source_projection *source, uint32_t slot,
    projection_record *record, qa_error *error)
{
    *record = (projection_record){.source = source};
    if (!application_source_projection_pointer(source, slot, &record->address, error) ||
        !source->span(source->context, record->address, source->entity_stride,
            &record->abi.bytes, error)) return false;
    record->abi.abi = source->abi;
    record->abi.context = record;
    record->abi.write = record_write;
    return true;
}

static bool record_word(projection_record *record, uint32_t offset,
    int32_t value, qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)value);
    return record->source->write(record->source->context, record->address + offset,
        (qa_bytes){bytes, sizeof(bytes)}, true, error);
}

bool application_source_projection_borrow(const application_source_projection *source,
    const qa_qvm_call *call, qa_actor_id actor, uint32_t *out, bool *created, qa_error *error)
{
    *created = !qa_q3_host_actor_slot(source->host, actor, out, NULL);
    if (!*created) return true;
    int32_t address;
    if (!source->invoke(source->context, call, source->allocate, NULL, 0, &address, error) ||
        !application_source_projection_slot(source, (uint32_t)address, out, error)) return false;
    qa_q3_host_game_data data; projection_record record; qa_actor_id previous;
    if (!table(source, &data, error) || !record_open(source, *out, &record, error) ||
        !qa_q3_host_actor(source->host, *out, false, &previous, error)) return false;
    if (*out < data.client_count || !qa_load_i32le(record.abi.bytes.data + source->inuse) || previous.registry)
        return application_fail(error, QA_ERROR_FORMAT, "Source allocator returned an occupied, client or inactive row");
    return qa_q3_host_bind_actor(source->host, *out, actor, true, error);
}

bool application_source_projection_publish(const application_source_projection *source,
    uint32_t slot, const qa_body_state *body, const int32_t *health,
    uint64_t time_ns, qa_error *error)
{
    projection_record record;
    if (!record_open(source, slot, &record, error)) return false;
    if (body) {
        qa_q3_entity entity; qa_qvm_entity_shared shared;
        if (!qa_q3_abi_read_entity(&record.abi, 0, true, &entity, error) ||
            !qa_q3_abi_read_shared_entity(&record.abi, 0, &shared, error)) return false;
        shared.origin = body->origin; shared.angles = body->angles;
        shared.local_bounds = body->bounds;
        shared.absolute_bounds = (qa_bounds){qa_vec_add(body->origin, body->bounds.mins),
            qa_vec_add(body->origin, body->bounds.maxs)};
        entity.number = (int32_t)slot;
        entity.pos.time = (int32_t)(time_ns / UINT64_C(1000000));
        entity.pos.base[0] = entity.origin[0] = body->origin.x;
        entity.pos.base[1] = entity.origin[1] = body->origin.y;
        entity.pos.base[2] = entity.origin[2] = body->origin.z;
        entity.pos.delta[0] = body->velocity.x; entity.pos.delta[1] = body->velocity.y;
        entity.pos.delta[2] = body->velocity.z;
        entity.angles[0] = body->angles.x; entity.angles[1] = body->angles.y;
        entity.angles[2] = body->angles.z;
        if (!qa_q3_abi_write_entity(&record.abi, 0, true, &entity, error) ||
            !qa_q3_abi_write_shared_entity(&record.abi, 0, &shared, error)) return false;
    }
    return (!health || record_word(&record, source->health, *health, error)) &&
        (!source->takedamage || record_word(&record, source->takedamage, source->can_take_damage, error));
}

bool application_source_projection_keep(const application_source_projection *source,
    uint32_t slot, qa_error *error)
{
    projection_record record; qa_q3_entity entity;
    if (!record_open(source, slot, &record, error) ||
        !qa_q3_abi_read_entity(&record.abi, 0, true, &entity, error)) return false;
    entity.number = (int32_t)slot;
    return qa_q3_abi_write_entity(&record.abi, 0, true, &entity, error) &&
        record_word(&record, source->inuse, 1, error);
}

bool application_source_projection_release(const application_source_projection *source,
    const qa_qvm_call *call, uint32_t slot, qa_error *error)
{
    projection_record record;
    if (!record_open(source, slot, &record, error)) return false;
    if (!qa_load_i32le(record.abi.bytes.data + source->inuse)) return true;
    if (record.address > INT32_MAX)
        return application_fail(error, QA_ERROR_FORMAT, "Source release pointer exceeds its callback word");
    int32_t pointer = (int32_t)record.address, result;
    return source->invoke(source->context, call, source->free, &pointer, 1, &result, error);
}

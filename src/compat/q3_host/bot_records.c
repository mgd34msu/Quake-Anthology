#include "bot_records.h"

static qa_vec3 vector(const uint8_t *bytes)
{
    return (qa_vec3){qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8)};
}

static void put_float(uint8_t *bytes, float value)
{
    qa_store_u32le(bytes, (uint32_t)q3_float_bits(value));
}

static void put_vector(uint8_t *bytes, qa_vec3 value)
{
    put_float(bytes, value.x); put_float(bytes + 4, value.y); put_float(bytes + 8, value.z);
}

static bool component_address(const q3_bot_memory *memory, unsigned component,
                                uint64_t *out, qa_error *error)
{
    if (!memory->address || component > 2 || memory->address > UINT64_MAX - component * 4u)
        return q3_fail(error, QA_ERROR_ARGUMENT, component, "Q3 bot vector exceeds allocation");
    *out = memory->address + component * 4u; return true;
}

bool q3_bot_vector_read(void *context, unsigned component, float *out, qa_error *error)
{
    q3_bot_memory *memory = context; uint64_t address; uint8_t bytes[4];
    if (!component_address(memory, component, &address, error) ||
        !q3_read(memory->call, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_f32le(bytes); return true;
}

bool q3_bot_vector_write(void *context, unsigned component, float value, qa_error *error)
{
    q3_bot_memory *memory = context; uint64_t address;
    return component_address(memory, component, &address, error) &&
        q3_write_float(memory->call, address, value, error);
}

bool q3_bot_vector_admit(void *context, unsigned component, qa_error *error)
{
    q3_bot_memory *memory = context; uint64_t address; q3_record record;
    return component_address(memory, component, &address, error) &&
        q3_record_open(memory->call, address, 4, &record, error);
}

bool q3_bot_inventory_read(void *context, int32_t index, int32_t *out, qa_error *error)
{
    q3_bot_memory *memory = context;
    int64_t displacement = (int64_t)index * 4;
    if (!memory->address ||
        (displacement < 0 ? memory->address < (uint64_t)-displacement :
                            memory->address > UINT64_MAX - (uint64_t)displacement))
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 bot inventory index exceeds allocation");
    uint64_t address = displacement < 0 ? memory->address - (uint64_t)-displacement :
                                          memory->address + (uint64_t)displacement;
    uint8_t bytes[4];
    if (!q3_read(memory->call, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_i32le(bytes); return true;
}

bool q3_bot_goal_read(q3_call *call, uint64_t address, qa_bot_goal *out, qa_error *error)
{
    uint8_t bytes[56];
    if (!q3_read(call, address, bytes, sizeof(bytes), error)) return false;
    *out = (qa_bot_goal){.origin = vector(bytes), .area = qa_load_i32le(bytes + 12),
        .mins = vector(bytes + 16), .maxs = vector(bytes + 28), .entity = qa_load_i32le(bytes + 40),
        .number = qa_load_i32le(bytes + 44), .flags = qa_load_i32le(bytes + 48),
        .item_info = qa_load_i32le(bytes + 52)};
    if(out->entity>0 && !q3_bot_entity_number(call,out->entity,&out->entity,error)) return false;
    return true;
}

bool q3_bot_goal_copy(q3_call *call, uint64_t address, const qa_bot_goal *goal, qa_error *error)
{
    uint8_t bytes[56];
    int32_t entity;
    entity=goal->entity;
    if(entity && !q3_bot_source_entity(call,entity,&entity,error)) return false;
    put_vector(bytes, goal->origin); qa_store_u32le(bytes + 12, (uint32_t)goal->area);
    put_vector(bytes + 16, goal->mins); put_vector(bytes + 28, goal->maxs);
    qa_store_u32le(bytes + 40, (uint32_t)entity);
    qa_store_u32le(bytes + 44, (uint32_t)goal->number);
    qa_store_u32le(bytes + 48, (uint32_t)goal->flags);
    qa_store_u32le(bytes + 52, (uint32_t)goal->item_info);
    return q3_write(call, address, (qa_bytes){bytes, sizeof(bytes)}, error);
}

bool q3_bot_goal_fields(q3_call *call, uint64_t address, const qa_bot_goal *goal,
                          q3_goal_fields fields, qa_error *error)
{
    q3_record admitted;
    int32_t entity;
    entity=goal->entity;
    if(entity && !q3_bot_source_entity(call,entity,&entity,error)) return false;
    if (!q3_record_open(call, address, 56, &admitted, error) ||
        !q3_write_word(call, address + 12, (uint32_t)goal->area, error) ||
        !q3_write_vector(call, address, goal->origin, error) ||
        !q3_write_word(call, address + 40, (uint32_t)entity, error) ||
        !q3_write_vector(call, address + 16, goal->mins, error) ||
        !q3_write_vector(call, address + 28, goal->maxs, error)) return false;
    if (fields == Q3_GOAL_LOCATION) return true;
    if (!q3_write_word(call, address + 44, (uint32_t)goal->number, error) ||
        !q3_write_word(call, address + 48, (uint32_t)goal->flags, error)) return false;
    return fields != Q3_GOAL_FULL || q3_write_word(call, address + 52, (uint32_t)goal->item_info, error);
}

bool q3_bot_weapon_copy(q3_call *call, uint64_t address, const qa_bot_weapon_info *weapon,
                          const qa_bot_projectile_info *projectile, qa_error *error)
{
    uint8_t bytes[552] = {0}, *p = bytes + 344;
    qa_store_u32le(bytes, weapon->valid);
    qa_store_u32le(bytes + 4, (uint32_t)weapon->number);
    memcpy(bytes + 8, weapon->name, 80); memcpy(bytes + 88, weapon->model, 80);
    qa_store_u32le(bytes + 168, (uint32_t)weapon->level);
    qa_store_u32le(bytes + 172, (uint32_t)weapon->weapon_inventory);
    qa_store_u32le(bytes + 176, (uint32_t)weapon->flags);
    memcpy(bytes + 180, weapon->projectile, 80);
    qa_store_u32le(bytes + 260, (uint32_t)weapon->projectile_count);
    put_float(bytes + 264, weapon->horizontal_spread); put_float(bytes + 268, weapon->vertical_spread);
    put_float(bytes + 272, weapon->speed); put_float(bytes + 276, weapon->acceleration);
    put_vector(bytes + 280, weapon->recoil); put_vector(bytes + 292, weapon->offset);
    put_vector(bytes + 304, weapon->angle_offset); put_float(bytes + 316, weapon->extra_z_velocity);
    qa_store_u32le(bytes + 320, (uint32_t)weapon->ammo_amount);
    qa_store_u32le(bytes + 324, (uint32_t)weapon->ammo_inventory);
    put_float(bytes + 328, weapon->activate); put_float(bytes + 332, weapon->reload);
    put_float(bytes + 336, weapon->spin_up); put_float(bytes + 340, weapon->spin_down);
    memcpy(p, projectile->name, 80); memcpy(p + 80, projectile->model, 80);
    qa_store_u32le(p + 160, (uint32_t)projectile->flags); put_float(p + 164, projectile->gravity);
    qa_store_u32le(p + 168, (uint32_t)projectile->damage); put_float(p + 172, projectile->radius);
    qa_store_u32le(p + 176, (uint32_t)projectile->visible_damage);
    qa_store_u32le(p + 180, (uint32_t)projectile->damage_type);
    qa_store_u32le(p + 184, (uint32_t)projectile->health_increase);
    put_float(p + 188, projectile->push); put_float(p + 192, projectile->detonation);
    put_float(p + 196, projectile->bounce); put_float(p + 200, projectile->bounce_friction);
    put_float(p + 204, projectile->bounce_stop);
    return q3_write(call, address, (qa_bytes){bytes, sizeof(bytes)}, error);
}

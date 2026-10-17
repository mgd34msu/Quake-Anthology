#include "guest_q3_gear_private.h"

bool q3gear_fail(qa_error *error, qa_status code, const char *message)
{
    qa_error_set(error, code, 0, "%s", message);
    return false;
}

bool q3gear_current(application_q3_gear *gear, qa_error *error)
{
    return (gear && gear->options.current(gear->options.context)) ||
        q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear lifetime has retired");
}

bool q3gear_word(application_q3_gear *gear, uint32_t address, int32_t *out, qa_error *error)
{
    uint8_t bytes[4];
    if (!qa_qvm_read(gear->vm, address, bytes, sizeof(bytes), error)) return false;
    *out = qa_load_i32le(bytes); return true;
}

bool q3gear_store(application_q3_gear *gear, uint32_t address, int32_t word, qa_error *error)
{
    uint8_t bytes[4]; qa_store_u32le(bytes, (uint32_t)word);
    return qa_qvm_write(gear->vm, address, (qa_bytes){bytes, sizeof(bytes)}, error);
}

bool q3gear_vector(application_q3_gear *gear, uint32_t address, qa_vec3 *out, qa_error *error)
{
    uint8_t bytes[12];
    if (!qa_qvm_read(gear->vm, address, bytes, sizeof(bytes), error)) return false;
    qa_vec3 value = {qa_load_f32le(bytes), qa_load_f32le(bytes + 4), qa_load_f32le(bytes + 8)};
    if (!qa_vec_finite(value))
        return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear vector is not finite");
    *out = value; return true;
}

bool q3gear_vector_store(application_q3_gear *gear, uint32_t address, qa_vec3 value, qa_error *error)
{
    if (!qa_vec_finite(value))
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear vector is not finite");
    uint8_t bytes[12]; float values[] = {value.x, value.y, value.z};
    for (size_t i = 0; i < 3; ++i) {
        uint32_t bits; memcpy(&bits, &values[i], sizeof(bits)); qa_store_u32le(bytes + 4*i, bits);
    }
    return qa_qvm_write(gear->vm, address, (qa_bytes){bytes, sizeof(bytes)}, error);
}

bool q3gear_layout(application_q3_gear *gear, qa_q3_host_game_data *out, qa_error *error)
{
    qa_q3_host_game_data layout;
    if (!qa_q3_host_game_data_read(gear->host, &layout) ||
        layout.entity_stride != gear->definition->entity_stride ||
        layout.client_stride != gear->definition->client_stride || layout.client_count != 64 ||
        layout.entity_count < 64 || layout.entity_count > 1024 ||
        layout.entities_address > UINT32_MAX || layout.clients_address > UINT32_MAX ||
        layout.entities_address + (uint64_t)layout.entity_count * layout.entity_stride > qa_qvm_memory_size(gear->vm) ||
        layout.clients_address + (uint64_t)layout.client_count * layout.client_stride > qa_qvm_memory_size(gear->vm))
        return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear located another entity/client layout");
    *out = layout; return true;
}

bool q3gear_slot(application_q3_gear *gear, uint32_t pointer, uint32_t *out, qa_error *error)
{
    qa_q3_host_game_data layout;
    if (!q3gear_layout(gear, &layout, error)) return false;
    if (pointer < layout.entities_address ||
        (pointer - layout.entities_address) % layout.entity_stride ||
        (pointer - layout.entities_address) / layout.entity_stride >= layout.entity_count)
        return q3gear_fail(error, QA_ERROR_ARGUMENT, "Separate QVM gear entity pointer is outside its located records");
    *out = (uint32_t)((pointer - layout.entities_address) / layout.entity_stride); return true;
}

size_t q3gear_pointer_row(application_q3_gear *gear, uint32_t pointer, bool bindings_only)
{
    qa_id_index_cursor cursor = qa_id_index_begin(&gear->pointers, 0, pointer);
    size_t row, first = SIZE_MAX;
    /* Duplicate pointers retain the old B0,T0,B1,T1 source-row priority. */
    while (qa_id_index_next(&gear->pointers, &cursor, &row))
        if ((!bindings_only || !(row & 1)) && row < first) first = row;
    return first;
}

void q3gear_binding_write(application_q3_gear *gear, uint32_t slot, q3gear_binding row)
{
    q3gear_binding previous = gear->bindings[slot];
    size_t ordinal = (size_t)slot * 2;
    if (previous.actor.registry && (!row.actor.registry || previous.pointer != row.pointer))
        qa_id_index_remove_value(&gear->pointers, 0, previous.pointer, ordinal);
    gear->bindings[slot] = row;
    if (row.actor.registry && (!previous.actor.registry || previous.pointer != row.pointer))
        qa_id_index_insert(&gear->pointers, 0, row.pointer, ordinal);
}

void q3gear_tether_write(application_q3_gear *gear, uint32_t slot, q3gear_tether row)
{
    q3gear_tether previous = gear->tethers[slot];
    size_t ordinal = (size_t)slot * 2 + 1;
    if (previous.actor.registry && (!row.actor.registry || previous.hook != row.hook))
        qa_id_index_remove_value(&gear->pointers, 0, previous.hook, ordinal);
    gear->tethers[slot] = row;
    if (row.actor.registry && (!previous.actor.registry || previous.hook != row.hook))
        qa_id_index_insert(&gear->pointers, 0, row.hook, ordinal);
}

qa_actor_id q3gear_actor(application_q3_gear *gear, int32_t pointer)
{
    if (!pointer) return (qa_actor_id){0};
    size_t row = q3gear_pointer_row(gear, (uint32_t)pointer, false);
    if (row == SIZE_MAX) return (qa_actor_id){0};
    return row & 1 ? gear->tethers[row / 2].actor : gear->bindings[row / 2].actor;
}

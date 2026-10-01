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
    if (!isfinite(value.x) || !isfinite(value.y) || !isfinite(value.z))
        return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear vector is not finite");
    *out = value; return true;
}

bool q3gear_vector_store(application_q3_gear *gear, uint32_t address, qa_vec3 value, qa_error *error)
{
    if (!isfinite(value.x) || !isfinite(value.y) || !isfinite(value.z))
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

qa_actor_id q3gear_actor(application_q3_gear *gear, int32_t pointer)
{
    if (!pointer) return (qa_actor_id){0};
    for (uint32_t i = 0; i < gear->capacity; ++i) {
        if (gear->bindings[i].actor.registry && gear->bindings[i].pointer == (uint32_t)pointer)
            return gear->bindings[i].actor;
        if (gear->tethers[i].actor.registry && gear->tethers[i].hook == (uint32_t)pointer)
            return gear->tethers[i].actor;
    }
    return (qa_actor_id){0};
}

static bool text_is(qa_bytes text, const char *value)
{
    return strlen(value) == text.size && !memcmp(text.data, value, text.size);
}

static bool retained_entity(const qa_entities *entities, size_t index, bool *world)
{
    static const char *classes[] = {"worldspawn", "info_player_start", "info_player_deathmatch",
        "info_player_intermission", "team_CTF_redplayer", "team_CTF_blueplayer",
        "team_CTF_redspawn", "team_CTF_bluespawn"};
    qa_bytes name;
    if (!qa_entity_value(entities, index, "classname", &name)) return false;
    *world = text_is(name, classes[0]);
    for (size_t i = 0; i < sizeof(classes)/sizeof(*classes); ++i)
        if (text_is(name, classes[i])) return true;
    return false;
}

bool q3gear_filter_entities(qa_bytes source, qa_buffer *out, qa_error *error)
{
    qa_entities entities = {0};
    if (!qa_entities_parse(source, QA_ENTITY_Q1, &entities, error)) return false;
    size_t size = 0, worlds = 0; bool okay = true;
    for (size_t i = 0; i < entities.count && okay; ++i) {
        bool world = false;
        if (!retained_entity(&entities, i, &world)) continue;
        worlds += world;
        if (size > SIZE_MAX - 4) { okay = false; break; }
        size += 4;
        qa_entity_record record = entities.records[i];
        for (size_t j = 0; j < record.property_count; ++j) {
            qa_entity_property field = entities.properties[record.first_property + j];
            if (memchr(field.key.data, '"', field.key.size) || memchr(field.key.data, 0, field.key.size) ||
                memchr(field.value.data, '"', field.value.size) || memchr(field.value.data, 0, field.value.size) ||
                field.key.size > SIZE_MAX - 6 || field.value.size > SIZE_MAX - 6 - field.key.size ||
                size > SIZE_MAX - 6 - field.key.size - field.value.size) { okay = false; break; }
            size += field.key.size + field.value.size + 6;
        }
    }
    if (!okay || worlds != 1 || size == SIZE_MAX) {
        qa_entities_free(&entities);
        return q3gear_fail(error, QA_ERROR_FORMAT, "Separate QVM gear requires one quotable authored worldspawn");
    }
    uint8_t *bytes = malloc(size + 1);
    if (!bytes) { qa_entities_free(&entities); return q3gear_fail(error, QA_ERROR_MEMORY, "Allocating separate QVM gear entity text"); }
    size_t cursor = 0;
    for (size_t i = 0; i < entities.count; ++i) {
        bool world = false;
        if (!retained_entity(&entities, i, &world)) continue;
        bytes[cursor++] = '{'; bytes[cursor++] = '\n';
        qa_entity_record record = entities.records[i];
        for (size_t j = 0; j < record.property_count; ++j) {
            qa_entity_property field = entities.properties[record.first_property + j];
            bytes[cursor++] = '"'; memcpy(bytes + cursor, field.key.data, field.key.size); cursor += field.key.size;
            bytes[cursor++] = '"'; bytes[cursor++] = ' '; bytes[cursor++] = '"';
            memcpy(bytes + cursor, field.value.data, field.value.size); cursor += field.value.size;
            bytes[cursor++] = '"'; bytes[cursor++] = '\n';
        }
        bytes[cursor++] = '}'; bytes[cursor++] = '\n';
    }
    bytes[cursor] = 0; qa_entities_free(&entities); *out = (qa_buffer){bytes, cursor}; return true;
}

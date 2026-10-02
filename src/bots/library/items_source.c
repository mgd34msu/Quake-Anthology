#include "items_source.h"
#include "../memory/internal.h"

static bool fail(qa_error *error, const char *text) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text); return false;
}
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}
static bool bytes(qa_bot_items *c, qa_bot_memory_span *span, qa_error *error) {
    if (!c || !c->memory || !qa_bot_memory_bytes(c->memory, c->allocation, span, error)) return false;
    bot_memory_record *record = bot_memory_record_get(c->memory, c->allocation);
    if (!record || record->kind != QA_BOT_MEMORY_HUNK)
        return fail(error, "Item configuration does not name its actual HUNK record");
    if (span->size != BOT_ITEM_HEADER_BYTES + (uint64_t)c->view.capacity * BOT_ITEM_BYTES)
        return fail(error, "Item configuration header exceeds its actual HUNK allocation");
    return true;
}
bool bot_items_create(qa_bot_memory *memory, const char *path, size_t capacity,
                      qa_bot_items **out, qa_error *error) {
    if (!memory || !path || !out || *out || capacity > (INT32_MAX - 4 - BOT_ITEM_HEADER_BYTES) / BOT_ITEM_BYTES)
        return fail(error, "Invalid item configuration source capacity/owner");
    qa_bot_items *c = calloc(1, sizeof(*c));
    if (!c) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining item configuration source"); return false; }
    atomic_init(&c->references, 1);
    if (!qa_bot_memory_retain(memory, error)) { free(c); return false; }
    c->memory = memory; c->view.capacity = capacity;
    c->view.path = bot_string(&c->arena, (qa_bytes){(const uint8_t *)path, strlen(path)}, error);
    c->items = calloc(capacity ? capacity : 1, sizeof(*c->items));
    c->projection_capacity = capacity ? capacity : 1;
    if (!c->view.path || !c->items ||
        !qa_bot_memory_allocate(memory, BOT_ITEM_HEADER_BYTES + (uint32_t)capacity * BOT_ITEM_BYTES,
                                QA_BOT_MEMORY_HUNK, true, NULL, &c->allocation, error)) {
        qa_bot_items_release(c); return false;
    }
    qa_bot_memory_span span;
    if (!qa_bot_memory_bytes(memory, c->allocation, &span, error)) { qa_bot_items_release(c); return false; }
    put(span.data + 4, BOT_ITEM_HEADER_BYTES);
    c->view.items = c->items; *out = c; return true;
}
bool bot_items_cell(qa_bot_items *c, size_t index, qa_bot_memory_span *out, qa_error *error) {
    if (!out || !c || index >= c->view.capacity || !bytes(c, out, error))
        return fail(error, "Item cell is outside its actual configuration");
    out->data += BOT_ITEM_HEADER_BYTES + index * BOT_ITEM_BYTES; out->size = BOT_ITEM_BYTES; return true;
}
bool bot_items_count(qa_bot_items *c, uint32_t count, qa_error *error) {
    qa_bot_memory_span span;
    if (!bytes(c, &span, error) || count > c->view.capacity) return false;
    put(span.data, count); return true;
}
bool bot_items_header_count(qa_bot_items *c, uint32_t *out, qa_error *error) {
    qa_bot_memory_span span;
    if (!out || !bytes(c, &span, error)) return false;
    *out = word(span.data);
    return *out <= c->view.capacity || fail(error, "Item parser count exceeds its actual source cells");
}
bool bot_items_member(qa_bot_items *c, uint32_t index, qa_error *error) {
    if (index >= c->view.capacity || c->member_count == SIZE_MAX ||
        !bot_grow((void **)&c->members, &c->member_capacity, c->member_count + 1,
                   sizeof(*c->members), error)) return false;
    c->members[c->member_count++] = index; return true;
}
bool bot_items_members_restore(qa_bot_items *c, qa_error *error) {
    uint32_t count;
    if (!bot_items_header_count(c, &count, error)) return false;
    for (uint32_t i = 0; i < count; ++i)
        if (!bot_items_member(c, i, error)) return false;
    return true;
}
bool bot_items_store(qa_bot_items *c, size_t index, const qa_bot_item_info *value, qa_error *error) {
    qa_bot_memory_span span;
    if (!value || !bot_items_cell(c, index, &span, error)) return false;
    memcpy(span.data, value->classname, 32); memcpy(span.data + 32, value->name, 80);
    memcpy(span.data + 112, value->model, 80);
    put(span.data + 192, (uint32_t)value->model_index); put(span.data + 196, (uint32_t)value->type);
    put(span.data + 200, (uint32_t)value->inventory); put(span.data + 232, (uint32_t)value->number);
    float values[] = {value->respawn_seconds, value->mins.x, value->mins.y, value->mins.z,
                      value->maxs.x, value->maxs.y, value->maxs.z};
    for (unsigned i = 0; i < 7; ++i) { uint32_t raw; memcpy(&raw, values + i, 4); put(span.data + 204 + i * 4, raw); }
    return true;
}
bool bot_items_project(qa_bot_items *c, qa_error *error) {
    qa_bot_memory_span header;
    if (!bytes(c, &header, error)) return false;
    if (c->member_count > c->projection_capacity &&
        !bot_grow((void **)&c->items, &c->projection_capacity, c->member_count, sizeof(*c->items), error)) return false;
    c->view.items = c->items; c->view.count = c->member_count;
    for (size_t i = 0; i < c->member_count; ++i) {
        qa_bot_memory_span span;
        if (!bot_items_cell(c, c->members[i], &span, error)) return false;
        qa_bot_item_info *value = c->items + i;
        memcpy(value->classname, span.data, 32); memcpy(value->name, span.data + 32, 80);
        memcpy(value->model, span.data + 112, 80);
        if (!memchr(value->classname, 0, 32) || !memchr(value->name, 0, 80) || !memchr(value->model, 0, 80))
            return fail(error, "Item source string is unterminated");
        uint32_t raw = word(span.data + 192); memcpy(&value->model_index, &raw, 4);
        raw = word(span.data + 196); memcpy(&value->type, &raw, 4);
        raw = word(span.data + 200); memcpy(&value->inventory, &raw, 4);
        raw = word(span.data + 232); memcpy(&value->number, &raw, 4);
        float values[7];
        for (unsigned n = 0; n < 7; ++n) { raw = word(span.data + 204 + n * 4); memcpy(values + n, &raw, 4); }
        value->respawn_seconds = values[0]; value->mins = (qa_vec3){values[1], values[2], values[3]};
        value->maxs = (qa_vec3){values[4], values[5], values[6]};
    }
    return true;
}
bool qa_bot_items_free(qa_bot_items *c, qa_error *error) {
    if (!c || c->active) return fail(error, "Item configuration free overlaps a source callback");
    return qa_bot_memory_free(c->memory, c->allocation, error);
}

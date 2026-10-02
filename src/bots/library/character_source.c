#include "character_source.h"
#include "../memory/internal.h"

static bool fail(qa_error *error, const char *text) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", text);
    return false;
}
static uint32_t word(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void put(uint8_t *p, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) p[i] = (uint8_t)(value >> (i * 8));
}
static bool bytes(qa_bot_character *c, qa_bot_memory_span *out, qa_error *error) {
    if (!c || !c->source || c->retired)
        return fail(error, "Character source is absent or freed");
    bot_memory_record *record = bot_memory_record_get(c->source->memory, c->allocation);
    if (!record || record->kind != QA_BOT_MEMORY_HEAP)
        return fail(error, "Character profile does not name its actual heap record");
    return qa_bot_memory_bytes(c->source->memory, c->allocation, out, error) &&
        (out->size == BOT_CHARACTER_BYTES || fail(error, "Character source extent is invalid"));
}
bool bot_character_store_create(qa_bot_memory *memory, bool owned, bot_character_store **out,
                                qa_error *error) {
    if (!memory || !out || *out) return fail(error, "Character store needs its MEMORY and empty output");
    bot_character_store *store = calloc(1, sizeof(*store));
    if (!store) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining character string pointers"); return false; }
    if (!qa_bot_memory_retain(memory, error)) { free(store); return false; }
    store->references = 1; store->next_pointer = 1; store->memory = memory; store->owns_memory = owned;
    *out = store; return true;
}
void bot_character_store_release(bot_character_store *store) {
    if (!store || --store->references) return;
    if (store->owns_memory) (void)qa_bot_memory_dispose(store->memory, NULL);
    (void)qa_bot_memory_release(store->memory, NULL);
    free(store->strings); free(store);
}
static bot_character_string *string(bot_character_store *store, uint32_t pointer) {
    for (size_t i = 0; i < store->count; ++i)
        if (store->strings[i].pointer == pointer) return store->strings + i;
    return NULL;
}
bool bot_character_create(bot_character_store *store, const char *path, float skill,
                          qa_bot_character **out, qa_error *error) {
    if (!store || !path || strlen(path) >= 64 || !out || *out)
        return fail(error, "Character filename copy exceeds MAX_QPATH destination");
    qa_bot_character *c = calloc(1, sizeof(*c));
    if (!c) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining character source alias"); return false; }
    atomic_init(&c->references, 1); c->source = store; ++store->references;
    if (!qa_bot_memory_allocate(store->memory, BOT_CHARACTER_BYTES, QA_BOT_MEMORY_HEAP, true,
                                NULL, &c->allocation, error)) {
        bot_character_store_release(store); free(c); return false;
    }
    qa_bot_memory_span span;
    if (!bytes(c, &span, error)) { qa_bot_character_release(c); return false; }
    memcpy(span.data, path, strlen(path) + 1);
    if (!bot_character_skill(c, skill, error)) { qa_bot_character_release(c); return false; }
    *out = c; return true;
}
bool bot_character_skill(qa_bot_character *c, float skill, qa_error *error) {
    qa_bot_memory_span span;
    if (!bytes(c, &span, error)) return false;
    uint32_t value; memcpy(&value, &skill, 4); put(span.data + 64, value); return true;
}
bool bot_character_write(qa_bot_character *c, uint32_t index, qa_bot_character_value value,
                         bool type_first, qa_error *error) {
    qa_bot_memory_span span;
    if (index >= QA_BOT_CHARACTERISTICS || (unsigned)value.kind > QA_BOT_CHARACTER_STRING ||
        !bytes(c, &span, error)) return fail(error, "Invalid character source cell");
    uint32_t offset = BOT_CHARACTER_VALUE_OFFSET + index * 8;
    uint32_t raw = 0;
    if (value.kind == QA_BOT_CHARACTER_STRING) {
        if (!value.data.string) return fail(error, "Character string source is absent");
        if (type_first) span.data[offset] = (uint8_t)value.kind;
        bot_character_store *store = c->source;
        if (store->next_pointer > UINT32_MAX ||
            !bot_grow((void **)&store->strings, &store->capacity, store->count + 1,
                      sizeof(*store->strings), error)) return false;
        size_t length = strlen(value.data.string);
        if (length >= INT32_MAX - 4) return fail(error, "Character string exceeds source allocation bounds");
        qa_bot_memory_allocation allocation = {0};
        if (!qa_bot_memory_allocate(store->memory, (uint32_t)length + 1, QA_BOT_MEMORY_HEAP,
                                    false, NULL, &allocation, error)) return false;
        raw = (uint32_t)store->next_pointer++;
        store->strings[store->count++] = (bot_character_string){raw, allocation};
        if (!bytes(c, &span, error)) return false;
        put(span.data + offset + 4, raw);
        qa_bot_memory_span text;
        if (!qa_bot_memory_bytes(store->memory, allocation, &text, error)) return false;
        memcpy(text.data, value.data.string, length + 1);
    } else if (value.kind == QA_BOT_CHARACTER_INTEGER) raw = (uint32_t)value.data.integer;
    else if (value.kind == QA_BOT_CHARACTER_FLOAT) memcpy(&raw, &value.data.number, 4);
    put(span.data + offset + 4, raw); span.data[offset] = (uint8_t)value.kind;
    return true;
}
bool bot_character_project(qa_bot_character *c, qa_error *error) {
    qa_bot_memory_span span;
    if (!bytes(c, &span, error) || !memchr(span.data, 0, 64))
        return fail(error, "Character source filename is unterminated");
    c->view.path = (const char *)span.data;
    uint32_t skill = word(span.data + 64); memcpy(&c->view.skill, &skill, 4);
    for (uint32_t i = 0; i < QA_BOT_CHARACTERISTICS; ++i) {
        uint32_t offset = BOT_CHARACTER_VALUE_OFFSET + i * 8, raw = word(span.data + offset + 4);
        qa_bot_character_value *value = c->view.values + i;
        *value = (qa_bot_character_value){.kind = (qa_bot_character_value_kind)span.data[offset]};
        if (value->kind == QA_BOT_CHARACTER_INTEGER) memcpy(&value->data.integer, &raw, 4);
        else if (value->kind == QA_BOT_CHARACTER_FLOAT) memcpy(&value->data.number, &raw, 4);
        else if (value->kind == QA_BOT_CHARACTER_STRING) {
            bot_character_string *row = string(c->source, raw); qa_bot_memory_span text;
            bot_memory_record *record = row ? bot_memory_record_get(c->source->memory, row->allocation) : NULL;
            if (!record || record->kind != QA_BOT_MEMORY_HEAP ||
                !qa_bot_memory_bytes(c->source->memory, row->allocation, &text, error) ||
                !memchr(text.data, 0, text.size)) return fail(error, "Invalid character string pointer");
            value->data.string = (const char *)text.data;
        } else if (value->kind != QA_BOT_CHARACTER_UNSET) return fail(error, "Invalid character type byte");
    }
    return true;
}
bool qa_bot_character_free(qa_bot_character *c, qa_error *error) {
    if (!c || c->active) return fail(error, "Character free overlaps a source callback");
    if (c->retired) return true;
    qa_bot_memory_span span;
    if (!bytes(c, &span, error)) return false;
    for (uint32_t i = 0; i < 80; ++i) {
        uint32_t offset = BOT_CHARACTER_VALUE_OFFSET + i * 8;
        if (span.data[offset] != QA_BOT_CHARACTER_STRING) continue;
        uint32_t pointer = word(span.data + offset + 4);
        if (!pointer) continue;
        bot_character_string *row = string(c->source, pointer);
        if (!row || !qa_bot_memory_free(c->source->memory, row->allocation, error)) return false;
        row->pointer = 0; put(span.data + offset + 4, 0);
    }
    if (!qa_bot_memory_free(c->source->memory, c->allocation, error)) return false;
    c->retired = true; c->ready = false; return true;
}
void bot_character_forget(qa_bot_library *library, qa_bot_character *c) {
    qa_bot_character **link = &library->characters, *previous = NULL;
    while (*link && *link != c) { previous = *link; link = &(*link)->next; }
    if (*link) {
        *link = c->next;
        if (library->last_character == c) library->last_character = previous;
        c->next = NULL; qa_bot_character_release(c);
    }
}

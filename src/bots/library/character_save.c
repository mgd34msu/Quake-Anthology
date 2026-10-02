#include "character_source.h"
#include "character_reader.h"
#include "../save_fields.h"
#include "qa/bots_allocator_save.h"
#include "../memory/internal.h"

bool bot_character_store_fields(qa_source_save_io *io, bot_character_store *store) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    size_t count = 0;
    if (!reading) for (size_t i = 0; i < store->count; ++i) count += store->strings[i].pointer != 0;
    if (!qa_source_save_u64(io, &store->next_pointer) || !store->next_pointer ||
        store->next_pointer > UINT64_C(4294967296) || !qa_source_save_count(io, &count, SIZE_MAX))
        return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid character string pointer space");
    if (reading) {
        if (count > (io->input.size - io->offset) / 12 || count > SIZE_MAX / sizeof(*store->strings))
            return bot_save_fail(io, QA_ERROR_FORMAT, "Truncated character string aliases");
        store->strings = calloc(count ? count : 1, sizeof(*store->strings));
        if (!store->strings) return bot_save_fail(io, QA_ERROR_MEMORY, "Restoring character string aliases");
        store->capacity = store->count = count;
    }
    size_t cursor = 0;
    for (size_t i = 0; i < count; ++i) {
        if (!reading) while (!store->strings[cursor].pointer) ++cursor;
        bot_character_string *row = store->strings + (reading ? i : cursor++);
        size_t reference = 0;
        if ((!reading && !qa_bot_memory_reference(store->memory, row->allocation, &reference, io->error)) ||
            !qa_source_save_u32(io, &row->pointer) || !row->pointer || row->pointer >= store->next_pointer ||
            !qa_source_save_count(io, &reference, SIZE_MAX) ||
            (reading && !qa_bot_memory_resolve(store->memory, reference, &row->allocation, io->error)))
            return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid character string allocation alias");
        qa_bot_memory_span span;
        bot_memory_record *record = bot_memory_record_get(store->memory, row->allocation);
        if (!record || record->kind != QA_BOT_MEMORY_HEAP ||
            !qa_bot_memory_bytes(store->memory, row->allocation, &span, io->error) || !memchr(span.data, 0, span.size))
            return bot_save_fail(io, QA_ERROR_FORMAT, "Character string allocation lacks terminator");
        for (size_t j = 0; j < i; ++j)
            if (reading && (store->strings[j].pointer == row->pointer ||
                (store->strings[j].allocation.slot == row->allocation.slot &&
                 store->strings[j].allocation.generation == row->allocation.generation)))
                return bot_save_fail(io, QA_ERROR_FORMAT, "Duplicate character string pointer");
    }
    return true;
}
bool bot_character_alias_fields(qa_source_save_io *io, bot_character_store *store,
                                const qa_bot_character *source, const qa_script_services *services,
                                qa_bot_character **out) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!store || (reading && (!out || *out)) || (!reading && (!source || source->source != store || source->active)))
        return bot_save_fail(io, QA_ERROR_ARGUMENT, "Character aliases require their actual MEMORY store");
    qa_bot_character *c = (qa_bot_character *)source;
    if (reading) {
        c = calloc(1, sizeof(*c));
        if (!c) return bot_save_fail(io, QA_ERROR_MEMORY, "Restoring character source alias");
        atomic_init(&c->references, 1); c->source = store; ++store->references;
    }
    size_t reference = 0;
    bool okay = qa_source_save_bool(io, &c->ready) && qa_source_save_bool(io, &c->retired) &&
        (!c->retired || !c->ready);
    if (okay && !c->retired) {
        if (!reading) okay = qa_bot_memory_reference(store->memory, c->allocation, &reference, io->error);
        if (okay) okay = qa_source_save_count(io, &reference, SIZE_MAX);
        if (okay && reading) okay = qa_bot_memory_resolve(store->memory, reference, &c->allocation, io->error);
        qa_bot_memory_span span;
        bot_memory_record *record = okay ? bot_memory_record_get(store->memory, c->allocation) : NULL;
        if (okay) okay = record && record->kind == QA_BOT_MEMORY_HEAP &&
            qa_bot_memory_bytes(store->memory, c->allocation, &span, io->error) && span.size == BOT_CHARACTER_BYTES;
    }
    if (okay) okay = bot_character_reader_fields(io, &c->reader, &c->script_host, services);
    if (okay && c->ready) okay = !c->reader && bot_character_project(c, io->error);
    if (reading) {
        if (okay) *out = c;
        else { c->retired = true; qa_bot_character_release(c); }
    }
    if (!okay && !io->failed) return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid character source alias");
    return okay;
}
bool bot_character_standalone_fields(qa_source_save_io *io, const qa_bot_character *source,
                                     qa_bot_character **out) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_bot_memory *memory = reading ? NULL : source->source->memory;
    bot_character_store *store = reading ? NULL : source->source;
    qa_buffer encoded = {0}; size_t length = 0;
    bool okay = reading || qa_bot_memory_capture(memory, &encoded, io->error);
    if (!reading) length = encoded.size;
    if (okay) okay = qa_source_save_count(io, &length, SIZE_MAX);
    if (okay && reading) {
        if (length > io->input.size - io->offset) okay = bot_save_fail(io, QA_ERROR_FORMAT, "Truncated character MEMORY");
        else {
            okay = qa_bot_memory_create(NULL, &memory, io->error) &&
                qa_bot_memory_restore(memory, (qa_bytes){io->input.data + io->offset, length}, io->error) &&
                bot_character_store_create(memory, true, &store, io->error);
            if (okay) io->offset += length;
        }
    } else if (okay) okay = qa_source_save_bytes(io, encoded.data, length);
    if (okay) okay = bot_character_store_fields(io, store) &&
        bot_character_alias_fields(io, store, source, NULL, out);
    if (reading) { bot_character_store_release(store); (void)qa_bot_memory_release(memory, NULL); }
    qa_buffer_free(&encoded); if (!okay) io->failed = true; return okay;
}

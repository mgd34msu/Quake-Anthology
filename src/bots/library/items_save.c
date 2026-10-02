#include "items_source.h"
#include "character_reader.h"
#include "../save_fields.h"
#include "qa/bots_allocator_save.h"

bool bot_items_alias_fields(qa_source_save_io *io, qa_bot_memory *memory, const qa_bot_items *source,
                           const qa_script_services *services, qa_bot_items **out) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    if (!memory || (reading && (!out || *out)) || (!reading && (!source || source->memory != memory || source->active)))
        return bot_save_fail(io, QA_ERROR_ARGUMENT, "Item aliases require their actual MEMORY owner");
    qa_bot_items *c = (qa_bot_items *)source;
    if (reading) {
        c = calloc(1, sizeof(*c));
        if (!c) return bot_save_fail(io, QA_ERROR_MEMORY, "Restoring item allocation alias");
        atomic_init(&c->references, 1);
        if (!qa_bot_memory_retain(memory, io->error)) { free(c); io->failed = true; return false; }
        c->memory = memory;
    }
    const char *path = reading ? NULL : c->view.path; size_t reference = 0;
    bool okay = bot_save_text(io, &path) && path &&
        qa_source_save_count(io, &c->view.capacity, (INT32_MAX - 4 - BOT_ITEM_HEADER_BYTES) / BOT_ITEM_BYTES) &&
        qa_source_save_bool(io, &c->ready) && qa_source_save_bool(io, &c->reader_retired);
    if (okay && reading) {
        c->view.path = bot_string(&c->arena, (qa_bytes){(const uint8_t *)path, strlen(path)}, io->error);
        c->items = calloc(c->view.capacity ? c->view.capacity : 1, sizeof(*c->items));
        c->projection_capacity = c->view.capacity ? c->view.capacity : 1;
        c->view.items = c->items; okay = c->view.path && c->items;
    }
    if (okay && !reading) okay = qa_bot_memory_reference(memory, c->allocation, &reference, io->error);
    if (okay) okay = qa_source_save_count(io, &reference, SIZE_MAX);
    if (okay && reading) okay = qa_bot_memory_resolve(memory, reference, &c->allocation, io->error);
    if (okay && reading) okay = bot_items_members_restore(c, io->error);
    if (okay) okay = bot_items_project(c, io->error) &&
        bot_character_reader_fields(io, &c->reader, &c->script_host, services) && (!c->ready || !c->reader);
    if (okay && c->reader_retired) okay = c->reader && !c->ready;
    if (reading) { free((void *)path); if (okay) *out = c; else qa_bot_items_release(c); }
    if (!okay && !io->failed) return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid item source alias");
    return okay;
}
bool bot_items_standalone_fields(qa_source_save_io *io, const qa_bot_items *source, qa_bot_items **out) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_bot_memory *memory = reading ? NULL : source->memory;
    qa_buffer encoded = {0}; size_t length = 0;
    bool okay = reading || qa_bot_memory_capture(memory, &encoded, io->error);
    if (!reading) length = encoded.size;
    if (okay) okay = qa_source_save_count(io, &length, SIZE_MAX);
    if (okay && reading) {
        if (length > io->input.size - io->offset) okay = bot_save_fail(io, QA_ERROR_FORMAT, "Truncated item MEMORY");
        else {
            okay = qa_bot_memory_create(NULL, &memory, io->error) &&
                qa_bot_memory_restore(memory, (qa_bytes){io->input.data + io->offset, length}, io->error);
            if (okay) io->offset += length;
        }
    } else if (okay) okay = qa_source_save_bytes(io, encoded.data, length);
    if (okay) okay = bot_items_alias_fields(io, memory, source, NULL, out);
    if (reading) (void)qa_bot_memory_release(memory, NULL);
    qa_buffer_free(&encoded); if (!okay) io->failed = true; return okay;
}

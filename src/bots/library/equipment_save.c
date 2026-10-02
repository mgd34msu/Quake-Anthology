#include "internal.h"
#include "../save_fields.h"
#include "qa/bot_assets_save.h"
#include "source_weapon_standalone_save.h"

static bool fixed_text(qa_source_save_io *io, char *text, size_t capacity)
{
    size_t length = 0;
    if (io->direction == QA_SOURCE_SAVE_WRITE) {
        const char *end = memchr(text, 0, capacity);
        if (!end)
            return bot_save_fail(io, QA_ERROR_FORMAT, "Unterminated bot definition text");
        length = (size_t)(end - text);
    }
    if (!qa_source_save_count(io, &length, capacity - 1) ||
        !qa_source_save_bytes(io, text, length))
        return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (memchr(text, 0, length))
            return bot_save_fail(io, QA_ERROR_FORMAT, "Embedded NUL in bot definition text");
        text[length] = 0;
    }
    return true;
}

bool bot_save_weapons_fields(qa_source_save_io *io, const qa_bot_weapons *source,
                             qa_bot_weapons **out)
{
    return bot_weapons_standalone_fields(io,source,out);
}

static bool item_fields(qa_source_save_io *io, qa_bot_item_info *value)
{
    return fixed_text(io, value->classname, sizeof(value->classname)) &&
        fixed_text(io, value->name, sizeof(value->name)) &&
        fixed_text(io, value->model, sizeof(value->model)) &&
        qa_source_save_i32(io, &value->model_index) && qa_source_save_i32(io, &value->type) &&
        qa_source_save_i32(io, &value->inventory) && qa_source_save_i32(io, &value->number) &&
        qa_source_save_f32(io, &value->respawn_seconds) && qa_source_save_vec3(io, &value->mins) &&
        qa_source_save_vec3(io, &value->maxs);
}

bool bot_save_items_fields(qa_source_save_io *io, const qa_bot_items *source, qa_bot_items **out)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_bot_items_view view = reading ? (qa_bot_items_view){0} : *qa_bot_items_read(source);
    qa_bot_item_info *items = NULL;
    bool ok = bot_save_text(io, &view.path) && view.path &&
        qa_source_save_count(io, &view.capacity, INT32_MAX) &&
        qa_source_save_count(io, &view.count, view.capacity);
    if (ok && reading) {
        if (view.capacity > SIZE_MAX / sizeof(*items) ||
            view.capacity > (io->input.size - io->offset) / 68)
            ok = bot_save_fail(io, QA_ERROR_FORMAT, "Truncated bot item definition table");
        if (ok && view.capacity)
            items = calloc(view.capacity, sizeof(*items));
        if (ok && view.capacity && !items)
            ok = bot_save_fail(io, QA_ERROR_MEMORY, "Restoring bot item definition fields");
        view.items = items;
    }
    for (size_t i = 0; ok && i < view.capacity; ++i) {
        qa_bot_item_info value = reading ? (qa_bot_item_info){0} : view.items[i];
        ok = item_fields(io, &value);
        if (reading)
            items[i] = value;
    }
    qa_bot_items *candidate = NULL;
    if (ok && reading) {
        ok = qa_bot_items_restore(&view, &candidate, io->error);
        if (!ok)
            io->failed = true;
    }
    if (reading) {
        free(items); free((void *)view.path);
        if (ok)
            *out = candidate;
        else
            qa_bot_items_release(candidate);
    }
    if (!ok && !io->failed)
        return bot_save_fail(io, QA_ERROR_FORMAT, "Invalid bot item definition fields");
    return ok;
}

#define ASSET_API(name, type, tag) \
bool qa_bot_##name##_save_capture(const type *source, qa_buffer *out, qa_error *error) \
{ \
    if (!source || !out) { \
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Missing bot " #name " capture owner/output"); \
        return false; \
    } \
    static const uint8_t magic[8] = tag; \
    qa_source_save_io io = {0}; \
    bool ok = qa_source_save_writer(&io, NULL, error) && bot_save_signature(&io, magic) && \
        bot_save_##name##_fields(&io, source, NULL) && qa_source_save_finish(&io, out); \
    qa_source_save_dispose(&io); \
    return ok; \
} \
bool qa_bot_##name##_save_restore(qa_bytes bytes, type **out, qa_error *error) \
{ \
    if (!out || *out) { \
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Bot " #name " restore requires an empty output"); \
        return false; \
    } \
    static const uint8_t magic[8] = tag; \
    qa_source_save_io io = {0}; type *candidate = NULL; \
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && bot_save_signature(&io, magic) && \
        bot_save_##name##_fields(&io, NULL, &candidate) && qa_source_save_finish(&io, NULL); \
    qa_source_save_dispose(&io); \
    if (!ok) { qa_bot_##name##_release(candidate); return false; } \
    *out = candidate; \
    return true; \
}

ASSET_API(weapons, qa_bot_weapons, "QABWRAW")
ASSET_API(items, qa_bot_items, "QABITEM")
#undef ASSET_API

#ifndef QA_TOOLS_SAVE_INTERNAL_H
#define QA_TOOLS_SAVE_INTERNAL_H
#include "qa/source_save.h"
#include "qa/arena.h"
#include "qa/console.h"
#include "qa/text.h"
#include <stdlib.h>
#include <string.h>

static inline bool tool_save_fail(qa_source_save_io *io, const char *text) {
    if (!io->failed) qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "%s", text);
    io->failed = true; return false;
}
static inline bool tool_save_blob(qa_source_save_io *io, qa_buffer *value, bool terminated) {
    bool present = value->data != NULL;
    size_t n = value->size;
    if (!qa_source_save_bool(io, &present) || !qa_source_save_count(io, &n, SIZE_MAX - (terminated ? 1 : 0))) return false;
    if ((!present && n) || (io->direction == QA_SOURCE_SAVE_READ && n > io->input.size - io->offset))
        return tool_save_fail(io, "invalid private continuation byte extent");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (present) {
            value->data = malloc(n + (terminated ? 1 : 0) + (!n && !terminated ? 1 : 0));
            if (!value->data) { io->failed = true; qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating private continuation bytes"); return false; }
        }
        value->size = n;
    }
    if (!qa_source_save_bytes(io, value->data, n)) return false;
    if (terminated && present && io->direction == QA_SOURCE_SAVE_READ) value->data[n] = 0;
    return true;
}
static inline bool tool_save_text(qa_source_save_io *io, char **text) {
    qa_buffer bytes = {(uint8_t *)*text, *text ? strlen(*text) : 0};
    bool ok = tool_save_blob(io, &bytes, true);
    if (io->direction == QA_SOURCE_SAVE_READ) *text = (char *)bytes.data;
    if (!ok) return false;
    if (bytes.data && memchr(bytes.data, 0, bytes.size))
        return tool_save_fail(io, "invalid private continuation text");
    return true;
}
static inline bool tool_save_arena_text(qa_source_save_io *io, qa_arena *arena, const char **text) {
    char *copy = io->direction == QA_SOURCE_SAVE_WRITE ? (char *)*text : NULL;
    bool ok = tool_save_text(io, &copy);
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (ok && copy) {
            size_t n = strlen(copy) + 1;
            char *owned = qa_arena_alloc(arena, n, 1, io->error);
            if (!owned) { io->failed = true; ok = false; }
            else { memcpy(owned, copy, n); *text = owned; }
        }
        free(copy);
    }
    return ok;
}
static inline bool tool_save_error(qa_source_save_io *io, qa_error *value) {
    uint32_t status = (uint32_t)value->code;
    uint64_t offset = value->offset;
    char *message = io->direction == QA_SOURCE_SAVE_WRITE ? value->message : NULL;
    if (!qa_source_save_u32(io, &status) || status > QA_ERROR_CAPACITY ||
        !qa_source_save_u64(io, &offset) || offset > SIZE_MAX) return tool_save_fail(io, "invalid private continuation error");
    bool ok = tool_save_text(io, &message);
    if (ok && (!message || strlen(message) >= sizeof value->message)) ok = tool_save_fail(io, "invalid private continuation error message");
    if (io->direction == QA_SOURCE_SAVE_READ) {
        if (ok) { value->code = (qa_status)status; value->offset = (size_t)offset; memcpy(value->message, message, strlen(message) + 1); }
        free(message);
    }
    return ok;
}
static inline bool tool_save_context(qa_source_save_io *io, qa_command_context *value, char **script) {
    uint32_t dialect = (uint32_t)value->dialect, origin = (uint32_t)value->origin;
    if (!qa_source_save_u64(io, &value->session) || !qa_source_save_u64(io, &value->owner) ||
        !qa_source_save_u64(io, &value->client) || !qa_source_save_u32(io, &value->seat) ||
        !qa_source_save_u32(io, &dialect) || dialect > QA_CONSOLE_Q3 ||
        !qa_source_save_u32(io, &origin) || origin > QA_COMMAND_REMOTE ||
        !qa_source_save_bool(io, &value->direct) || !qa_source_save_bool(io, &value->console_text) ||
        !qa_source_save_u64(io, &value->registry) || !qa_source_save_u64(io, &value->generation) ||
        !qa_source_save_actor(io, &value->actor)) return tool_save_fail(io, "invalid deferred command context");
    if (io->direction == QA_SOURCE_SAVE_WRITE) *script = (char *)value->script;
    if (!tool_save_text(io, script)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) { value->dialect = (qa_console_dialect)dialect; value->origin = (qa_command_origin)origin; value->script = *script; }
    return true;
}
#endif

#include "buffer_internal.h"
#include "qa/text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, qa_status code, const char *message) {
    qa_error_set(error, code, 0, "%s", message);
    return false;
}
static size_t slot(const qa_console_buffer *buffer, size_t index) {
    return (buffer->first + index) % buffer->capacity;
}
static row_state *row(qa_console_buffer *buffer, size_t index) {
    return &buffer->rows[slot(buffer, index)];
}
static qa_console_cell *cells(qa_console_buffer *buffer, size_t index) {
    return buffer->cells + slot(buffer, index) * buffer->width;
}
qa_console_buffer *qa_console_buffer_create(qa_ruleset_id dialect, size_t width,
                                            size_t capacity, qa_error *error) {
    if (dialect < QA_RULESET_NETQUAKE || dialect > QA_RULESET_Q3 || !width || width > capacity ||
        capacity > SIZE_MAX / sizeof(qa_console_cell) ||
        capacity / width > SIZE_MAX / sizeof(row_state)) {
        fail(error, QA_ERROR_ARGUMENT, "Invalid console scrollback dimensions");
        return NULL;
    }
    qa_console_buffer *buffer = calloc(1, sizeof(*buffer));
    if (!buffer) {
        fail(error, QA_ERROR_MEMORY, "Allocating console scrollback");
        return NULL;
    }
    buffer->character_capacity = capacity;
    buffer->capacity = capacity / width;
    buffer->width = width;
    buffer->dialect = dialect;
    buffer->rows = calloc(buffer->capacity, sizeof(*buffer->rows));
    buffer->cells = malloc(buffer->capacity * width * sizeof(*buffer->cells));
    if (!buffer->rows || !buffer->cells) {
        qa_console_buffer_destroy(buffer);
        fail(error, QA_ERROR_MEMORY, "Allocating console rows");
        return NULL;
    }
    buffer->count = buffer->sequence = 1;
    return buffer;
}
void qa_console_buffer_destroy(qa_console_buffer *buffer) {
    if (!buffer)
        return;
    free(buffer->cells);
    free(buffer->rows);
    free(buffer);
}
void qa_console_buffer_dialect(qa_console_buffer *buffer, qa_ruleset_id dialect) {
    buffer->dialect = dialect;
}
static void append(qa_console_buffer *buffer, row_state value, const qa_console_cell *text) {
    if (buffer->count == buffer->capacity) {
        buffer->first = (buffer->first + 1) % buffer->capacity;
        if (buffer->write)
            --buffer->write;
    } else
        ++buffer->count;
    *row(buffer, buffer->count - 1) = value;
    if (value.count)
        memcpy(cells(buffer, buffer->count - 1), text, value.count * sizeof(*text));
}
static void linefeed(qa_console_buffer *buffer, double time, bool notify, bool wrapped) {
    row_state *current = row(buffer, buffer->write);
    current->wrapped = wrapped;
    current->time = time;
    current->notify = notify;
    if (buffer->write + 1 == buffer->count) {
        if (buffer->backscroll)
            ++buffer->backscroll;
        append(buffer, (row_state){.sequence = buffer->sequence++, .time = time, .notify = notify},
               NULL);
        buffer->write = buffer->count - 1;
    } else
        ++buffer->write;
    if (buffer->backscroll >= buffer->count)
        buffer->backscroll = buffer->count - 1;
    buffer->x = 0;
}
bool qa_console_buffer_print(qa_console_buffer *buffer, qa_bytes text, double time,
                             qa_error *error) {
    if ((!text.data && text.size) || !isfinite(time))
        return fail(error, QA_ERROR_ARGUMENT, "Invalid console output");
    bool notify = true, alternate = false;
    size_t at = 0;
    if (text.size >= 12 && !memcmp(text.data, "[skipnotify]", 12)) {
        notify = false;
        at = 12;
    }
    if (buffer->dialect != QA_RULESET_Q3 && at < text.size &&
        (text.data[at] == 1 || text.data[at] == 2)) {
        alternate = true;
        ++at;
    }
    uint8_t color = 7;
    uint32_t code;
    while (qa_utf8_next(text, &at, &code)) {
        if ((buffer->dialect == QA_RULESET_Q3 || buffer->dialect == QA_RULESET_Q2_RERELEASE) &&
            code == '^' && at < text.size && text.data[at] >= '0' && text.data[at] <= '9') {
            color = (uint8_t)((text.data[at++] - '0') & 7);
            continue;
        }
        if (code == '\n') {
            linefeed(buffer, time, notify, false);
            continue;
        }
        if (code == '\r') {
            buffer->x = 0;
            continue;
        }
        if (code > ' ') {
            size_t length = 1, look = at;
            uint32_t next;
            while (length < buffer->width && qa_utf8_next(text, &look, &next) && next > ' ')
                ++length;
            if (length < buffer->width && buffer->x >= buffer->width - length)
                linefeed(buffer, time, notify, true);
        }
        row_state *current = row(buffer, buffer->write);
        while (current->count < buffer->x)
            cells(buffer, buffer->write)[current->count++] = (qa_console_cell){' ', 7, false};
        cells(buffer, buffer->write)[buffer->x++] = (qa_console_cell){code, color, alternate};
        if (buffer->x > current->count)
            current->count = buffer->x;
        current->time = time;
        current->notify = notify;
        if (buffer->x == buffer->width)
            linefeed(buffer, time, notify, true);
    }
    return true;
}
typedef struct reflow {
    qa_console_buffer *target;
    qa_console_cell *paragraph;
    size_t length, cursor_offset, anchor_offset, produced, cursor_row, cursor_x, anchor_row;
} reflow;
static void flush(reflow *state) {
    qa_console_buffer *target = state->target;
    size_t start = state->produced,
           lines = state->length ? 1 + (state->length - 1) / target->width : 1;
    for (size_t i = 0; i < lines; ++i) {
        size_t offset = i * target->width, count = state->length - offset;
        if (count > target->width)
            count = target->width;
        append(
            target,
            (row_state){.sequence = target->sequence++, .count = count, .wrapped = i + 1 < lines},
            state->paragraph + offset);
        ++state->produced;
    }
    if (state->anchor_offset != SIZE_MAX) {
        size_t offset = state->anchor_offset / target->width;
        state->anchor_row = start + (offset < lines ? offset : lines - 1);
    }
    if (state->cursor_offset != SIZE_MAX) {
        state->cursor_x = state->cursor_offset % target->width;
        state->cursor_row = start + state->cursor_offset / target->width;
        if (state->cursor_row == state->produced) {
            row(target, target->count - 1)->wrapped = true;
            append(target, (row_state){.sequence = target->sequence++}, NULL);
            ++state->produced;
        }
    }
    state->length = 0;
    state->anchor_offset = state->cursor_offset = SIZE_MAX;
}
bool qa_console_buffer_resize(qa_console_buffer *buffer, size_t width, qa_error *error) {
    if (width == buffer->width)
        return true;
    qa_console_buffer *target =
        qa_console_buffer_create(buffer->dialect, width, buffer->character_capacity, error);
    if (!target)
        return false;
    qa_console_cell *paragraph = malloc(buffer->character_capacity * sizeof(*paragraph));
    if (!paragraph) {
        qa_console_buffer_destroy(target);
        return fail(error, QA_ERROR_MEMORY, "Allocating console reflow");
    }
    target->sequence = buffer->sequence;
    target->count = 0;
    reflow state = {.target = target,
                    .paragraph = paragraph,
                    .cursor_offset = SIZE_MAX,
                    .anchor_offset = SIZE_MAX};
    size_t anchor = buffer->count - 1 - buffer->backscroll;
    for (size_t i = 0; i < buffer->count; ++i) {
        const row_state *current = row(buffer, i);
        if (i == anchor)
            state.anchor_offset = state.length;
        if (i == buffer->write)
            state.cursor_offset = state.length + buffer->x;
        memcpy(paragraph + state.length, cells(buffer, i), current->count * sizeof(*paragraph));
        state.length += current->count;
        if (!current->wrapped)
            flush(&state);
    }
    if (state.length || state.cursor_offset != SIZE_MAX || state.anchor_offset != SIZE_MAX)
        flush(&state);
    size_t removed = state.produced - target->count;
    target->write = state.cursor_row < removed ? 0 : state.cursor_row - removed;
    target->x = state.cursor_x;
    target->backscroll = !buffer->backscroll ? 0 : state.produced - 1 - state.anchor_row;
    if (target->backscroll >= target->count)
        target->backscroll = target->count - 1;
    qa_console_buffer old = *buffer;
    *buffer = *target;
    *target = old;
    free(paragraph);
    qa_console_buffer_destroy(target);
    return true;
}
void qa_console_buffer_clear(qa_console_buffer *buffer) {
    buffer->first = buffer->write = buffer->x = buffer->backscroll = 0;
    buffer->count = 1;
    buffer->rows[0] = (row_state){.sequence = buffer->sequence++};
}
void qa_console_buffer_clear_notify(qa_console_buffer *buffer) {
    for (size_t i = 0; i < buffer->count; ++i)
        row(buffer, i)->notify = false;
}
void qa_console_buffer_scroll(qa_console_buffer *buffer, int64_t rows) {
    if (rows >= 0) {
        uint64_t count = (uint64_t)rows;
        size_t room = buffer->count - 1 - buffer->backscroll;
        buffer->backscroll += count > room ? room : (size_t)count;
    } else {
        uint64_t count = (uint64_t)(-(rows + 1)) + 1;
        buffer->backscroll -= count > buffer->backscroll ? buffer->backscroll : (size_t)count;
    }
}
void qa_console_buffer_bottom(qa_console_buffer *buffer) { buffer->backscroll = 0; }
void qa_console_buffer_top(qa_console_buffer *buffer) { buffer->backscroll = buffer->count - 1; }
size_t qa_console_buffer_width(const qa_console_buffer *buffer) { return buffer->width; }
size_t qa_console_buffer_backscroll(const qa_console_buffer *buffer) { return buffer->backscroll; }
size_t qa_console_buffer_count(const qa_console_buffer *buffer) { return buffer->count; }
bool qa_console_buffer_row(const qa_console_buffer *buffer, size_t index, qa_console_row *out) {
    if (index >= buffer->count)
        return false;
    size_t at = slot(buffer, index);
    const row_state *current = &buffer->rows[at];
    *out = (qa_console_row){current->sequence, buffer->cells + at * buffer->width,
                            current->count,    current->time,
                            current->notify,   current->wrapped};
    return true;
}
size_t qa_console_buffer_visible(const qa_console_buffer *buffer, size_t maximum, size_t *first) {
    size_t end = buffer->count - buffer->backscroll, count = maximum < end ? maximum : end;
    *first = end - count;
    return count;
}
bool qa_console_row_notifies(const qa_console_row *row_view, double now, double duration) {
    return row_view->notify && now - row_view->time_ms <= duration;
}
bool qa_console_buffer_dump(const qa_console_buffer *buffer, qa_buffer *out, qa_error *error) {
    size_t bytes = 0;
    for (size_t i = 0; i < buffer->count; ++i) {
        qa_console_row view;
        qa_console_buffer_row(buffer, i, &view);
        size_t count = view.count;
        while (count && qa_unicode_whitespace(view.cells[count - 1].scalar))
            --count;
        for (size_t j = 0; j < count; ++j) {
            char encoded[4];
            size_t n = qa_utf8_encode(view.cells[j].scalar, encoded);
            if (bytes > SIZE_MAX - n - 2)
                return fail(error, QA_ERROR_MEMORY, "Console dump overflow");
            bytes += n;
        }
        if (bytes >= SIZE_MAX - 1)
            return fail(error, QA_ERROR_MEMORY, "Console dump overflow");
        ++bytes;
    }
    uint8_t *text = malloc(bytes + 1);
    if (!text)
        return fail(error, QA_ERROR_MEMORY, "Allocating console dump");
    size_t at = 0;
    for (size_t i = 0; i < buffer->count; ++i) {
        qa_console_row view;
        qa_console_buffer_row(buffer, i, &view);
        size_t count = view.count;
        while (count && qa_unicode_whitespace(view.cells[count - 1].scalar))
            --count;
        for (size_t j = 0; j < count; ++j)
            at += qa_utf8_encode(view.cells[j].scalar, (char *)text + at);
        text[at++] = '\n';
    }
    text[at] = 0;
    *out = (qa_buffer){text, at};
    return true;
}

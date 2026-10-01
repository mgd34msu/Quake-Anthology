#include "seat_internal.h"
#include "qa/console_seat_save.h"
#include "qa/binary.h"
#include "qa/text.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct seat_io {
    bool reading;
    qa_bytes input;
    size_t offset;
    qac_text output;
    qa_error *error;
} seat_io;

static bool invalid(seat_io *io, const char *text) {
    return qac_fail(io->error, io->reading ? QA_ERROR_FORMAT : QA_ERROR_ARGUMENT, text);
}
static bool bytes(seat_io *io, void *value, size_t size) {
    if (!io->reading)
        return qac_text_add(&io->output, value, size, io->error);
    if (io->offset > io->input.size || size > io->input.size - io->offset)
        return invalid(io, "truncated seat console continuation");
    if (size) memcpy(value, io->input.data + io->offset, size);
    io->offset += size;
    return true;
}
static bool u32(seat_io *io, uint32_t *value) {
    uint8_t data[4];
    if (!io->reading) qa_store_u32le(data, *value);
    if (!bytes(io, data, sizeof(data))) return false;
    if (io->reading) *value = qa_load_u32le(data);
    return true;
}
static bool u64(seat_io *io, uint64_t *value) {
    uint8_t data[8];
    if (!io->reading) qa_store_u64le(data, *value);
    if (!bytes(io, data, sizeof(data))) return false;
    if (io->reading) *value = qa_load_u64le(data);
    return true;
}
static bool count(seat_io *io, size_t *value, size_t maximum) {
    uint64_t wire = *value;
    if (!u64(io, &wire)) return false;
    if (wire > SIZE_MAX || wire > maximum)
        return invalid(io, "invalid seat console extent");
    if (io->reading) *value = (size_t)wire;
    return true;
}
static bool boolean(seat_io *io, bool *value) {
    uint8_t wire = *value;
    if (!bytes(io, &wire, 1)) return false;
    if (wire > 1) return invalid(io, "invalid seat console boolean");
    if (io->reading) *value = wire != 0;
    return true;
}
static bool f32(seat_io *io, float *value) {
    uint32_t wire;
    memcpy(&wire, value, sizeof(wire));
    if (!u32(io, &wire)) return false;
    if (io->reading) memcpy(value, &wire, sizeof(wire));
    return true;
}
static bool f64(seat_io *io, double *value) {
    uint64_t wire;
    memcpy(&wire, value, sizeof(wire));
    if (!u64(io, &wire)) return false;
    if (io->reading) memcpy(value, &wire, sizeof(wire));
    return isfinite(*value) || invalid(io, "nonfinite seat console timestamp");
}
static bool text(seat_io *io, char **value, size_t maximum) {
    bool present = *value != NULL;
    if (!boolean(io, &present)) return false;
    size_t length = present && !io->reading ? strlen(*value) : 0;
    if (present && !count(io, &length, maximum)) return false;
    if (!io->reading) return !present || bytes(io, *value, length);
    char *copy = NULL;
    if (present) {
        if (length == SIZE_MAX || io->offset > io->input.size ||
            length > io->input.size - io->offset)
            return invalid(io, "invalid private seat console text extent");
        copy = malloc(length + 1);
        if (!copy) return qac_fail(io->error, QA_ERROR_MEMORY, "retaining private console text");
        if (!bytes(io, copy, length) || memchr(copy, 0, length)) {
            free(copy);
            return invalid(io, "private seat console text contains a NUL byte");
        }
        copy[length] = 0;
    }
    free(*value);
    *value = copy;
    return true;
}
static bool scalar(uint32_t value) {
    return value <= 0x10ffffu && !(value >= 0xd800u && value <= 0xdfffu);
}
static bool field(seat_io *io, qa_text_field **owned, size_t maximum) {
    qa_text_field *p = *owned;
    size_t saved_maximum = p ? p->maximum : 0, width = p ? p->width : 0;
    if (!count(io, &saved_maximum, maximum) || saved_maximum != maximum ||
        !count(io, &width, SIZE_MAX))
        return invalid(io, "seat text field dimensions changed");
    if (io->reading) {
        p = qa_text_field_create(maximum, width, io->error);
        if (!p) return false;
        *owned = p;
    }
    if (!p || !count(io, &p->length, maximum) || !count(io, &p->cursor, maximum) ||
        !count(io, &p->scroll, maximum) || !boolean(io, &p->overstrike) ||
        !boolean(io, &p->dirty) || !boolean(io, &p->selected)) return false;
    if (p->cursor > p->length || p->scroll > p->cursor)
        return invalid(io, "invalid seat text field cursor");
    for (size_t i = 0; i < p->length; ++i)
        if (!u32(io, &p->characters[i]) || !scalar(p->characters[i]) ||
            p->characters[i] < 32 || p->characters[i] == 127)
            return invalid(io, "invalid seat text field character");
    char *cached = io->reading ? NULL : p->text;
    if (!text(io, &cached, maximum * 4)) return false;
    if (!cached) return invalid(io, "seat text field lost its text cache");
    if (io->reading) {
        memcpy(p->text, cached, strlen(cached) + 1);
        free(cached);
    }
    if (!p->dirty) {
        size_t at = 0, length = strlen(p->text);
        for (size_t i = 0; i < p->length; ++i) {
            char encoded[4];
            size_t n = qa_utf8_encode(p->characters[i], encoded);
            if (at > length || n > length - at || memcmp(p->text + at, encoded, n))
                return invalid(io, "seat text cache differs from actual characters");
            at += n;
        }
        if (at != length) return invalid(io, "seat text cache has trailing characters");
    }
    size_t matches = io->reading ? 0 : p->match_count;
    size_t limit = io->reading ? (io->input.size - io->offset) / 9 : SIZE_MAX / sizeof(char *);
    if (!count(io, &matches, limit)) return false;
    if (io->reading && matches) {
        if (matches > SIZE_MAX / sizeof(*p->matches))
            return invalid(io, "seat completion allocation overflow");
        p->matches = calloc(matches, sizeof(*p->matches));
        if (!p->matches) return qac_fail(io->error, QA_ERROR_MEMORY, "retaining seat completion");
        p->match_count = matches;
    }
    for (size_t i = 0; i < matches; ++i) {
        if (!text(io, &p->matches[i], SIZE_MAX - 1)) return false;
        if (!p->matches[i]) return invalid(io, "seat completion lost a registered name");
    }
    if (!count(io, &p->match_index, SIZE_MAX) || !text(io, &p->tail, SIZE_MAX - 1)) return false;
    if (matches ? !p->tail || p->match_index >= matches : p->tail || p->selected)
        return invalid(io, "invalid seat completion continuation");
    return true;
}
static bool history(seat_io *io, qa_console_history **owned, size_t capacity) {
    qa_console_history *p = *owned;
    size_t saved_capacity = p ? p->capacity : 0;
    if (!count(io, &saved_capacity, capacity) || saved_capacity != capacity)
        return invalid(io, "seat history capacity changed");
    if (io->reading) {
        p = qa_console_history_create(capacity, io->error);
        if (!p) return false;
        *owned = p;
    }
    if (!p || !count(io, &p->count, capacity) || !count(io, &p->first, capacity - 1) ||
        !count(io, &p->position, p->count) || !text(io, &p->draft, SIZE_MAX - 1)) return false;
    for (size_t i = 0; i < capacity; ++i) {
        if (!text(io, &p->entries[i], SIZE_MAX - 1)) return false;
        bool present = false;
        for (size_t j = 0; j < p->count; ++j)
            if ((p->first + j) % capacity == i) present = true;
        if (present != (p->entries[i] != NULL) || (present && !*p->entries[i]))
            return invalid(io, "seat history ring differs from its entries");
    }
    return true;
}
static bool buffer(seat_io *io, qa_console_buffer **owned, size_t character_capacity) {
    qa_console_buffer *p = *owned;
    size_t saved_capacity = p ? p->character_capacity : 0, width = p ? p->width : 0;
    uint32_t dialect = p ? (uint32_t)p->dialect : 0;
    if (!count(io, &saved_capacity, character_capacity) || saved_capacity != character_capacity ||
        !count(io, &width, character_capacity) || !width || !u32(io, &dialect) ||
        !qac_dialect_valid((qa_console_dialect)dialect))
        return invalid(io, "invalid seat console scrollback dimensions");
    if (io->reading) {
        p = qa_console_buffer_create((qa_console_dialect)dialect, width, character_capacity, io->error);
        if (!p) return false;
        *owned = p;
    }
    if (!p || !count(io, &p->first, p->capacity - 1) || !count(io, &p->count, p->capacity) ||
        !p->count || !count(io, &p->write, p->count - 1) || !count(io, &p->x, width - 1) ||
        !count(io, &p->backscroll, p->count - 1) || !u64(io, &p->sequence))
        return invalid(io, "invalid seat console scrollback cursor");
    for (size_t i = 0; i < p->count; ++i) {
        size_t at = (p->first + i) % p->capacity;
        row_state *row = &p->rows[at];
        if (!u64(io, &row->sequence) || !count(io, &row->count, width) ||
            !f64(io, &row->time) || !boolean(io, &row->notify) || !boolean(io, &row->wrapped))
            return false;
        for (size_t j = 0; j < row->count; ++j) {
            qa_console_cell *cell = &p->cells[at * width + j];
            if (!u32(io, &cell->scalar) || !scalar(cell->scalar) ||
                !bytes(io, &cell->color, 1) || cell->color > 7 || !boolean(io, &cell->alternate))
                return invalid(io, "invalid seat console scrollback cell");
        }
    }
    return true;
}
static bool lines(seat_io *io, qa_seat_console *seat) {
    size_t n = 0;
    if (!io->reading) for (staged_line *p = seat->staged_first; p; p = p->next) ++n;
    size_t limit = io->reading ? (io->input.size - io->offset) / 21 : SIZE_MAX;
    if (!count(io, &n, limit) || (!seat->staged && n))
        return invalid(io, "unstaged seat console has pending publication rows");
    staged_line *p = seat->staged_first;
    for (size_t i = 0; i < n; ++i) {
        uint32_t dialect = p ? (uint32_t)p->dialect : 0;
        double time = p ? p->time : 0;
        char *line = io->reading ? NULL : p->text;
        if (!u32(io, &dialect) || !qac_dialect_valid((qa_console_dialect)dialect) ||
            !f64(io, &time) || !text(io, &line, SIZE_MAX - sizeof(staged_line) - 1)) return false;
        if (!line) return invalid(io, "seat publication row has no text");
        if (io->reading) {
            size_t length = strlen(line);
            staged_line *next = malloc(sizeof(*next) + length + 1);
            if (!next) {
                free(line);
                return qac_fail(io->error, QA_ERROR_MEMORY, "retaining staged seat output");
            }
            *next = (staged_line){.dialect = (qa_console_dialect)dialect, .time = time};
            memcpy(next->text, line, length + 1);
            free(line);
            if (seat->staged_last) seat->staged_last->next = next;
            else seat->staged_first = next;
            seat->staged_last = next;
        } else p = p->next;
    }
    return true;
}
static uint32_t capabilities(const qa_seat_console_options *options) {
    return (options->now_ms ? 1u : 0u) | (options->connected ? 2u : 0u) |
        (options->focus ? 4u : 0u) | (options->chat ? 8u : 0u) | (options->clipboard ? 16u : 0u) |
        (options->context_ready ? 32u : 0u);
}
static bool continuation(seat_io *io, qa_seat_console *seat, const qa_seat_console_options *candidate,
                          const qa_seat_console_save_resolvers *resolve, qa_bytes identity) {
    uint32_t magic = 0x43534151u, version = 2, cap = capabilities(candidate), physical = candidate->seat;
    if (!u32(io, &magic) || magic != 0x43534151u || !u32(io, &version) || version != 2 ||
        !u32(io, &cap) || cap != capabilities(candidate))
        return invalid(io, "seat console continuation owner or version changed");
    if (!u32(io, &physical) || physical >= 4 || physical != candidate->seat)
        return invalid(io, "seat console continuation names another physical route");
    qa_command_context *command = &seat->options.command;
    uint32_t dialect = command->dialect, origin = command->origin;
    if (!u32(io, &dialect) || !qac_dialect_valid((qa_console_dialect)dialect) ||
        !u32(io, &origin) || origin > QA_COMMAND_REMOTE || !u32(io, &command->seat) ||
        !boolean(io, &command->direct) || !boolean(io, &command->console_text) ||
        !text(io, &seat->script, SIZE_MAX - 1)) return false;
    command->dialect = (qa_console_dialect)dialect;
    command->origin = (qa_command_origin)origin;
    command->script = seat->script;
    size_t size = identity.size;
    size_t maximum = io->reading ? io->input.size - io->offset : SIZE_MAX;
    if (!count(io, &size, maximum)) return false;
    if (io->reading) {
        if (io->offset > io->input.size || size > io->input.size - io->offset)
            return invalid(io, "truncated seat command identity");
        qa_bytes saved = {io->input.data + io->offset, size};
        io->offset += size;
        qa_command_context shape = *command;
        if (!resolve->command_decode(resolve->context, candidate, saved, command, io->error))
            return false;
        if (command->dialect != shape.dialect || command->origin != shape.origin ||
            command->seat != shape.seat || command->direct != shape.direct ||
            command->console_text != shape.console_text || command->script != shape.script)
            return invalid(io, "seat context resolver changed source command semantics");
    } else if (!bytes(io, (void *)identity.data, size)) return false;
    if (!f32(io, &seat->fraction)) return false;
    uint32_t target;
    memcpy(&target, &seat->chat_target, sizeof(target));
    if (!u32(io, &target)) return false;
    if (io->reading) memcpy(&seat->chat_target, &target, sizeof(target));
    return boolean(io, &seat->staged) && boolean(io, &seat->opened) &&
        boolean(io, &seat->suppress_toggle_text) && boolean(io, &seat->targeted) &&
        boolean(io, &seat->control) && boolean(io, &seat->shift) &&
        field(io, &seat->field, 1023) && field(io, &seat->chat, 1023) &&
        history(io, &seat->history, 32) && buffer(io, &seat->buffer, 32768) && lines(io, seat);
}
bool qa_seat_console_save_capture(qa_seat_console *seat, const qa_seat_console_save_resolvers *resolve,
                                  qa_buffer *out, qa_error *error) {
    if (!out || out->data || out->size || !resolve || !resolve->command_encode ||
        !qa_seat_console_idle(seat) ||
        seat->options.command.script != seat->script)
        return qac_fail(error, QA_ERROR_ARGUMENT, "seat console capture requires its idle actual owner");
    if (!qac_seat_context_ready(&seat->options, &seat->options.command, error)) return false;
    ++seat->active_depth;
    qa_buffer identity = {0};
    seat_io io = {.error = error};
    bool okay = resolve->command_encode(resolve->context, &seat->options, &identity, error);
    if (okay && identity.size && !identity.data)
        okay = qac_fail(error, QA_ERROR_ARGUMENT, "seat context encoder returned absent identity bytes");
    if (okay) okay = continuation(&io, seat, &seat->options, resolve,
                                   (qa_bytes){identity.data, identity.size});
    if (okay) okay = qac_text_finish(&io.output, out, error);
    free(identity.data);
    free(io.output.data);
    --seat->active_depth;
    return okay;
}
bool qa_seat_console_save_restore(qa_seat_console *seat, const qa_seat_console_save_resolvers *resolve,
                                  qa_bytes input, qa_error *error) {
    if ((!input.data && input.size) || !resolve || !resolve->command_decode ||
        !qa_seat_console_idle(seat))
        return qac_fail(error, QA_ERROR_ARGUMENT, "seat console restore requires its idle actual owner");
    qa_seat_console *scratch = calloc(1, sizeof(*scratch));
    if (!scratch) return qac_fail(error, QA_ERROR_MEMORY, "retaining seat console candidate");
    scratch->options = seat->options;
    scratch->options.command = (qa_command_context){0};
    ++seat->active_depth;
    seat_io io = {.reading = true, .input = input, .error = error};
    bool okay = continuation(&io, scratch, &seat->options, resolve, (qa_bytes){0});
    if (okay && io.offset != input.size) okay = invalid(&io, "trailing seat console continuation bytes");
    if (okay) okay = qac_seat_context_ready(&scratch->options, &scratch->options.command, error);
    if (okay) {
        qa_text_field *field = scratch->field, *chat = scratch->chat;
        qa_console_history *history = scratch->history;
        qa_console_buffer *buffer = scratch->buffer;
        qa_text_field old_field = *seat->field, old_chat = *seat->chat;
        qa_console_history old_history = *seat->history;
        qa_console_buffer old_buffer = *seat->buffer;
        *seat->field = *field;
        *seat->chat = *chat;
        *seat->history = *history;
        *seat->buffer = *buffer;
        *field = old_field;
        *chat = old_chat;
        *history = old_history;
        *buffer = old_buffer;
        qa_seat_console old = *seat;
        scratch->active_depth = old.active_depth;
        *seat = *scratch;
        *scratch = old;
        seat->field = old.field;
        seat->chat = old.chat;
        seat->history = old.history;
        seat->buffer = old.buffer;
        scratch->field = field;
        scratch->chat = chat;
        scratch->history = history;
        scratch->buffer = buffer;
    }
    --seat->active_depth;
    qa_seat_console_destroy(scratch);
    return okay;
}

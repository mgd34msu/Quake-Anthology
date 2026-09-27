#include "qa/field.h"
#include "qa/input.h"
#include "qa/text.h"
#include <stdlib.h>
#include <string.h>

struct qa_text_field {
    uint32_t *characters;
    char *text;
    size_t maximum, length, cursor, scroll, width;
    bool overstrike, dirty, selected;
    char **matches, *tail;
    size_t match_count, match_index;
};
struct qa_console_history {
    char **entries, *draft;
    size_t capacity, count, first, position;
};
static bool fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_MEMORY, 0, "%s", message);
    return false;
}
static char *copy(const char *text, qa_error *e) {
    size_t n = strlen(text) + 1;
    char *result = malloc(n);
    if (!result)
        fail(e, "Allocating console text");
    else
        memcpy(result, text, n);
    return result;
}
static void completion_clear(qa_text_field *field) {
    for (size_t i = 0; i < field->match_count; ++i)
        free(field->matches[i]);
    free(field->matches);
    free(field->tail);
    field->matches = NULL;
    field->tail = NULL;
    field->match_count = 0;
    field->selected = false;
}
static void visible(qa_text_field *field) {
    if (field->scroll > field->cursor)
        field->scroll = field->cursor;
    if (field->cursor - field->scroll >= field->width)
        field->scroll = field->cursor - (field->width ? field->width : 1) + 1;
}
qa_text_field *qa_text_field_create(size_t maximum, size_t width, qa_error *e) {
    if (maximum > (SIZE_MAX - 1) / 4) {
        fail(e, "Console field capacity overflow");
        return NULL;
    }
    qa_text_field *field = calloc(1, sizeof(*field));
    if (!field) {
        fail(e, "Allocating console field");
        return NULL;
    }
    field->characters = maximum ? malloc(maximum * sizeof(*field->characters)) : NULL;
    field->text = malloc(maximum * 4 + 1);
    if ((maximum && !field->characters) || !field->text) {
        qa_text_field_destroy(field);
        fail(e, "Allocating console field text");
        return NULL;
    }
    field->maximum = maximum;
    field->width = width;
    field->text[0] = 0;
    return field;
}
void qa_text_field_destroy(qa_text_field *field) {
    if (!field)
        return;
    completion_clear(field);
    free(field->characters);
    free(field->text);
    free(field);
}
void qa_text_field_clear(qa_text_field *field) {
    completion_clear(field);
    field->length = field->cursor = field->scroll = 0;
    field->text[0] = 0;
    field->dirty = false;
}
static void assign(qa_text_field *field, qa_bytes text) {
    field->length = 0;
    size_t at = 0;
    uint32_t code;
    while (field->length < field->maximum && qa_utf8_next(text, &at, &code))
        if (code >= 32 && code != 127)
            field->characters[field->length++] = code;
    field->cursor = field->length;
    field->dirty = true;
    visible(field);
}
void qa_text_field_set(qa_text_field *field, const char *text) {
    completion_clear(field);
    assign(field, (qa_bytes){(const uint8_t *)text, strlen(text)});
}
void qa_text_field_insert(qa_text_field *field, qa_bytes text) {
    completion_clear(field);
    size_t at = 0;
    uint32_t code;
    while (qa_utf8_next(text, &at, &code)) {
        if (code < 32 || code == 127)
            continue;
        if (field->overstrike && field->cursor < field->length)
            field->characters[field->cursor++] = code;
        else if (field->length < field->maximum) {
            memmove(field->characters + field->cursor + 1, field->characters + field->cursor,
                    (field->length - field->cursor) * sizeof(*field->characters));
            field->characters[field->cursor++] = code;
            ++field->length;
        }
    }
    field->dirty = true;
    visible(field);
}
void qa_text_field_cursor(qa_text_field *field, size_t cursor) {
    completion_clear(field);
    field->cursor = cursor < field->length ? cursor : field->length;
    visible(field);
}
void qa_text_field_width(qa_text_field *field, size_t width) {
    field->width = width;
    visible(field);
}
bool qa_text_field_overstrike(const qa_text_field *field) { return field->overstrike; }
void qa_text_field_set_overstrike(qa_text_field *field, bool value) { field->overstrike = value; }
qa_field_view qa_text_field_read(qa_text_field *field) {
    if (field->dirty) {
        size_t n = 0;
        for (size_t i = 0; i < field->length; ++i)
            n += qa_utf8_encode(field->characters[i], field->text + n);
        field->text[n] = 0;
        field->dirty = false;
    }
    return (qa_field_view){
        field->text,      field->selected ? field->matches[field->match_index] : NULL,
        field->cursor,    field->scroll,
        field->length,    field->width,
        field->overstrike};
}
static size_t word(const qa_text_field *field, bool backward) {
    size_t at = field->cursor;
    while ((backward ? at > 0 : at < field->length) &&
           qa_unicode_whitespace(field->characters[backward ? at - 1 : at]))
        at = backward ? at - 1 : at + 1;
    while ((backward ? at > 0 : at < field->length) &&
           !qa_unicode_whitespace(field->characters[backward ? at - 1 : at]))
        at = backward ? at - 1 : at + 1;
    return at;
}
static void erase(qa_text_field *field, size_t first, size_t last) {
    if (first == last)
        return;
    memmove(field->characters + first, field->characters + last,
            (field->length - last) * sizeof(*field->characters));
    field->length -= last - first;
    field->dirty = true;
}
bool qa_text_field_key(qa_text_field *field, int key, const qa_field_controls *controls,
                       bool *handled, qa_error *e) {
    *handled = true;
    if (key != QA_KEY_SHIFT && key != QA_KEY_CONTROL)
        completion_clear(field);
    if ((key == 'v' && controls->control) ||
        ((key == QA_KEY_INSERT || key == QA_KEY_KP_INSERT) && controls->shift)) {
        if (controls->clipboard) {
            qa_buffer text = {0};
            if (!controls->clipboard(controls->context, &text, e)) {
                qa_buffer_free(&text);
                return false;
            }
            size_t n = 0;
            while (n < text.size && text.data[n] && text.data[n] != '\r' && text.data[n] != '\n')
                ++n;
            qa_text_field_insert(field, (qa_bytes){text.data, n});
            qa_buffer_free(&text);
        }
        return true;
    }
    if (controls->control)
        switch (key) {
        case 'a':
            field->cursor = 0;
            goto done;
        case 'e':
            field->cursor = field->length;
            goto done;
        case 'c':
        case 'u':
            qa_text_field_clear(field);
            goto done;
        case 'k':
            erase(field, field->cursor, field->length);
            goto done;
        case 'w': {
            size_t first = word(field, true);
            erase(field, first, field->cursor);
            field->cursor = first;
            goto done;
        }
        }
    switch (key) {
    case QA_KEY_BACKSPACE:
        if (field->cursor) {
            erase(field, field->cursor - 1, field->cursor);
            --field->cursor;
        }
        break;
    case QA_KEY_DELETE:
        if (field->cursor < field->length)
            erase(field, field->cursor, field->cursor + 1);
        break;
    case QA_KEY_LEFT:
        field->cursor = controls->control ? word(field, true)
                        : field->cursor   ? field->cursor - 1
                                          : 0;
        break;
    case QA_KEY_RIGHT:
        field->cursor = controls->control               ? word(field, false)
                        : field->cursor < field->length ? field->cursor + 1
                                                        : field->length;
        break;
    case QA_KEY_HOME:
        field->cursor = 0;
        break;
    case QA_KEY_END:
        field->cursor = field->length;
        break;
    case QA_KEY_INSERT:
        field->overstrike = !field->overstrike;
        break;
    default:
        *handled = false;
        return true;
    }
done:
    visible(field);
    return true;
}
static unsigned fold(unsigned c) { return c >= 'A' && c <= 'Z' ? c + 'a' - 'A' : c; }
static int compare_text(const char *a, const char *b) {
    while (*a && fold((unsigned char)*a) == fold((unsigned char)*b)) {
        ++a;
        ++b;
    }
    unsigned x = fold((unsigned char)*a), y = fold((unsigned char)*b);
    return x < y ? -1 : x > y;
}
static int compare_names(const void *a, const void *b) {
    return compare_text(*(const char *const *)a, *(const char *const *)b);
}
static bool starts(const char *name, const char *prefix, size_t length) {
    for (size_t i = 0; i < length; ++i)
        if (!name[i] || fold((unsigned char)name[i]) != fold((unsigned char)prefix[i]))
            return false;
    return true;
}
bool qa_text_field_complete(qa_text_field *field, const char *const *names, size_t count,
                            bool reverse, qa_error *e) {
    if (!field->match_count) {
        size_t first =
            field->length && (field->characters[0] == '/' || field->characters[0] == '\\') ? 1 : 0;
        size_t end = first;
        while (end < field->length && !qa_unicode_whitespace(field->characters[end]))
            ++end;
        if (field->cursor < first || field->cursor > end)
            return true;
        qa_field_view view = qa_text_field_read(field);
        size_t prefix_bytes = 0, end_bytes = 0;
        for (size_t i = first; i < field->cursor; ++i) {
            char tmp[4];
            prefix_bytes += qa_utf8_encode(field->characters[i], tmp);
        }
        for (size_t i = 0; i < end; ++i) {
            char tmp[4];
            end_bytes += qa_utf8_encode(field->characters[i], tmp);
        }
        if (count > SIZE_MAX / sizeof(char *))
            return fail(e, "Completion list overflow");
        field->matches = count ? calloc(count, sizeof(*field->matches)) : NULL;
        field->tail = copy(view.text + end_bytes, e);
        if ((count && !field->matches) || !field->tail) {
            completion_clear(field);
            return fail(e, "Allocating console completion");
        }
        for (size_t i = 0; i < count; ++i) {
            if (!names[i] || !starts(names[i], view.text + first, prefix_bytes))
                continue;
            bool duplicate = false;
            for (size_t j = 0; j < field->match_count; ++j)
                if (!compare_text(field->matches[j], names[i])) {
                    duplicate = true;
                    break;
                }
            if (duplicate)
                continue;
            char *name = copy(names[i], e);
            if (!name) {
                completion_clear(field);
                return false;
            }
            field->matches[field->match_count++] = name;
        }
        if (!field->match_count) {
            completion_clear(field);
            return true;
        }
        qsort(field->matches, field->match_count, sizeof(*field->matches), compare_names);
        field->match_index = reverse ? 0 : field->match_count - 1;
    }
    size_t index = reverse ? (field->match_index ? field->match_index - 1 : field->match_count - 1)
                           : (field->match_index + 1) % field->match_count;
    const char *selected = field->matches[index];
    size_t selected_bytes = strlen(selected), tail_bytes = strlen(field->tail);
    if (selected_bytes > SIZE_MAX - tail_bytes - 2)
        return fail(e, "Completion text overflow");
    char *text = malloc(selected_bytes + tail_bytes + 2);
    if (!text)
        return fail(e, "Allocating completed console text");
    text[0] = '/';
    memcpy(text + 1, selected, selected_bytes);
    memcpy(text + selected_bytes + 1, field->tail, tail_bytes + 1);
    qa_bytes utf8 = {(const uint8_t *)text, selected_bytes + tail_bytes + 1};
    size_t at = 0, scalars = 0, cursor = 1;
    uint32_t code;
    while (qa_utf8_next(utf8, &at, &code)) {
        ++scalars;
        if (at <= selected_bytes + 1)
            cursor = scalars;
    }
    if (scalars <= field->maximum) {
        assign(field, utf8);
        field->cursor = cursor;
        visible(field);
        field->match_index = index;
        field->selected = true;
    } else if (!field->selected)
        completion_clear(field);
    free(text);
    return true;
}
qa_console_history *qa_console_history_create(size_t capacity, qa_error *e) {
    if (!capacity || capacity > SIZE_MAX / sizeof(char *)) {
        fail(e, "Invalid console history capacity");
        return NULL;
    }
    qa_console_history *history = calloc(1, sizeof(*history));
    if (!history) {
        fail(e, "Allocating console history");
        return NULL;
    }
    history->entries = calloc(capacity, sizeof(*history->entries));
    if (!history->entries) {
        free(history);
        fail(e, "Allocating console history entries");
        return NULL;
    }
    history->capacity = capacity;
    return history;
}
void qa_console_history_destroy(qa_console_history *history) {
    if (!history)
        return;
    for (size_t i = 0; i < history->capacity; ++i)
        free(history->entries[i]);
    free(history->entries);
    free(history->draft);
    free(history);
}
bool qa_console_history_add(qa_console_history *history, const char *text, qa_error *e) {
    if (!*text)
        return true;
    char *line = copy(text, e);
    if (!line)
        return false;
    size_t slot = (history->first + history->count) % history->capacity;
    free(history->entries[slot]);
    history->entries[slot] = line;
    if (history->count == history->capacity)
        history->first = (history->first + 1) % history->capacity;
    else
        ++history->count;
    history->position = history->count;
    free(history->draft);
    history->draft = NULL;
    return true;
}
bool qa_console_history_replace(qa_console_history *history, const char *const *lines, size_t count,
                                qa_error *e) {
    qa_console_history *candidate = qa_console_history_create(history->capacity, e);
    if (!candidate)
        return false;
    for (size_t i = 0; i < count; ++i)
        if (!qa_console_history_add(candidate, lines[i], e)) {
            qa_console_history_destroy(candidate);
            return false;
        }
    qa_console_history old = *history;
    *history = *candidate;
    *candidate = old;
    qa_console_history_destroy(candidate);
    return true;
}
size_t qa_console_history_count(const qa_console_history *history) { return history->count; }
const char *qa_console_history_at(const qa_console_history *history, size_t i) {
    return i < history->count ? history->entries[(history->first + i) % history->capacity] : NULL;
}
bool qa_console_history_previous(qa_console_history *history, const char *draft, const char **out,
                                 qa_error *e) {
    if (history->position == history->count) {
        char *text = copy(draft, e);
        if (!text)
            return false;
        free(history->draft);
        history->draft = text;
    }
    if (history->position)
        --history->position;
    const char *line = qa_console_history_at(history, history->position);
    *out = line ? line : history->draft ? history->draft : draft;
    return true;
}
const char *qa_console_history_next(qa_console_history *history) {
    if (history->position < history->count)
        ++history->position;
    const char *line = qa_console_history_at(history, history->position);
    return line ? line : history->draft ? history->draft : "";
}

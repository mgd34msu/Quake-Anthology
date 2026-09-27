#include "qa/field.h"
#include "qa/input.h"
#include <string.h>

typedef struct paste_state {
    unsigned depth;
    size_t operations;
} paste_state;
static bool invalid(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static bool length_of(const qa_byte_field *field, int32_t *length, qa_error *error) {
    const uint8_t *end = memchr(field->text, 0, sizeof(field->text));
    if (!end)
        return invalid(error, "Unterminated source edit field");
    *length = (int32_t)(end - field->text);
    if (field->cursor < 0 || field->cursor > *length)
        return invalid(error, "Source edit cursor outside text");
    return true;
}
static int32_t scroll_add(int32_t value, int32_t delta) {
    int64_t next = (int64_t)value + delta;
    return next < INT32_MIN ? INT32_MIN : next > INT32_MAX ? INT32_MAX : (int32_t)next;
}
void qa_byte_field_clear(qa_byte_field *field) {
    memset(field->text, 0, sizeof(field->text));
    field->cursor = field->scroll = 0;
}
bool qa_byte_field_set(qa_byte_field *field, qa_bytes text, qa_error *error) {
    if (text.size >= sizeof(field->text) ||
        (text.size && (!text.data || memchr(text.data, 0, text.size))))
        return invalid(error, "Source edit text exceeds 255 bytes or contains NUL");
    if (text.size)
        memmove(field->text, text.data, text.size);
    field->text[text.size] = 0;
    return true;
}
static bool character(qa_byte_field *, int32_t, const qa_field_controls *, bool *, paste_state *,
                      qa_error *);
static bool paste(qa_byte_field *field, const qa_field_controls *controls, bool *overstrike,
                  paste_state *state, qa_error *error) {
    if (!controls->clipboard)
        return true;
    if (state->depth == 32)
        return invalid(error, "Recursive source paste exceeds 32 clipboard reads");
    qa_buffer text = {0};
    if (!controls->clipboard(controls->context, &text, error)) {
        qa_buffer_free(&text);
        return false;
    }
    bool ok = true;
    ++state->depth;
    for (size_t i = 0; i < text.size && text.data[i]; ++i) {
        if (++state->operations > 65536) {
            ok = invalid(error, "Source paste exceeds 65536 byte operations");
            break;
        }
        int32_t code = text.data[i] < 128 ? text.data[i] : (int32_t)text.data[i] - 256;
        if (!character(field, code, controls, overstrike, state, error)) {
            ok = false;
            break;
        }
    }
    --state->depth;
    qa_buffer_free(&text);
    return ok;
}
bool qa_byte_field_key(qa_byte_field *field, int key, const qa_field_controls *controls,
                       bool *overstrike, qa_error *error) {
    if ((key == QA_KEY_INSERT || key == QA_KEY_KP_INSERT) && controls->shift) {
        paste_state state = {0};
        return paste(field, controls, overstrike, &state, error);
    }
    int32_t length;
    if (!length_of(field, &length, error))
        return false;
    if (key == QA_KEY_DELETE) {
        if (field->cursor < length)
            memmove(field->text + field->cursor, field->text + field->cursor + 1,
                    (size_t)(length - field->cursor));
    } else if (key == QA_KEY_RIGHT) {
        if (field->cursor < length)
            ++field->cursor;
        if ((int64_t)field->cursor >= (int64_t)field->scroll + field->width &&
            field->cursor <= length)
            field->scroll = scroll_add(field->scroll, 1);
    } else if (key == QA_KEY_LEFT) {
        if (field->cursor > 0)
            --field->cursor;
        if (field->cursor < field->scroll)
            field->scroll = scroll_add(field->scroll, -1);
    } else if (key == QA_KEY_HOME || ((key == 'a' || key == 'A') && controls->control))
        field->cursor = 0;
    else if (key == QA_KEY_END || ((key == 'e' || key == 'E') && controls->control))
        field->cursor = length;
    else if (key == QA_KEY_INSERT)
        *overstrike = !*overstrike;
    return true;
}
static bool character(qa_byte_field *field, int32_t code, const qa_field_controls *controls,
                      bool *overstrike, paste_state *state, qa_error *error) {
    if (code == 22)
        return paste(field, controls, overstrike, state, error);
    if (code == 3) {
        qa_byte_field_clear(field);
        return true;
    }
    int32_t length;
    if (!length_of(field, &length, error))
        return false;
    if (code == 8) {
        if (field->cursor > 0) {
            memmove(field->text + field->cursor - 1, field->text + field->cursor,
                    (size_t)(length - field->cursor + 1));
            --field->cursor;
            if (field->cursor < field->scroll)
                field->scroll = scroll_add(field->scroll, -1);
        }
    } else if (code == 1)
        field->cursor = field->scroll = 0;
    else if (code == 5) {
        field->cursor = length;
        int64_t scroll = (int64_t)length - field->width;
        field->scroll = scroll > INT32_MAX ? INT32_MAX : (int32_t)scroll;
    } else if (code >= 32) {
        if (code > 255)
            return invalid(error, "Source edit characters require bytes");
        if (*overstrike) {
            if (field->cursor == 255)
                return true;
        } else {
            if (length == 255)
                return true;
            memmove(field->text + field->cursor + 1, field->text + field->cursor,
                    (size_t)(length - field->cursor + 1));
        }
        field->text[field->cursor++] = (uint8_t)code;
        if (field->cursor >= field->width)
            field->scroll = scroll_add(field->scroll, 1);
        if (field->cursor == length + 1)
            field->text[field->cursor] = 0;
    }
    return true;
}
bool qa_byte_field_character(qa_byte_field *field, int32_t code, const qa_field_controls *controls,
                             bool *overstrike, qa_error *error) {
    paste_state state = {0};
    return character(field, code, controls, overstrike, &state, error);
}

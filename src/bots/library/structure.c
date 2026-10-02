#include "internal.h"

bool bot_read_string(qa_script *s, char *out, size_t capacity, qa_error *e) {
    qa_script_token token;
    if (!bot_token(s, &token, e))
        return false;
    if (token.kind != QA_SCRIPT_STRING)
        return bot_fail(s, "Expected quoted bot resource string", e);
    qa_bytes value = qa_script_token_value(&token);
    size_t size = 0;
    while (size < value.size && size + 1 < capacity && value.data[size] != 0) {
        out[size] = (char)value.data[size];
        ++size;
    }
    out[size] = 0;
    return true;
}
static bool number(qa_script *s, bool integer, double *out, qa_error *e) {
    qa_script_token token;
    if (!bot_token(s, &token, e))
        return false;
    bool negative = qa_script_token_is(&token, "-");
    if (negative && !bot_token(s, &token, e))
        return false;
    bool floating = (token.subtype & QA_SCRIPT_FLOAT) != 0;
    if (token.kind != QA_SCRIPT_NUMBER || (floating && integer))
        return bot_fail(s, "Expected bot structure number", e);
    double value = floating ? token.number : token.integer;
    if (negative)
        value = -value;
    if (!isfinite(value) || !isfinite((float)value) ||
        (integer && (value < -32768 || value > 32767)))
        return bot_fail(s, "Bot structure number is outside source range", e);
    *out = value;
    return true;
}
static bool vector(qa_script *s, qa_vec3 *out, qa_error *e) {
    if (!qa_script_expect(s, "{", e))
        return false;
    for (size_t i = 0; i < 3; ++i) {
        bool closed;
        if (!qa_script_check(s, "}", &closed, e))
            return false;
        if (closed)
            return true;
        double value;
        if (!number(s, false, &value, e))
            return false;
        if (i == 0)
            out->x = (float)value;
        else if (i == 1)
            out->y = (float)value;
        else
            out->z = (float)value;
        qa_script_token token;
        if (!bot_token(s, &token, e))
            return false;
        if (qa_script_token_is(&token, "}"))
            return true;
        if (!qa_script_token_is(&token, ","))
            return bot_fail(s, "Expected vector comma or closing brace", e);
    }
    return true;
}
bool bot_structure_source(qa_script *s, void *out, const bot_field *fields, size_t field_count,
                          void *context, bool (*written)(void *, qa_error *), qa_error *e) {
    if (!qa_script_expect(s, "{", e))
        return false;
    for (;;) {
        qa_script_token token;
        if (!bot_token(s, &token, e))
            return false;
        if (qa_script_token_is(&token, "}"))
            return true;
        size_t index = 0;
        while (index < field_count && !qa_script_token_is(&token, fields[index].name))
            ++index;
        if (index == field_count)
            return bot_fail(s, "Unknown bot structure field", e);
        const bot_field *field = fields + index;
        uint8_t *target = (uint8_t *)out + field->offset;
        if (field->kind == BOT_FIELD_STRING) {
            if (!bot_read_string(s, (char *)target, field->size, e))
                return false;
        } else if (field->kind == BOT_FIELD_VECTOR) {
            qa_vec3 value;
            memcpy(&value, target, sizeof(value));
            if (!vector(s, &value, e))
                return false;
            memcpy(target, &value, sizeof(value));
        } else {
            double value;
            if (!number(s, field->kind == BOT_FIELD_INT, &value, e))
                return false;
            if (field->kind == BOT_FIELD_INT) {
                int32_t integer = (int32_t)value;
                memcpy(target, &integer, sizeof(integer));
            } else {
                float number = (float)value;
                memcpy(target, &number, sizeof(number));
            }
        }
        if (written && !written(context, e)) return false;
    }
}
bool bot_structure(qa_script *s, void *out, const bot_field *fields, size_t field_count,
                   qa_error *e) {
    return bot_structure_source(s, out, fields, field_count, NULL, NULL, e);
}

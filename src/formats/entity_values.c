#include "qa/bsp.h"
#include "qa/text.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool fail(qa_error *error, const char *message) {
    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
static qa_bytes numeric_text(qa_bytes text) {
    if (text.size > 127)
        text.size = 127;
    const uint8_t *zero = text.size ? memchr(text.data, 0, text.size) : NULL;
    if (zero)
        text.size = (size_t)(zero - text.data);
    return text;
}
static bool space(uint8_t c) { return c == ' ' || (c >= '\t' && c <= '\r'); }
static bool digit(uint8_t c, bool hex) {
    return (c >= '0' && c <= '9') || (hex && ((c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F')));
}
static bool word(qa_bytes b, size_t at, const char *s) {
    for (; *s; ++s, ++at) {
        if (at >= b.size)
            return false;
        uint8_t c = b.data[at];
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
        if (c != (uint8_t)*s)
            return false;
    }
    return true;
}
/* Numeric scanners identify the source-consumed prefix. The common C-locale
 * conversion owns rounding, infinities, subnormals and NaN payloads. */
static bool number(qa_bytes b, size_t *cursor, bool scan, double *out, bool *converted,
                   qa_error *e) {
    size_t i = *cursor;
    while (i < b.size && space(b.data[i]))
        ++i;
    size_t begin = i;
    if (i < b.size && (b.data[i] == '+' || b.data[i] == '-'))
        ++i;
    size_t unsigned_start = i;
    if (word(b, i, "inf")) {
        i += 3;
        if (word(b, i, "inity"))
            i += 5;
        else if (scan && i < b.size && (b.data[i] == 'i' || b.data[i] == 'I')) {
            *converted = false;
            return true;
        }
    } else if (word(b, i, "nan")) {
        i += 3;
        if (i < b.size && b.data[i] == '(') {
            size_t payload = i++;
            while (i < b.size && ((b.data[i] >= 'a' && b.data[i] <= 'z') ||
                                  (b.data[i] >= 'A' && b.data[i] <= 'Z') ||
                                  digit(b.data[i], false) || b.data[i] == '_'))
                ++i;
            if (i < b.size && b.data[i] == ')')
                ++i;
            else if (scan) {
                *converted = false;
                return true;
            } else
                i = payload;
        }
    } else {
        bool hex = word(b, i, "0x");
        if (hex)
            i += 2;
        size_t digits = 0;
        while (i < b.size && digit(b.data[i], hex)) {
            ++i;
            ++digits;
        }
        if (i < b.size && b.data[i] == '.') {
            ++i;
            while (i < b.size && digit(b.data[i], hex)) {
                ++i;
                ++digits;
            }
        }
        if (!digits && hex && !scan) {
            i = unsigned_start + 1;
            digits = 1;
            hex = false;
        }
        if (!digits) {
            *converted = false;
            return true;
        }
        if (i < b.size && (b.data[i] == (hex ? 'p' : 'e') || b.data[i] == (hex ? 'P' : 'E'))) {
            size_t exponent = i++;
            if (i < b.size && (b.data[i] == '+' || b.data[i] == '-'))
                ++i;
            size_t first = i;
            while (i < b.size && digit(b.data[i], false))
                ++i;
            if (i == first) {
                if (scan) {
                    *converted = false;
                    return true;
                }
                i = exponent;
            }
        }
    }
    if (!qa_parse_number((qa_bytes){b.data + begin, i - begin}, out, e))
        return false;
    *converted = true;
    *cursor = i;
    return true;
}
bool qa_bsp_value_integer(qa_bytes text, int32_t *out, qa_error *e) {
    if (!out || (text.size && !text.data))
        return fail(e, "invalid BSP integer text/output");
    qa_bytes b = numeric_text(text);
    size_t i = 0;
    while (i < b.size && space(b.data[i]))
        ++i;
    bool negative = i < b.size && b.data[i] == '-';
    if (i < b.size && (b.data[i] == '+' || b.data[i] == '-'))
        ++i;
    uint32_t value = 0, limit = negative ? UINT32_C(2147483648) : INT32_MAX;
    while (i < b.size && digit(b.data[i], false)) {
        uint32_t d = b.data[i++] - '0';
        if (value > (limit - d) / 10)
            return fail(e, "BSP integer exceeds source signed range");
        value = value * 10 + d;
    }
    *out = negative ? value == UINT32_C(2147483648) ? INT32_MIN : -(int32_t)value : (int32_t)value;
    return true;
}
bool qa_bsp_value_float(qa_bytes text, float *out, qa_error *e) {
    if (!out || (text.size && !text.data))
        return fail(e, "invalid BSP float text/output");
    qa_bytes b = numeric_text(text);
    double value = 0;
    size_t cursor = 0;
    bool converted;
    if (!number(b, &cursor, false, &value, &converted, e))
        return false;
    *out = converted ? (float)value : 0;
    return true;
}
bool qa_bsp_value_vector(qa_bytes text, qa_vec3 *out, qa_error *e) {
    if (!out || (text.size && !text.data))
        return fail(e, "invalid BSP vector text/output");
    qa_bytes b = numeric_text(text);
    float axes[3] = {0};
    size_t cursor = 0;
    for (size_t i = 0; i < 3; ++i) {
        double value;
        bool converted;
        if (!number(b, &cursor, true, &value, &converted, e))
            return false;
        if (!converted)
            break;
        axes[i] = (float)value;
    }
    *out = qa_v3(axes[0], axes[1], axes[2]);
    return true;
}

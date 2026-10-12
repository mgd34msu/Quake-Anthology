#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "qa/text.h"
#include "qa/math.h"

#include <fenv.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <locale.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
static INIT_ONCE numeric_once = INIT_ONCE_STATIC_INIT;
static _locale_t numeric_locale;
static BOOL CALLBACK open_numeric_locale(PINIT_ONCE once, PVOID argument, PVOID *context) {
    (void)once;
    (void)argument;
    (void)context;
    numeric_locale = _create_locale(LC_NUMERIC, "C");
    return TRUE;
}
#else
#include <threads.h>
static once_flag numeric_once = ONCE_FLAG_INIT;
static locale_t numeric_locale;
static void open_numeric_locale(void) {
    numeric_locale = newlocale(LC_NUMERIC_MASK, "C", (locale_t)0);
}
#endif

/* Process-lifetime locale shared by all parsers, without changing UI locale. */
static bool ready(qa_error *error) {
#if defined(_WIN32)
    if (!InitOnceExecuteOnce(&numeric_once, open_numeric_locale, NULL, NULL)) {
        qa_error_set(error, QA_ERROR_IO, 0, "initializing numeric locale");
        return false;
    }
#else
    call_once(&numeric_once, open_numeric_locale);
#endif
    if (!numeric_locale) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "opening numeric locale");
        return false;
    }
    return true;
}

int32_t qa_source_to_i32(double value) {
    return value >= -2147483648.0 && value < 2147483648.0
        ? (int32_t)value : INT32_MIN;
}

static unsigned number_byte(qa_number_cursor *cursor) {
    if (cursor->offset > cursor->text.size) {
        cursor->overrun = true;
        return 0;
    }
    return cursor->offset < cursor->text.size ? cursor->text.data[cursor->offset] : 0;
}

static unsigned number_take(qa_number_cursor *cursor) {
    unsigned byte = number_byte(cursor);
    if (cursor->offset < SIZE_MAX) ++cursor->offset;
    else cursor->overrun = true;
    return byte;
}

static void number_space(qa_number_cursor *cursor, qa_decimal_rule rule) {
    unsigned byte;
    while ((byte = number_byte(cursor)) != 0) {
        bool space = rule == QA_DECIMAL_LOCALE_CLAMP ? isspace(byte) != 0 :
            rule == QA_DECIMAL_SIGNED_WRAP ? byte <= 32 || byte >= 128 :
            rule == QA_DECIMAL_CONTROL_WRAP ? byte <= 32 :
            byte == ' ' || (byte >= '\t' && byte <= '\r');
        if (!space) break;
        ++cursor->offset;
    }
}

int32_t qa_parse_decimal_i32_cursor(qa_number_cursor *cursor, qa_decimal_rule rule,
                                   bool consume_delimiter) {
    number_space(cursor, rule);
    if (number_byte(cursor) == 0) return 0;
    unsigned byte = number_byte(cursor);
    bool negative = byte == '-';
    if (byte == '-' || byte == '+') ++cursor->offset;
    uint32_t value = 0, limit = negative ? UINT32_C(2147483648) : INT32_MAX;
    bool clamp = rule == QA_DECIMAL_ASCII_CLAMP || rule == QA_DECIMAL_LOCALE_CLAMP;
    while ((byte = number_byte(cursor)) >= '0' && byte <= '9') {
        uint32_t digit = number_take(cursor) - '0';
        value = clamp && value > (limit - digit) / 10 ?
            limit : value * 10u + digit;
    }
    if (consume_delimiter) number_take(cursor);
    if (negative) value = 0u - value;
    return value <= INT32_MAX ? (int32_t)value : -1 - (int32_t)(UINT32_MAX - value);
}

int32_t qa_parse_decimal_i32(const char *text, qa_decimal_rule rule) {
    if (!text) return 0;
    qa_number_cursor cursor = {.text = {(const uint8_t *)text, SIZE_MAX}};
    return qa_parse_decimal_i32_cursor(&cursor, rule, false);
}

uint16_t qa_angle_to_word(float angle,qa_angle_word_rule rule) {
    double scaled;
    switch(rule) {
    case QA_ANGLE_WORD_PRECISE: scaled=(double)angle*(65536.0/360.0);break;
    case QA_ANGLE_WORD_VM: scaled=angle*(65536.0f/360.0f);break;
    case QA_ANGLE_WORD_ROUNDED: scaled=floorf(angle*(65536.0f/360.0f)+.5f);break;
    default: scaled=angle*65536.0f/360.0f;break;
    }
    int32_t integer=qa_source_to_i32(scaled);
    return (uint16_t)(uint32_t)integer;
}

float qa_angle_mod(float angle,qa_angle_word_rule rule) {
    return (float)qa_angle_to_word(angle,rule)*(360.0f/65536.0f);
}
float qa_angle_wrap(float angle) {
    float result=fmodf(angle,360);
    return result<0?result+360:result;
}
float qa_angle_subtract(float first,float second) {
    float result=first-second;
    while(result>180) result-=360;
    while(result< -180) result+=360;
    return result;
}
float qa_angle_delta(float target,float current) {
    return qa_angle_subtract(qa_angle_wrap(target),qa_angle_wrap(current));
}

bool qa_format_quake_float(float value, char out[64], qa_error *error) {
    if (!out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid Quake float output");
        return false;
    }
    int32_t integer = qa_source_to_i32(value);
    if (value == (float)integer) {
        (void)snprintf(out, 64, "%d", integer);
        return true;
    }
    if (!ready(error)) return false;
#if defined(_WIN32)
    int count = _snprintf_l(out, 64, "%5.1f", numeric_locale, (double)value);
#else
    locale_t previous = uselocale(numeric_locale);
    if (!previous) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "selecting numeric locale");
        return false;
    }
    int count = snprintf(out, 64, "%5.1f", (double)value);
    uselocale(previous);
#endif
    if (count < 0 || count >= 64) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "formatting Quake float");
        return false;
    }
    return true;
}

double qa_parse_quake_number_cursor(qa_number_cursor *cursor, qa_quake_number_policy policy,
                                    bool scan) {
    if (policy == QA_QUAKE_NUMBER_Q3_VM || policy == QA_QUAKE_NUMBER_Q3_BYTECODE) {
        bool bytecode = policy == QA_QUAKE_NUMBER_Q3_BYTECODE;
        number_space(cursor, QA_DECIMAL_SIGNED_WRAP);
        unsigned character = number_byte(cursor);
        if (character == 0) return 0;
        float sign = 1, value = 0;
        if (character == '-' || character == '+') {
            if (character == '-')
                sign = -1;
            ++cursor->offset;
        }
        character = scan ? '0' : number_byte(cursor);
        if (number_byte(cursor) != '.') {
            for (;;) {
                character = number_take(cursor);
                if (character < '0' || character > '9') break;
                unsigned digit = character - '0';
                if (bytecode) {
                    /* MULF and ADDF are distinct four-byte source operations. */
                    float product = (float)((double)value * 10.0);
                    value = (float)((double)product + digit);
                } else
                    value = value * 10 + (float)digit;
            }
        } else if (!scan)
            ++cursor->offset;
        if (character == '.') {
            double fraction = bytecode ? (double)0.1f : 0.1;
            for (;;) {
                character = number_take(cursor);
                if (character < '0' || character > '9') break;
                unsigned digit = character - '0';
                if (bytecode) {
                    float product = (float)((double)digit * fraction);
                    value = (float)((double)value + product);
                    fraction = (float)(fraction * (double)0.1f);
                } else {
                    value = (float)((double)value + digit * fraction);
                    fraction *= 0.1;
                }
            }
        }
        return value * sign;
    }
    int sign = 1;
    if (number_byte(cursor) == '-') {
        sign = -1;
        ++cursor->offset;
    }
    double value = 0;
    unsigned byte = number_byte(cursor), next = 0;
    if ((byte == '0' || byte == '\'') && cursor->offset < cursor->text.size &&
        cursor->text.size - cursor->offset > 1)
        next = cursor->text.data[cursor->offset + 1];
    if (byte == '0' && (next == 'x' || next == 'X')) {
        cursor->offset += 2;
        int c;
        while ((c = (int)number_byte(cursor)) != 0) {
            int digit = c >= '0' && c <= '9' ? c - '0' :
                c >= 'a' && c <= 'f' ? c - 'a' + 10 :
                c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
            if (digit < 0)
                break;
            value = value * 16 + digit;
            ++cursor->offset;
        }
    } else if (byte == '\'') {
        int quoted = (int)next;
        if ((policy & QA_QUAKE_NUMBER_SIGNED_QUOTE) && quoted >= 128)
            quoted -= 256;
        value = quoted;
    } else {
        size_t places = 0;
        bool decimal = false;
        while ((byte = number_byte(cursor)) != 0) {
            if (byte == '.') {
                decimal = true;
                places = 0;
                ++cursor->offset;
                continue;
            }
            if (byte < '0' || byte > '9')
                break;
            if (policy & QA_QUAKE_NUMBER_DIGIT_FIRST)
                value = value * 10 + (byte - '0');
            else
                value = value * 10 + byte - '0';
            if (decimal)
                ++places;
            ++cursor->offset;
        }
        while (places) {
            value /= 10;
            --places;
        }
    }
    return value * sign;
}

double qa_parse_quake_number(const char *text, qa_quake_number_policy policy) {
    if (!text) return 0;
    qa_number_cursor cursor = {.text = {(const uint8_t *)text, SIZE_MAX}};
    return qa_parse_quake_number_cursor(&cursor, policy, false);
}

size_t qa_format_q3_integer(int32_t value, char out[12]) {
    unsigned char reversed[11];
    size_t count = 0;
    uint32_t bits = value < 0 ? 0u - (uint32_t)value : (uint32_t)value;
    int32_t remaining;
    memcpy(&remaining, &bits, sizeof(remaining));
    do {
        reversed[count++] = (unsigned char)(48 + remaining % 10);
        remaining /= 10;
    } while (remaining);
    if (value < 0)
        reversed[count++] = '-';
    for (size_t index = 0; index < count; ++index)
        out[index] = (char)reversed[count - index - 1];
    out[count] = 0;
    return count;
}

bool qa_format_number(double value, char out[32], qa_error *error) {
    if (!out || !isfinite(value)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "cannot serialize nonfinite number");
        return false;
    }
    if (!ready(error))
        return false;
#if defined(_WIN32)
    int count = _snprintf_l(out, 32, "%.17g", numeric_locale, value);
#else
    locale_t previous = uselocale(numeric_locale);
    if (!previous) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "selecting numeric locale");
        return false;
    }
    int count = snprintf(out, 32, "%.17g", value);
    uselocale(previous);
#endif
    if (count < 0 || count >= 32) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "formatting number");
        return false;
    }
    return true;
}

bool qa_format_fixed(double value, unsigned digits, char *out, size_t capacity, qa_error *error) {
    if (out && capacity)
        out[0] = 0;
    if (!out || !capacity || digits > INT_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid fixed number output");
        return false;
    }
    if (!isfinite(value)) {
        const char *text = isnan(value) ? "nan" : signbit(value) ? "-inf" : "inf";
        size_t size = strlen(text) + 1;
        if (capacity < size) {
            qa_error_set(error, QA_ERROR_ARGUMENT, size, "fixed number output is too small");
            return false;
        }
        memcpy(out, text, size);
        return true;
    }
    if ((size_t)digits + (digits ? 3u : 2u) > capacity) {
        qa_error_set(error, QA_ERROR_ARGUMENT, capacity, "fixed number output is too small");
        return false;
    }
    if (!ready(error))
        return false;
    int previous_rounding = fegetround();
    if (previous_rounding < 0 || fesetround(FE_TONEAREST) != 0) {
        qa_error_set(error, QA_ERROR_IO, 0, "selecting numeric rounding");
        return false;
    }
#if defined(_WIN32)
    int count = _snprintf_l(out, capacity, "%.*f", numeric_locale, (int)digits, value);
#else
    locale_t previous = uselocale(numeric_locale);
    if (!previous) {
        (void)fesetround(previous_rounding);
        qa_error_set(error, QA_ERROR_IO, 0, "selecting numeric locale");
        return false;
    }
    int count = snprintf(out, capacity, "%.*f", (int)digits, value);
    uselocale(previous);
#endif
    int restored = fesetround(previous_rounding);
    if (count < 0 || (size_t)count >= capacity || restored != 0) {
        out[0] = 0;
        qa_error_set(error, count < 0 || restored ? QA_ERROR_IO : QA_ERROR_ARGUMENT,
                     count < 0 ? 0 : (size_t)count + 1, "formatting fixed number");
        return false;
    }
    return true;
}

bool qa_parse_number(qa_bytes input, double *out, qa_error *error) {
    if (!out || !input.size || !input.data || input.size == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid numeric token");
        return false;
    }
    if (!ready(error))
        return false;
    char local[128], *text = local;
    if (input.size >= sizeof(local)) {
        text = malloc(input.size + 1);
        if (!text) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating numeric token");
            return false;
        }
    }
    memcpy(text, input.data, input.size);
    text[input.size] = 0;
    char *end;
#if defined(_WIN32)
    double value = _strtod_l(text, &end, numeric_locale);
#else
    double value = strtod_l(text, &end, numeric_locale);
#endif
    bool valid = end != text;
    while (*end == ' ' || (*end >= '\t' && *end <= '\r'))
        ++end;
    valid = valid && (size_t)(end - text) == input.size;
    if (text != local)
        free(text);
    if (!valid) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "invalid numeric token");
        return false;
    }
    *out = value;
    return true;
}

bool qa_parse_strtod(const char *text, double *out, size_t *consumed, bool *range_error, qa_error *error) {
    int previous_errno = errno;
    if (!text || !out || !consumed || !range_error) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid strtod input");
        errno = previous_errno;
        return false;
    }
    if (!ready(error)) {
        errno = previous_errno;
        return false;
    }
    char *end;
    errno = 0;
#if defined(_WIN32)
    double value = _strtod_l(text, &end, numeric_locale);
#else
    double value = strtod_l(text, &end, numeric_locale);
#endif
    *range_error = errno == ERANGE;
    errno = previous_errno;
    *consumed = (size_t)(end - text);
    *out = value;
    return true;
}

bool qa_parse_atof(const char *text, double *out, qa_error *error) {
    if (!text || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid atof input");
        return false;
    }
    if (!ready(error))
        return false;
#if defined(_WIN32)
    *out = _strtod_l(text, NULL, numeric_locale);
#else
    *out = strtod_l(text, NULL, numeric_locale);
#endif
    return true;
}

bool qa_parse_atof_float(const char *text, float *out, qa_error *error) {
    if (!text || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid atof input");
        return false;
    }
    if (!ready(error))
        return false;
#if defined(_WIN32)
    *out = _strtof_l(text, NULL, numeric_locale);
#else
    *out = strtof_l(text, NULL, numeric_locale);
#endif
    return true;
}

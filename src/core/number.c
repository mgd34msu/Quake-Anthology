#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "qa/text.h"

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

#include "qa/bot_bsp.h"
#include "qa/text.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

struct qa_bot_bsp {
    qa_entities entities;
    size_t record_capacity, property_capacity;
};
static bool fail(qa_error *e, const char *message) {
    qa_error_set(e, QA_ERROR_ARGUMENT, 0, "%s", message);
    return false;
}
int32_t qa_bot_bsp_next(const qa_entities *entities, int32_t after) {
    return entities && after >= 0 && after < INT32_MAX && (size_t)after < entities->count
               ? after + 1
               : 0;
}
bool qa_bot_bsp_value(const qa_entities *entities, int32_t entity, const char *key, qa_bytes *out) {
    return entity > 0 && qa_entity_value(entities, (size_t)(entity - 1), key, out);
}
bool qa_bot_bsp_lookup(const qa_entities *entities, int32_t id, qa_bot_bsp_compare compare,
                       void *context, qa_entity_property *out, bool *found, qa_error *e) {
    if (!compare || !out || !found)
        return fail(e, "missing BSP epair comparator/output");
    *found = false;
    if (!entities || id <= 0 || (size_t)id > entities->count)
        return true;
    qa_entity_record record = entities->records[id - 1];
    for (size_t i = record.property_count; i; --i) {
        const qa_entity_property *property = &entities->properties[record.first_property + i - 1];
        qa_bytes key = property->key;
        const uint8_t *zero = key.size ? memchr(key.data, 0, key.size) : NULL;
        if (zero)
            key.size = (size_t)(zero - key.data);
        bool equal;
        if (!compare(context, key, &equal, e))
            return false;
        if (equal) {
            *out = *property;
            *found = true;
            return true;
        }
    }
    return true;
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
bool qa_bot_bsp_parse_integer(qa_bytes text, int32_t *out, qa_error *e) {
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
bool qa_bot_bsp_parse_float(qa_bytes text, float *out, qa_error *e) {
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
bool qa_bot_bsp_parse_vector(qa_bytes text, qa_vec3 *out, qa_error *e) {
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
bool qa_bot_bsp_integer(const qa_entities *entities, int32_t id, const char *key, int32_t *out,
                        bool *found, qa_error *e) {
    if (!key || !out || !found)
        return fail(e, "missing BSP integer output/key");
    qa_bytes text = {0};
    *found = qa_bot_bsp_value(entities, id, key, &text);
    return qa_bot_bsp_parse_integer(text, out, e);
}
bool qa_bot_bsp_float(const qa_entities *entities, int32_t id, const char *key, float *out,
                      bool *found, qa_error *e) {
    if (!key || !out || !found)
        return fail(e, "missing BSP float output/key");
    qa_bytes text = {0};
    *found = qa_bot_bsp_value(entities, id, key, &text);
    return qa_bot_bsp_parse_float(text, out, e);
}
bool qa_bot_bsp_vector(const qa_entities *entities, int32_t id, const char *key, qa_vec3 *out,
                       bool *found, qa_error *e) {
    if (!key || !out || !found)
        return fail(e, "missing BSP vector output/key");
    qa_bytes text = {0};
    *found = qa_bot_bsp_value(entities, id, key, &text);
    return qa_bot_bsp_parse_vector(text, out, e);
}
static bool reserve(void **data, size_t *capacity, size_t count, size_t width, qa_error *e) {
    if (count <= *capacity)
        return true;
    size_t next = *capacity ? *capacity * 2 : 64;
    if (next < count)
        next = count;
    if (next > SIZE_MAX / width)
        return fail(e, "BSP entity allocation overflow");
    void *p = realloc(*data, next * width);
    if (!p) {
        qa_error_set(e, QA_ERROR_MEMORY, count, "allocating bot BSP tables");
        return false;
    }
    *data = p;
    *capacity = next;
    return true;
}
static void diagnostic(const qa_script_lexer_options *options, qa_script_lexer *lexer,
                       qa_script_severity severity, const char *message) {
    if (options && options->diagnostic) {
        qa_script_diagnostic d = {
            .severity = severity, .location = qa_script_lexer_position(lexer), .message = message};
        options->diagnostic(options->context, &d);
    }
}
static bool token_diagnostic(const qa_script_lexer_options *options, qa_script_lexer *lexer,
                             const char *prefix, qa_bytes token, const char *suffix, qa_error *e) {
    if (!options || !options->diagnostic)
        return true;
    size_t first = strlen(prefix), last = strlen(suffix);
    if (token.size > SIZE_MAX - first - last - 1)
        return fail(e, "BSP diagnostic size overflow");
    char *text = malloc(first + token.size + last + 1);
    if (!text) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Formatting BSP token diagnostic");
        return false;
    }
    memcpy(text, prefix, first);
    if (token.size)
        memcpy(text + first, token.data, token.size);
    memcpy(text + first + token.size, suffix, last + 1);
    diagnostic(options, lexer, QA_SCRIPT_ERROR, text);
    free(text);
    return true;
}
bool qa_bot_bsp_load(qa_bytes bytes, const qa_script_lexer_options *options, qa_bot_bsp **out,
                     qa_error *e) {
    qa_error local = {0};
    if (!e)
        e = &local;
    if (!out || (bytes.size && !bytes.data))
        return fail(e, "invalid BSP entity source");
    const uint8_t *zero = bytes.size ? memchr(bytes.data, 0, bytes.size) : NULL;
    if (zero)
        bytes.size = (size_t)(zero - bytes.data);
    qa_script_lexer_options opts = options ? *options : (qa_script_lexer_options){0};
    opts.flags = QA_SCRIPT_NO_STRING_CONCAT | QA_SCRIPT_NO_STRING_ESCAPES;
    qa_script_lexer *lexer;
    if (!qa_script_lexer_open("entdata", bytes, &opts, &lexer, e))
        return false;
    qa_bot_bsp *bsp = calloc(1, sizeof(*bsp));
    if (!bsp) {
        qa_script_lexer_close(lexer);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating bot BSP owner");
        return false;
    }
    bool ok = true, malformed = false;
    for (;;) {
        qa_script_token token;
        bool found;
        if (!qa_script_lexer_next(lexer, &token, &found, e)) {
            if (e->code != QA_ERROR_FORMAT)
                ok = false;
            else
                *e = (qa_error){0};
            break;
        }
        if (!found)
            break;
        if (!qa_script_token_is(&token, "{")) {
            malformed = true;
            ok = token_diagnostic(&opts, lexer, "invalid ", token.text, "\n", e);
            break;
        }
        if (bsp->entities.count == 2047) {
            diagnostic(&opts, lexer, QA_SCRIPT_INFO, "too many entities in BSP file\n");
            break;
        }
        if (!reserve((void **)&bsp->entities.records, &bsp->record_capacity,
                     bsp->entities.count + 1, sizeof(*bsp->entities.records), e)) {
            ok = false;
            break;
        }
        qa_entity_record *record = &bsp->entities.records[bsp->entities.count++];
        *record = (qa_entity_record){.first_property = bsp->entities.property_count};
        for (;;) {
            if (!qa_script_lexer_next(lexer, &token, &found, e)) {
                ok = e->code == QA_ERROR_FORMAT;
                malformed = ok;
                if (ok) {
                    *e = (qa_error){0};
                    diagnostic(&opts, lexer, QA_SCRIPT_ERROR, "missing }\n");
                }
                break;
            }
            if (!found) {
                malformed = true;
                diagnostic(&opts, lexer, QA_SCRIPT_ERROR, "missing }\n");
                break;
            }
            if (qa_script_token_is(&token, "}"))
                break;
            if (token.kind != QA_SCRIPT_STRING) {
                malformed = true;
                ok = token_diagnostic(&opts, lexer, "invalid ", token.text, "\n", e);
                break;
            }
            /* With escapes and concatenation disabled, values borrow their
             * exact source bytes. The lexer's temporary decoded buffer is not retained. */
            qa_bytes key = token.text;
            if (key.size < 2) {
                malformed = true;
                break;
            }
            key.data++;
            key.size -= 2;
            if (!qa_script_lexer_next(lexer, &token, &found, e)) {
                ok = e->code == QA_ERROR_FORMAT;
                malformed = ok;
                if (ok) {
                    *e = (qa_error){0};
                    diagnostic(&opts, lexer, QA_SCRIPT_ERROR, "couldn't read expected token");
                }
                break;
            }
            if (!found || token.kind != QA_SCRIPT_STRING) {
                malformed = true;
                if (!found)
                    diagnostic(&opts, lexer, QA_SCRIPT_ERROR, "couldn't read expected token");
                else
                    ok = token_diagnostic(&opts, lexer, "expected a string, found ", token.text, "",
                                          e);
                break;
            }
            qa_bytes value = token.text;
            if (value.size < 2) {
                malformed = true;
                break;
            }
            value.data++;
            value.size -= 2;
            if (!reserve((void **)&bsp->entities.properties, &bsp->property_capacity,
                         bsp->entities.property_count + 1, sizeof(*bsp->entities.properties), e)) {
                ok = false;
                break;
            }
            bsp->entities.properties[bsp->entities.property_count++] =
                (qa_entity_property){key, value};
            ++record->property_count;
        }
        if (!ok || malformed)
            break;
    }
    if (malformed) {
        qa_entities_free(&bsp->entities);
        bsp->record_capacity = bsp->property_capacity = 0;
    }
    qa_script_lexer_close(lexer);
    if (!ok) {
        qa_bot_bsp_close(bsp);
        return false;
    }
    *out = bsp;
    return true;
}
const qa_entities *qa_bot_bsp_entities(const qa_bot_bsp *bsp) {
    return bsp ? &bsp->entities : NULL;
}
void qa_bot_bsp_close(qa_bot_bsp *bsp) {
    if (!bsp)
        return;
    qa_entities_free(&bsp->entities);
    free(bsp);
}

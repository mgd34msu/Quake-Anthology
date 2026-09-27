#include "internal.h"

typedef struct bot_variable {
    struct bot_variable *next, *bucket_next;
    qa_bot_variable view;
    char *text;
    size_t capacity;
    uint32_t hash;
    char name[];
} bot_variable;

static unsigned folded(unsigned byte) {
    return byte >= 'a' && byte <= 'z' ? byte - ('a' - 'A') : byte;
}
static uint32_t name_hash(const char *name) {
    uint32_t hash = UINT32_C(2166136261);
    for (size_t i = 0; i < 99999 && name[i]; ++i)
        hash = (hash ^ folded((unsigned char)name[i])) * UINT32_C(16777619);
    return hash;
}
static bool name_equal(const char *a, const char *b) {
    for (size_t i = 0; i < 99999; ++i) {
        if (folded((unsigned char)a[i]) != folded((unsigned char)b[i]))
            return false;
        if (!a[i])
            return true;
    }
    return true;
}
static bot_variable *find_variable(const qa_bot_library *library, const char *name) {
    if (!library || !name)
        return NULL;
    uint32_t hash = name_hash(name);
    for (bot_variable *v = library->variable_buckets[hash & 127]; v; v = v->bucket_next)
        if (v->hash == hash && name_equal(v->name, name))
            return v;
    return NULL;
}
bool qa_bot_variable_number(const char *text, float *out, qa_error *e) {
    if (!text || !out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "missing bot variable numeric text/output");
        return false;
    }
    int32_t denominator = 0;
    float value = 0;
    for (size_t i = 0; text[i]; ++i) {
        unsigned byte = (unsigned char)text[i];
        if (byte < '0' || byte > '9') {
            if (denominator || byte != '.') {
                *out = 0;
                return true;
            }
            denominator = 10;
            byte = (unsigned char)text[++i];
            if (!byte) {
                *out = 0;
                return true;
            }
        }
        if (denominator) {
            int32_t signed_byte = byte < 128 ? (int32_t)byte : (int32_t)byte - 256;
            value += (float)(signed_byte - '0') / (float)denominator;
            if (denominator > INT32_MAX / 10) {
                qa_error_set(e, QA_ERROR_FORMAT, i, "bot variable fractional denominator overflow");
                return false;
            }
            denominator *= 10;
        } else
            value = (float)((double)value * 10.0 + (byte - '0'));
    }
    *out = value;
    return true;
}
const qa_bot_variable *qa_bot_library_variable(const qa_bot_library *library, const char *name) {
    bot_variable *v = find_variable(library, name);
    return v ? &v->view : NULL;
}
static bool assign_variable(qa_bot_library *library, const char *name, const char *text,
                            const qa_bot_variable **out, qa_error *e) {
    if (!library || !name || !text) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "invalid bot library variable");
        return false;
    }
    float number;
    if (!qa_bot_variable_number(text, &number, e))
        return false;
    size_t length = strlen(text), name_length = strlen(name);
    if (length == SIZE_MAX || name_length > SIZE_MAX - sizeof(bot_variable) - 1)
        goto memory;
    bot_variable *v = find_variable(library, name);
    bool created = !v;
    if (created) {
        v = calloc(1, sizeof(*v) + name_length + 1);
        if (!v)
            goto memory;
        memcpy(v->name, name, name_length + 1);
        v->view.name = v->name;
        v->hash = name_hash(name);
    }
    if (length + 1 > v->capacity) {
        char *replacement = malloc(length + 1);
        if (!replacement) {
            if (created)
                free(v);
            goto memory;
        }
        memcpy(replacement, text, length + 1);
        free(v->text);
        v->text = replacement;
        v->capacity = length + 1;
    } else
        memmove(v->text, text, length + 1);
    v->view.string = v->text;
    v->view.value = number;
    v->view.modified = true;
    if (created) {
        v->next = library->variables;
        library->variables = v;
        v->bucket_next = library->variable_buckets[v->hash & 127];
        library->variable_buckets[v->hash & 127] = v;
    }
    if (out)
        *out = &v->view;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, 0, "allocating bot library variable");
    return false;
}
bool qa_bot_library_variable_default(qa_bot_library *library, const char *name, const char *text,
                                     const qa_bot_variable **out, qa_error *e) {
    if (!out) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "missing bot variable output");
        return false;
    }
    const qa_bot_variable *existing = qa_bot_library_variable(library, name);
    if (existing) {
        *out = existing;
        return true;
    }
    return assign_variable(library, name, text, out, e);
}
bool qa_bot_library_variable_set(qa_bot_library *library, const char *name, const char *text,
                                 qa_error *e) {
    return assign_variable(library, name, text, NULL, e);
}
void qa_bot_library_variable_unmodify(qa_bot_library *library, const char *name) {
    bot_variable *v = find_variable(library, name);
    if (v)
        v->view.modified = false;
}
const qa_bot_variable *qa_bot_library_variable_at(const qa_bot_library *library, size_t index) {
    bot_variable *v = library ? library->variables : NULL;
    while (v && index--)
        v = v->next;
    return v ? &v->view : NULL;
}
void qa_bot_library_variables_clear(qa_bot_library *library) {
    if (!library)
        return;
    while (library->variables) {
        bot_variable *v = library->variables;
        library->variables = v->next;
        free(v->text);
        free(v);
    }
    memset(library->variable_buckets, 0, sizeof(library->variable_buckets));
}
bool qa_bot_library_variable_restore(qa_bot_library *library, const qa_bot_variable *saved,
                                     qa_error *e) {
    if (!saved || !library || !saved->name || !saved->string ||
        find_variable(library, saved->name)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "invalid or duplicate saved bot variable");
        return false;
    }
    if (!assign_variable(library, saved->name, saved->string, NULL, e))
        return false;
    bot_variable *v = library->variables;
    v->view.value = saved->value;
    v->view.flags = saved->flags;
    v->view.modified = saved->modified;
    return true;
}

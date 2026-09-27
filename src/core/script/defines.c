#include "internal.h"

static uint32_t hash(qa_bytes name) {
    uint32_t hash = 0;
    for (size_t i = 0; i < name.size; ++i)
        hash += (uint32_t)name.data[i] * (uint32_t)(119 + i);
    return (hash ^ (hash >> 10) ^ (hash >> 20)) & 1023;
}
script_macro *script_macro_find(const script_macro_table *table, qa_bytes name) {
    for (script_macro *m = table->buckets[hash(name)]; m != NULL; m = m->next)
        if (script_bytes_equal(m->name, name))
            return m;
    return NULL;
}
bool script_macro_remove(script_macro_table *table, qa_bytes name, bool *fixed) {
    *fixed = false;
    script_macro **at = &table->buckets[hash(name)];
    while (*at != NULL) {
        script_macro *m = *at;
        if (script_bytes_equal(m->name, name)) {
            if (m->fixed) {
                *fixed = true;
                return false;
            }
            *at = m->next;
            --table->count;
            return true;
        }
        at = &m->next;
    }
    return false;
}
static bool copy_token(qa_arena *arena, const qa_script_token *source, const char *path,
                       qa_script_token *out, qa_error *e) {
    *out = *source;
    char *text = script_string(arena, source->text.data, source->text.size, e);
    if (text == NULL)
        return false;
    out->text.data = (const uint8_t *)text;
    out->location.path = path;
    out->leading_whitespace = (qa_bytes){0};
    out->lines_crossed = 0;
    return true;
}
bool script_macro_parse(script_macro_table *table, const qa_script_token *tokens, size_t count,
                        size_t limit, qa_error *e) {
    if (count == 0 || tokens[0].kind != QA_SCRIPT_NAME) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Script define requires a name");
        return false;
    }
    script_macro *existing = script_macro_find(table, tokens[0].text);
    if (existing != NULL && existing->fixed) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Cannot redefine a fixed script macro");
        return false;
    }
    if (existing == NULL && table->count >= limit) {
        qa_error_set(e, QA_ERROR_FORMAT, 0, "Script define count exceeds configured limit");
        return false;
    }
    script_macro *m = qa_arena_alloc(&table->arena, sizeof(*m), _Alignof(script_macro), e);
    if (m == NULL)
        return false;
    *m = (script_macro){0};
    char *name = script_string(&table->arena, tokens[0].text.data, tokens[0].text.size, e);
    if (name == NULL)
        return false;
    m->name = (qa_bytes){(const uint8_t *)name, tokens[0].text.size};
    size_t at = 1;
    if (at < count && qa_script_token_is(tokens + at, "(") &&
        tokens[at].leading_whitespace.size == 0) {
        m->function = true;
        ++at;
        size_t begin = at, parameters = 0;
        while (at < count && !qa_script_token_is(tokens + at, ")")) {
            if (tokens[at].kind != QA_SCRIPT_NAME || parameters == 128) {
                qa_error_set(e, QA_ERROR_FORMAT, at, "Invalid script macro parameter");
                return false;
            }
            for (size_t i = begin; i < at; i += 2)
                if (script_bytes_equal(tokens[i].text, tokens[at].text)) {
                    qa_error_set(e, QA_ERROR_FORMAT, at, "Duplicate script macro parameter");
                    return false;
                }
            ++parameters;
            ++at;
            if (at < count && qa_script_token_is(tokens + at, ")"))
                break;
            if (at == count || !qa_script_token_is(tokens + at, ",")) {
                qa_error_set(e, QA_ERROR_FORMAT, at, "Unterminated script macro parameters");
                return false;
            }
            ++at;
            if (at < count && qa_script_token_is(tokens + at, ")")) {
                qa_error_set(e, QA_ERROR_FORMAT, at, "Missing script macro parameter after comma");
                return false;
            }
        }
        if (at == count) {
            qa_error_set(e, QA_ERROR_FORMAT, at, "Unterminated script macro parameters");
            return false;
        }
        if (parameters != 0) {
            m->parameters = qa_arena_alloc(&table->arena, parameters * sizeof(*m->parameters),
                                           _Alignof(qa_bytes), e);
            if (m->parameters == NULL)
                return false;
            for (size_t i = 0; i < parameters; ++i) {
                qa_bytes text = tokens[begin + i * 2].text;
                char *copy = script_string(&table->arena, text.data, text.size, e);
                if (copy == NULL)
                    return false;
                m->parameters[i] = (qa_bytes){(const uint8_t *)copy, text.size};
            }
        }
        m->parameter_count = parameters;
        ++at;
    }
    size_t body = count - at;
    if (body > SIZE_MAX / sizeof(*m->tokens)) {
        qa_error_set(e, QA_ERROR_MEMORY, at, "Script macro body size overflow");
        return false;
    }
    if (body != 0) {
        m->tokens =
            qa_arena_alloc(&table->arena, body * sizeof(*m->tokens), _Alignof(qa_script_token), e);
        if (m->tokens == NULL)
            return false;
        const char *source_path = tokens[at].location.path == NULL ? "" : tokens[at].location.path;
        char *stored_path = script_string(&table->arena, source_path, strlen(source_path), e);
        if (stored_path == NULL)
            return false;
        for (; at < count; ++at) {
            if (tokens[at].kind == QA_SCRIPT_NAME && script_bytes_equal(tokens[at].text, m->name))
                continue;
            const char *path = tokens[at].location.path == NULL ? "" : tokens[at].location.path;
            if (strcmp(source_path, path) != 0) {
                source_path = path;
                stored_path = script_string(&table->arena, path, strlen(path), e);
                if (stored_path == NULL)
                    return false;
            }
            if (!copy_token(&table->arena, tokens + at, stored_path, m->tokens + m->token_count, e))
                return false;
            ++m->token_count;
        }
        if (m->token_count != 0 && (qa_script_token_is(m->tokens, "##") ||
                                    qa_script_token_is(m->tokens + m->token_count - 1, "##"))) {
            qa_error_set(e, QA_ERROR_FORMAT, 0, "Misplaced script macro merge operator");
            return false;
        }
    }
    bool fixed;
    (void)script_macro_remove(table, m->name, &fixed);
    uint32_t bucket = hash(m->name);
    m->next = table->buckets[bucket];
    table->buckets[bucket] = m;
    ++table->count;
    return true;
}
bool script_macro_text(script_macro_table *table, const char *definition, size_t limit,
                       qa_error *e) {
    if (definition == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing macro definition");
        return false;
    }
    qa_script_lexer *lexer;
    qa_bytes input = script_bytes(definition);
    if (!qa_script_lexer_open("<define>", input, NULL, &lexer, e))
        return false;
    qa_script_token *tokens = NULL;
    size_t count = 0, capacity = 0;
    bool ok = true, found;
    for (;;) {
        qa_script_token token;
        if (!qa_script_lexer_next(lexer, &token, &found, e)) {
            ok = false;
            break;
        }
        if (!found)
            break;
        if (!script_grow((void **)&tokens, &capacity, count + 1, sizeof(*tokens), e)) {
            ok = false;
            break;
        }
        tokens[count++] = token;
    }
    size_t start = count != 0 && qa_script_token_is(tokens, "#") ? 1 : 0;
    if (start < count && qa_script_token_is(tokens + start, "define"))
        ++start;
    if (ok)
        ok = script_macro_parse(table, tokens == NULL ? NULL : tokens + start, count - start, limit,
                                e);
    free(tokens);
    qa_script_lexer_close(lexer);
    return ok;
}
bool script_table_import(script_macro_table *to, const script_macro_table *from, qa_error *e) {
    for (size_t bucket = 0; bucket < 1024; ++bucket)
        for (const script_macro *m = from->buckets[bucket]; m != NULL; m = m->next) {
            script_macro *copy =
                qa_arena_alloc(&to->arena, sizeof(*copy), _Alignof(script_macro), e);
            if (copy == NULL)
                return false;
            *copy = *m;
            copy->next = to->buckets[bucket];
            to->buckets[bucket] = copy;
            ++to->count;
        }
    return true;
}
void script_table_clear(script_macro_table *table) {
    memset(table->buckets, 0, sizeof(table->buckets));
    table->count = 0;
}
bool script_macro_copy(script_macro_table *table, const qa_script_macro_state *source,
                       script_macro **out, qa_error *e) {
    script_macro *m = qa_arena_alloc(&table->arena, sizeof(*m), _Alignof(script_macro), e);
    if (m == NULL)
        return false;
    *m = (script_macro){.parameter_count = source->parameter_count,
                        .token_count = source->token_count,
                        .builtin = source->builtin,
                        .function = source->function,
                        .fixed = source->fixed};
    char *name = script_string(&table->arena, source->name.data, source->name.size, e);
    if (name == NULL)
        return false;
    m->name = (qa_bytes){(const uint8_t *)name, source->name.size};
    if (m->parameter_count != 0) {
        m->parameters = qa_arena_alloc(&table->arena, m->parameter_count * sizeof(*m->parameters),
                                       _Alignof(qa_bytes), e);
        if (m->parameters == NULL)
            return false;
        for (size_t i = 0; i < m->parameter_count; ++i) {
            qa_bytes p = source->parameters[i];
            char *text = script_string(&table->arena, p.data, p.size, e);
            if (text == NULL)
                return false;
            m->parameters[i] = (qa_bytes){(const uint8_t *)text, p.size};
        }
    }
    if (m->token_count != 0) {
        m->tokens = qa_arena_alloc(&table->arena, m->token_count * sizeof(*m->tokens),
                                   _Alignof(qa_script_token), e);
        if (m->tokens == NULL)
            return false;
        const char *previous = NULL, *path = NULL;
        for (size_t i = 0; i < m->token_count; ++i) {
            const char *p =
                source->tokens[i].location.path == NULL ? "" : source->tokens[i].location.path;
            if (previous == NULL || strcmp(previous, p) != 0) {
                previous = p;
                path = script_string(&table->arena, p, strlen(p), e);
                if (path == NULL)
                    return false;
            }
            if (!copy_token(&table->arena, source->tokens + i, path, m->tokens + i, e))
                return false;
        }
    }
    if (source->active) {
        uint32_t bucket = hash(m->name);
        m->next = table->buckets[bucket];
        table->buckets[bucket] = m;
        ++table->count;
    }
    *out = m;
    return true;
}
bool qa_script_defines_create(qa_script_defines **out, qa_error *e) {
    if (out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing global define output");
        return false;
    }
    qa_script_defines *d = calloc(1, sizeof(*d));
    if (d == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating script globals");
        return false;
    }
    atomic_init(&d->references, 1);
    *out = d;
    return true;
}
void qa_script_defines_retain(qa_script_defines *d) {
    if (d != NULL)
        atomic_fetch_add_explicit(&d->references, 1, memory_order_relaxed);
}
void qa_script_defines_release(qa_script_defines *d) {
    if (d != NULL && atomic_fetch_sub_explicit(&d->references, 1, memory_order_acq_rel) == 1) {
        qa_arena_destroy(&d->table.arena);
        free(d);
    }
}
bool qa_script_defines_add(qa_script_defines *d, const char *text, qa_error *e) {
    if (d == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing global define owner");
        return false;
    }
    return script_macro_text(&d->table, text, SIZE_MAX, e);
}
bool qa_script_defines_remove(qa_script_defines *d, const char *name, qa_error *e) {
    if (d == NULL || name == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing global define name/owner");
        return false;
    }
    bool fixed;
    (void)script_macro_remove(&d->table, script_bytes(name), &fixed);
    return true;
}
void qa_script_defines_clear(qa_script_defines *d) {
    if (d != NULL)
        script_table_clear(&d->table);
}

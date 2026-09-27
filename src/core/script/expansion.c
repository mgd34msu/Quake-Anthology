#include "internal.h"

typedef struct macro_argument {
    size_t start, count;
} macro_argument;
static bool append(qa_script *s, script_queued_token **tokens, size_t *count, size_t *capacity,
                   script_queued_token token, qa_error *e) {
    if (*count >= s->options.maximum_queued_tokens)
        return script_fail(s, token.token.location, "Macro token limit exceeded", e);
    if (!script_grow((void **)tokens, capacity, *count + 1, sizeof(**tokens), e))
        return false;
    (*tokens)[(*count)++] = token;
    return true;
}
static int parameter(const script_macro *m, const qa_script_token *token) {
    if (token->kind != QA_SCRIPT_NAME)
        return -1;
    for (size_t i = 0; i < m->parameter_count; ++i)
        if (script_bytes_equal(m->parameters[i], token->text))
            return (int)i;
    return -1;
}
static bool arguments(qa_script *s, const script_macro *m, qa_script_location location,
                      macro_argument args[128], script_queued_token **out, size_t *count,
                      qa_error *e) {
    script_queued_token token;
    bool found;
    if (!script_raw(s, &token, &found, e))
        return false;
    if (!found || !qa_script_token_is(&token.token, "(")) {
        if (found && !script_push(s, token, e))
            return false;
        return script_fail(s, location, "Function macro requires arguments", e);
    }
    script_queued_token *tokens = NULL;
    size_t length = 0, capacity = 0, argument = 0;
    int depth = 0;
    bool last_comma = true;
    memset(args, 0, 128 * sizeof(*args));
    for (;;) {
        if (!script_raw(s, &token, &found, e))
            goto fail;
        if (!found) {
            script_fail(s, location, "Unterminated macro arguments", e);
            goto fail;
        }
        if (qa_script_token_is(&token.token, ",") && depth <= 0) {
            if (last_comma)
                script_warn(s, token.token.location, "Too many commas in macro arguments");
            if (++argument >= m->parameter_count) {
                script_fail(s, location, "Too many macro arguments", e);
                goto fail;
            }
            args[argument].start = length;
            last_comma = true;
            continue;
        }
        last_comma = false;
        if (qa_script_token_is(&token.token, "(")) {
            if (depth == INT_MAX) {
                script_fail(s, location, "Macro argument nesting overflow", e);
                goto fail;
            }
            ++depth;
            continue;
        }
        if (qa_script_token_is(&token.token, ")") && --depth <= 0) {
            if (args[m->parameter_count - 1].count == 0)
                script_warn(s, token.token.location, "Too few macro arguments");
            *out = tokens;
            *count = length;
            return true;
        }
        if (!append(s, &tokens, &length, &capacity, token, e))
            goto fail;
        ++args[argument].count;
    }
fail:
    free(tokens);
    return false;
}
static bool joined(qa_script *s, qa_bytes left, qa_bytes right, size_t left_trim, size_t right_trim,
                   qa_bytes *out, qa_error *e) {
    if (left.size < left_trim || right.size < right_trim ||
        left.size - left_trim > SIZE_MAX - (right.size - right_trim))
        return script_fail(s, qa_script_position(s), "Invalid merged macro token", e);
    size_t size = left.size - left_trim + right.size - right_trim;
    if (size + 1 >= s->options.token_limit)
        return script_fail(s, qa_script_position(s), "Merged macro token exceeds token limit", e);
    char *text = qa_arena_alloc(&s->arena, size + 1, 1, e);
    if (text == NULL)
        return false;
    if (left.size != left_trim)
        memcpy(text, left.data, left.size - left_trim);
    if (right.size != right_trim)
        memcpy(text + left.size - left_trim, right.data + right_trim, right.size - right_trim);
    text[size] = 0;
    *out = (qa_bytes){(const uint8_t *)text, size};
    return true;
}
static bool stringize(qa_script *s, const script_queued_token *tokens, macro_argument argument,
                      qa_script_location location, qa_script_token *out, qa_error *e) {
    size_t size = 2;
    for (size_t i = 0; i < argument.count; ++i) {
        size_t add = tokens[argument.start + i].token.text.size;
        if (add > SIZE_MAX - size)
            return script_fail(s, location, "Macro string size overflow", e);
        size += add;
    }
    if (size >= s->options.token_limit)
        return script_fail(s, location, "Stringized macro exceeds token limit", e);
    char *text = qa_arena_alloc(&s->arena, size + 1, 1, e);
    if (text == NULL)
        return false;
    size_t at = 1;
    text[0] = '"';
    for (size_t i = 0; i < argument.count; ++i) {
        qa_bytes bytes = tokens[argument.start + i].token.text;
        if (bytes.size != 0)
            memcpy(text + at, bytes.data, bytes.size);
        at += bytes.size;
    }
    text[at++] = '"';
    text[at] = 0;
    *out = (qa_script_token){.kind = QA_SCRIPT_STRING,
                             .subtype = (uint32_t)size,
                             .text = {(const uint8_t *)text, size},
                             .location = location};
    return true;
}
static bool builtin(qa_script *s, const script_macro *m, script_queued_token invocation,
                    qa_error *e) {
    qa_script_token token = invocation.token;
    char number[32];
    const char *value = NULL;
    switch (m->builtin) {
    case 1:
        if (!qa_format_number(token.location.line, number, e))
            return false;
        value = number;
        token.kind = QA_SCRIPT_NUMBER;
        token.subtype = QA_SCRIPT_DECIMAL | QA_SCRIPT_INTEGER;
        memcpy(&token.integer, &token.location.line, sizeof(token.integer));
        token.number = token.location.line;
        break;
    case 2:
        value = qa_script_position(s).path;
        if (value == NULL)
            value = "";
        token.kind = QA_SCRIPT_NAME;
        break;
    case 3:
    case 4: {
        const char *part = m->builtin == 3 ? s->services.date : s->services.time;
        if (part == NULL)
            return script_fail(s, token.location,
                               "Date/time builtin requires a captured source date/time", e);
        size_t length = strlen(part);
        if (length > SIZE_MAX - 3 || length + 3 >= s->options.token_limit)
            return script_fail(s, token.location, "Date/time builtin exceeds token limit", e);
        char *quoted = qa_arena_alloc(&s->arena, length + 3, 1, e);
        if (quoted == NULL)
            return false;
        quoted[0] = '"';
        memcpy(quoted + 1, part, length);
        quoted[length + 1] = '"';
        quoted[length + 2] = 0;
        token.kind = QA_SCRIPT_NAME;
        value = quoted;
        break;
    }
    default:
        return script_fail(s, token.location, "Unknown builtin macro", e);
    }
    size_t size = strlen(value);
    if (size >= s->options.token_limit)
        return script_fail(s, token.location, "Builtin exceeds token limit", e);
    char *text = script_string(&s->arena, value, size, e);
    if (text == NULL)
        return false;
    token.text = (qa_bytes){(const uint8_t *)text, size};
    if (token.kind == QA_SCRIPT_NAME)
        token.subtype = (uint32_t)size;
    return script_push(s, (script_queued_token){token, invocation.expansion}, e);
}
bool script_expand(qa_script *s, script_queued_token invocation, script_macro *m, qa_error *e) {
    s->empty_expansion = false;
    if (s->expansions >= s->options.maximum_expansions)
        return script_fail(s, invocation.token.location,
                           "Macro expansion count exceeds configured limit", e);
    ++s->expansions;
    for (const script_expansion *p = invocation.expansion; p != NULL; p = p->parent)
        if (p->macro == m)
            return script_fail(s, invocation.token.location, "Recursive macro expansion", e);
    script_expansion *expansion =
        qa_arena_alloc(&s->arena, sizeof(*expansion), _Alignof(script_expansion), e);
    if (expansion == NULL)
        return false;
    *expansion = (script_expansion){m, invocation.expansion};
    invocation.expansion = expansion;
    if (m->builtin != 0)
        return builtin(s, m, invocation, e);
    macro_argument args[128];
    script_queued_token *arg_tokens = NULL, *tokens = NULL;
    size_t arg_count = 0, count = 0, capacity = 0;
    if (m->parameter_count != 0 &&
        !arguments(s, m, invocation.token.location, args, &arg_tokens, &arg_count, e))
        return false;
    for (size_t i = 0; i < m->token_count; ++i) {
        qa_script_token token = m->tokens[i];
        int index = parameter(m, &token);
        if (index >= 0) {
            for (size_t j = 0; j < args[index].count; ++j) {
                script_queued_token item = arg_tokens[args[index].start + j];
                item.expansion = expansion;
                if (!append(s, &tokens, &count, &capacity, item, e))
                    goto fail;
            }
            continue;
        }
        if (qa_script_token_is(&token, "#")) {
            if (i + 1 == m->token_count || (index = parameter(m, m->tokens + i + 1)) < 0) {
                script_warn(s, invocation.token.location,
                            "Stringizing operator without macro parameter");
                continue;
            }
            ++i;
            if (!stringize(s, arg_tokens, args[index], invocation.token.location, &token, e))
                goto fail;
        }
        if (!append(s, &tokens, &count, &capacity, (script_queued_token){token, expansion}, e))
            goto fail;
    }
    for (size_t i = 0; i + 2 < count;) {
        if (!qa_script_token_is(&tokens[i + 1].token, "##")) {
            ++i;
            continue;
        }
        qa_script_token *a = &tokens[i].token, *b = &tokens[i + 2].token;
        bool names =
            a->kind == QA_SCRIPT_NAME && (b->kind == QA_SCRIPT_NAME || b->kind == QA_SCRIPT_NUMBER);
        bool strings = a->kind == QA_SCRIPT_STRING && b->kind == QA_SCRIPT_STRING;
        if (!names && !strings) {
            script_fail(s, invocation.token.location, "Invalid macro token merge", e);
            goto fail;
        }
        if (!joined(s, a->text, b->text, strings ? 1 : 0, strings ? 1 : 0, &a->text, e))
            goto fail;
        a->subtype = (uint32_t)a->text.size;
        memmove(tokens + i + 1, tokens + i + 3, (count - i - 3) * sizeof(*tokens));
        count -= 2;
    }
    s->empty_expansion = count == 0;
    for (size_t i = count; i != 0; --i)
        if (!script_push(s, tokens[i - 1], e))
            goto fail;
    free(tokens);
    free(arg_tokens);
    return true;
fail:
    free(tokens);
    free(arg_tokens);
    return false;
}

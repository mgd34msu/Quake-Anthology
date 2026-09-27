#include "internal.h"

static const qa_script_punctuation defaults[] = {
    {">>=", 1}, {"<<=", 2}, {"...", 3}, {"##", 4},  {"&&", 5},  {"||", 6},  {">=", 7},  {"<=", 8},
    {"==", 9},  {"!=", 10}, {"*=", 11}, {"/=", 12}, {"%=", 13}, {"+=", 14}, {"-=", 15}, {"++", 16},
    {"--", 17}, {"&=", 18}, {"|=", 19}, {"^=", 20}, {">>", 21}, {"<<", 22}, {"->", 23}, {"::", 24},
    {".*", 25}, {"*", 26},  {"/", 27},  {"%", 28},  {"+", 29},  {"-", 30},  {"=", 31},  {"&", 32},
    {"|", 33},  {"^", 34},  {"~", 35},  {"!", 36},  {">", 37},  {"<", 38},  {".", 39},  {",", 40},
    {";", 41},  {":", 42},  {"?", 43},  {"(", 44},  {")", 45},  {"{", 46},  {"}", 47},  {"[", 48},
    {"]", 49},  {"\\", 50}, {"#", 51},  {"$", 52}};
static int32_t default_heads[256], default_next[sizeof(defaults) / sizeof(*defaults)];
static atomic_uint default_index_state;

static void punctuation_index(const qa_script_punctuation *table, size_t count, int32_t *heads,
                              int32_t *next) {
    for (size_t i = 0; i < 256; ++i)
        heads[i] = -1;
    for (size_t i = 0; i < count; ++i) {
        size_t length = strlen(table[i].text);
        int32_t *at = &heads[(uint8_t)table[i].text[0]];
        while (*at >= 0 && strlen(table[*at].text) >= length)
            at = next + *at;
        next[i] = *at;
        *at = (int32_t)i;
    }
}

static void default_punctuation_index(void) {
    if (atomic_load_explicit(&default_index_state, memory_order_acquire) == 2)
        return;
    unsigned expected = 0;
    if (atomic_compare_exchange_strong_explicit(&default_index_state, &expected, 1,
                                                memory_order_acq_rel, memory_order_acquire)) {
        punctuation_index(defaults, sizeof(defaults) / sizeof(*defaults), default_heads,
                          default_next);
        atomic_store_explicit(&default_index_state, 2, memory_order_release);
    } else {
        while (atomic_load_explicit(&default_index_state, memory_order_acquire) != 2) {
        }
    }
}
bool script_grow(void **data, size_t *capacity, size_t count, size_t stride, qa_error *e) {
    if (count <= *capacity)
        return true;
    size_t next = *capacity < 16 ? 16 : *capacity;
    while (next < count && next <= SIZE_MAX / 2)
        next *= 2;
    if (next < count || next > SIZE_MAX / stride)
        goto memory;
    void *p = realloc(*data, next * stride);
    if (p == NULL)
        goto memory;
    *data = p;
    *capacity = next;
    return true;
memory:
    qa_error_set(e, QA_ERROR_MEMORY, count, "Growing retained script storage");
    return false;
}
char *script_string(qa_arena *arena, const void *bytes, size_t size, qa_error *e) {
    if (size == SIZE_MAX) {
        qa_error_set(e, QA_ERROR_MEMORY, size, "Script string size overflow");
        return NULL;
    }
    char *out = qa_arena_alloc(arena, size + 1, 1, e);
    if (out == NULL)
        return NULL;
    if (size != 0)
        memcpy(out, bytes, size);
    out[size] = 0;
    return out;
}
qa_script_location qa_script_lexer_position(const qa_script_lexer *l) {
    return l == NULL
               ? (qa_script_location){0}
               : (qa_script_location){l->path, l->state.line, l->state.column, l->state.offset};
}
bool script_error(qa_script_lexer *l, const char *message, qa_error *e) {
    qa_script_location location = qa_script_lexer_position(l);
    qa_error_set(e, QA_ERROR_FORMAT, location.offset, "%s:%u:%u: %s", l->path, location.line,
                 location.column, message);
    l->source_failure = (l->options.flags & QA_SCRIPT_NO_ERRORS) == 0;
    script_report_error(l, message);
    return false;
}
void script_report_error(qa_script_lexer *l, const char *message) {
    if ((l->options.flags & QA_SCRIPT_NO_ERRORS) == 0 && l->options.diagnostic != NULL) {
        qa_script_location location = qa_script_lexer_position(l);
        qa_script_diagnostic d = {QA_SCRIPT_ERROR, location, message};
        l->options.diagnostic(l->options.context, &d);
    }
}
bool script_unsupported(qa_script_lexer *l, const char *message, qa_error *e) {
    l->source_failure = false;
    qa_error_set(e, QA_ERROR_UNSUPPORTED, l->state.offset, "%s", message);
    return false;
}
void script_warning(qa_script_lexer *l, const char *message) {
    if ((l->options.flags & QA_SCRIPT_NO_WARNINGS) == 0 && l->options.diagnostic != NULL) {
        qa_script_diagnostic d = {QA_SCRIPT_WARNING, qa_script_lexer_position(l), message};
        l->options.diagnostic(l->options.context, &d);
    }
}
uint8_t script_peek(const qa_script_lexer *l, size_t ahead) {
    return l->state.offset >= l->input.size || ahead >= l->input.size - l->state.offset
               ? 0
               : l->input.data[l->state.offset + ahead];
}
void script_advance(qa_script_lexer *l, size_t count) {
    while (count-- != 0 && l->state.offset < l->input.size) {
        uint8_t c = l->input.data[l->state.offset++];
        if (c == '\n') {
            if (l->state.line != UINT32_MAX)
                ++l->state.line;
            l->state.column = 1;
        } else if (l->state.column != UINT32_MAX)
            ++l->state.column;
    }
}
void script_whitespace(qa_script_lexer *l) {
    for (;;) {
        uint8_t c = script_peek(l, 0);
        if (c == 0)
            return;
        if (c <= 32 || c >= 128) {
            script_advance(l, 1);
            continue;
        }
        if (c == '/' && script_peek(l, 1) == '/') {
            script_advance(l, 2);
            while ((c = script_peek(l, 0)) != 0 && c != '\n')
                script_advance(l, 1);
            continue;
        }
        if (c == '/' && script_peek(l, 1) == '*') {
            script_advance(l, 2);
            while (script_peek(l, 0) != 0 &&
                   !(script_peek(l, 0) == '*' && script_peek(l, 1) == '/'))
                script_advance(l, 1);
            if (script_peek(l, 0) != 0)
                script_advance(l, 2);
            continue;
        }
        return;
    }
}
bool qa_script_lexer_open(const char *path, qa_bytes input, const qa_script_lexer_options *options,
                          qa_script_lexer **out, qa_error *e) {
    if (path == NULL || out == NULL || (input.size != 0 && input.data == NULL)) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid script lexer input");
        return false;
    }
    qa_script_lexer *l = calloc(1, sizeof(*l));
    if (l == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating script lexer");
        return false;
    }
    l->input = input;
    if (options != NULL)
        l->options = *options;
    if (l->options.token_limit == 0)
        l->options.token_limit = 1024;
    if (l->options.token_limit < 4 || l->options.token_limit > UINT32_MAX) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid script token limit");
        goto fail;
    }
    l->path = script_string(&l->arena, path, strlen(path), e);
    if (l->path == NULL)
        goto fail;
    l->state.line = 1;
    l->state.column = 1;
    const qa_script_punctuation *spec = l->options.punctuations;
    size_t count = l->options.punctuation_count;
    if (spec == NULL) {
        default_punctuation_index();
        l->punctuations = defaults;
        l->heads = default_heads;
        l->next = default_next;
        l->punctuation_count = sizeof(defaults) / sizeof(*defaults);
        *out = l;
        return true;
    }
    if (count > INT32_MAX || count > SIZE_MAX / sizeof(*l->punctuations) ||
        count > SIZE_MAX / sizeof(*l->next) - 256) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid script punctuation count");
        goto fail;
    }
    l->owned_punctuations = calloc(count == 0 ? 1 : count, sizeof(*l->owned_punctuations));
    l->owned_index = malloc((256 + count) * sizeof(*l->owned_index));
    if (l->owned_punctuations == NULL || l->owned_index == NULL) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating script punctuation index");
        goto fail;
    }
    l->punctuations = l->owned_punctuations;
    l->heads = l->owned_index;
    l->next = l->owned_index + 256;
    l->punctuation_count = count;
    for (size_t i = 0; i < count; ++i) {
        if (spec[i].text == NULL || spec[i].text[0] == 0) {
            qa_error_set(e, QA_ERROR_ARGUMENT, i, "Empty script punctuation");
            goto fail;
        }
        size_t length = strlen(spec[i].text);
        l->owned_punctuations[i] =
            (qa_script_punctuation){script_string(&l->arena, spec[i].text, length, e), spec[i].id};
        if (l->punctuations[i].text == NULL)
            goto fail;
    }
    punctuation_index(l->punctuations, count, l->owned_index, l->owned_index + 256);
    *out = l;
    return true;
fail:
    qa_script_lexer_close(l);
    return false;
}
void qa_script_lexer_close(qa_script_lexer *l) {
    if (l == NULL)
        return;
    free(l->owned_punctuations);
    free(l->owned_index);
    qa_arena_destroy(&l->arena);
    free(l);
}
bool qa_script_token_is(const qa_script_token *token, const char *text) {
    if (token == NULL || text == NULL)
        return false;
    size_t size = strlen(text);
    return size == token->text.size && (size == 0 || memcmp(token->text.data, text, size) == 0);
}
qa_bytes qa_script_token_value(const qa_script_token *token) {
    if (token == NULL)
        return (qa_bytes){0};
    qa_bytes value = token->text;
    if ((token->kind == QA_SCRIPT_STRING || token->kind == QA_SCRIPT_LITERAL) && value.size >= 2) {
        ++value.data;
        value.size -= 2;
    }
    return value;
}
bool qa_script_lexer_next(qa_script_lexer *l, qa_script_token *out, bool *found, qa_error *e) {
    if (l == NULL || out == NULL || found == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid script token request");
        return false;
    }
    l->source_failure = false;
    *found = false;
    if (l->state.unread) {
        *out = l->state.token;
        l->state.unread = false;
        *found = true;
        return true;
    }
    *out = (qa_script_token){0};
    size_t whitespace = l->state.offset;
    uint32_t before = l->state.line;
    script_whitespace(l);
    if (script_peek(l, 0) == 0)
        return true;
    *out = (qa_script_token){
        .location = qa_script_lexer_position(l),
        .lines_crossed = l->state.line - before,
        .leading_whitespace = {l->input.data + whitespace, l->state.offset - whitespace}};
    size_t start = l->state.offset;
    uint8_t c = script_peek(l, 0);
    if (c == '"' || c == '\'') {
        if (!script_quoted(l, out, e))
            return false;
    } else if (script_digit(c) || (c == '.' && script_digit(script_peek(l, 1)))) {
        if (!script_number(l, out, e))
            return false;
    } else if ((l->options.flags & QA_SCRIPT_PRIMITIVE_TOKENS) != 0) {
        out->text = (qa_bytes){l->input.data + start, 0};
        while ((c = script_peek(l, 0)) > 32 && c < 128 && c != ';') {
            if (out->text.size >= l->options.token_limit)
                return script_error(l, "Primitive exceeds script token limit", e);
            script_advance(l, 1);
            out->text.size = l->state.offset - start;
        }
        if (out->text.size == l->options.token_limit)
            return script_unsupported(l, "Primitive token has no source string terminator", e);
    } else if (script_alpha(c)) {
        out->kind = QA_SCRIPT_NAME;
        out->text = (qa_bytes){l->input.data + start, 0};
        while (script_name(script_peek(l, 0))) {
            script_advance(l, 1);
            out->text.size = l->state.offset - start;
            if (l->state.offset - start >= l->options.token_limit)
                return script_error(l, "Name exceeds script token limit", e);
        }
        out->subtype = (uint32_t)out->text.size;
    } else {
        int32_t index = l->heads[c];
        while (index >= 0) {
            const char *text = l->punctuations[index].text;
            size_t size = strlen(text);
            if (size <= l->input.size - start && memcmp(l->input.data + start, text, size) == 0) {
                out->kind = QA_SCRIPT_PUNCTUATION;
                out->text = (qa_bytes){(const uint8_t *)text, size};
                out->subtype = l->punctuations[index].id;
                script_advance(l, size);
                break;
            }
            index = l->next[index];
        }
        if (index < 0)
            return script_error(l, "Cannot read script token", e);
    }
    *found = true;
    return true;
}
bool qa_script_lexer_unread(qa_script_lexer *l, const qa_script_token *token, qa_error *e) {
    if (l == NULL || token == NULL || l->state.unread) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Script lexer already has an unread token");
        return false;
    }
    l->state.unread = true;
    l->state.token = *token;
    return true;
}
void qa_script_lexer_reset(qa_script_lexer *l) {
    if (l != NULL)
        l->state = (qa_script_lexer_state){.line = 1, .column = 1};
}
bool qa_script_lexer_capture(const qa_script_lexer *l, qa_script_lexer_state *out, qa_error *e) {
    if (l == NULL || out == NULL) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Missing lexer checkpoint input/output");
        return false;
    }
    *out = l->state;
    return true;
}
bool qa_script_lexer_restore(qa_script_lexer *l, const qa_script_lexer_state *state, qa_error *e) {
    if (l == NULL || state == NULL || state->offset > l->input.size || state->line == 0 ||
        state->column == 0 ||
        (state->unread && (state->token.text.size >= l->options.token_limit ||
                           (state->token.text.size != 0 && state->token.text.data == NULL) ||
                           (state->token.leading_whitespace.size != 0 &&
                            state->token.leading_whitespace.data == NULL)))) {
        qa_error_set(e, QA_ERROR_ARGUMENT, 0, "Invalid lexer checkpoint");
        return false;
    }
    qa_script_lexer_state copy = *state;
    if (!copy.unread)
        copy.token = (qa_script_token){0};
    if (copy.unread) {
        char *text = script_string(&l->arena, copy.token.text.data, copy.token.text.size, e);
        char *whitespace = script_string(&l->arena, copy.token.leading_whitespace.data,
                                         copy.token.leading_whitespace.size, e);
        const char *path = copy.token.location.path == NULL ? l->path : copy.token.location.path;
        char *stored_path = script_string(&l->arena, path, strlen(path), e);
        if (text == NULL || whitespace == NULL || stored_path == NULL)
            return false;
        copy.token.text.data = (const uint8_t *)text;
        copy.token.leading_whitespace.data = (const uint8_t *)whitespace;
        copy.token.location.path = stored_path;
    }
    l->state = copy;
    return true;
}

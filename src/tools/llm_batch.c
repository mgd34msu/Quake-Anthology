#include "llm_internal.h"
#include <stdlib.h>
#include <string.h>

static bool equal(const char *a, const char *b) {
    while (*a && *b) {
        unsigned x = (unsigned char)*a++, y = (unsigned char)*b++;
        if (x >= 'A' && x <= 'Z') x += 'a' - 'A';
        if (y >= 'A' && y <= 'Z') y += 'a' - 'A';
        if (x != y) return false;
    }
    return *a == *b;
}
static bool indirect(const char *name) {
    static const char *const names[] = {"llm_ask", "llm_exec", "llm_cancel", "exec", "vstr", "alias", "bind", "stuffcmds", "cmd", "wait"};
    for (size_t i = 0; i < sizeof names / sizeof names[0]; ++i) if (equal(name, names[i])) return true;
    return false;
}
typedef struct batch {
    qa_console *console;
    const qa_command_context *context;
    size_t maximum_command, maximum_buffer, commands;
    llm_text text;
} batch;
static bool source_q2(qa_console_dialect dialect) { return dialect == QA_CONSOLE_Q2 || dialect == QA_CONSOLE_Q2_RERELEASE; }
static bool known_variable(batch *b, const char *name) {
    if (qa_cvars_find(qa_console_cvar_owner(b->console, b->context, name), name)) return true;
    for (size_t i = 0;; ++i) {
        qa_cvars *registry = qa_console_visible_cvars(b->console, b->context, i);
        if (!registry) break;
        for (size_t j = 0; j < qa_cvars_count(registry); ++j) {
            const qa_cvar_view *var = qa_cvars_at(registry, j);
            if (equal(var->name, name) && qa_console_cvar_owner(b->console, b->context, var->name) == registry) return true;
        }
    }
    return false;
}
static bool visit(batch *, qa_bytes, const char *const *, size_t, qa_error *);
static bool admit_line(batch *b, qa_bytes line, const char *const *ancestry, size_t depth, qa_error *error) {
    if (line.size >= b->maximum_command) return llm_fail(error, "a command exceeds the engine line limit");
    if (source_q2(b->context->dialect)) {
        size_t token = 0;
        for (size_t i = 0; i < line.size; ++i) {
            token = line.data[i] <= 32 || line.data[i] == '"' ? 0 : token + 1;
            if (token >= 128) return llm_fail(error, "a command token exceeds the engine limit");
        }
    }
    char *text = malloc(line.size + 1);
    if (!text) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating validated console line"); return false; }
    memcpy(text, line.data, line.size); text[line.size] = 0;
    qa_command_tokens tokens = {0}; bool ok = false;
    if (!qa_command_tokenize(text, b->context->dialect, false, &tokens, error)) goto done;
    if (!tokens.count || (b->context->dialect != QA_CONSOLE_Q3 && tokens.count >= 80)) { llm_fail(error, "a command has no name or too many arguments"); goto done; }
    const char *name = tokens.values[0];
    if (indirect(name)) { llm_fail(error, "llm_exec requires literal commands without scripts, bindings, waits or LLM calls"); goto done; }
    const qa_console_entry *command = qa_console_find(b->console, b->context, name);
    if (!command && b->context->dialect != QA_CONSOLE_Q3) {
        const qa_console_entry *alias = NULL;
        for (size_t i = 0; (alias = qa_console_alias_at(b->console, b->context->owner, i)) != NULL; ++i) {
            if (!equal(alias->name, name)) continue;
            for (size_t j = 0; j < depth; ++j) if (equal(ancestry[j], name)) { llm_fail(error, "recursive console alias rejected"); goto done; }
            const char *path[9];
            for (size_t j = 0; j < depth; ++j) path[j] = ancestry[j];
            path[depth] = name;
            ok = visit(b, (qa_bytes){(const uint8_t *)alias->alias_text, strlen(alias->alias_text)}, path, depth + 1, error); goto done;
        }
    }
    if (!command && !known_variable(b, name)) { llm_fail(error, "unknown command or setting; use find or help, then retry"); goto done; }
    static const char *const setters[] = {"set", "seta", "setu", "sets", "reset", "toggle", "inc", "dec"};
    for (size_t i = 0; i < sizeof setters / sizeof setters[0]; ++i) if (equal(name, setters[i])) {
        if (tokens.count < 2 || !known_variable(b, tokens.values[1])) { llm_fail(error, "generated settings commands require an existing console variable"); goto done; }
        break;
    }
    if (b->commands >= 32 || line.size + 1 > 4096 - b->text.buffer.size ||
        b->text.buffer.size + line.size + 1 >= b->maximum_buffer) { llm_fail(error, "expanded response exceeds 32 commands or the engine buffer limit"); goto done; }
    if (!llm_text_add(&b->text, line, error) || !llm_text_string(&b->text, "\n", error)) goto done;
    ++b->commands; ok = true;
done:
    qa_command_tokens_free(&tokens); free(text); return ok;
}
static bool visit(batch *b, qa_bytes bytes, const char *const *ancestry, size_t depth, qa_error *error) {
    if (depth > 8) return llm_fail(error, "alias expansion exceeds eight levels");
    if (bytes.size > 4096 || (bytes.size && !bytes.data)) return llm_fail(error, "response exceeds 4096 command characters");
    bool quoted = false; size_t start = 0;
    for (size_t i = 0; i <= bytes.size; ++i) {
        uint8_t c = i < bytes.size ? bytes.data[i] : 0;
        if (i < bytes.size && (c > 126 || (c < 32 && c != '\n' && c != '\r' && c != '\t')))
            return llm_fail(error, "response contains unsupported command characters");
        if (c == '"') quoted = !quoted;
        if (!quoted && (c == '`' || (c == '/' && i + 1 < bytes.size && (bytes.data[i + 1] == '/' || bytes.data[i + 1] == '*'))))
            return llm_fail(error, "return commands only without comments or Markdown");
        if (!quoted && c == '$' && source_q2(b->context->dialect)) return llm_fail(error, "variable macro expansion is not supported; return literal values");
        if (i == bytes.size || c == '\n' || c == '\r' || (c == ';' && !quoted)) {
            if (quoted) return llm_fail(error, "response contains an unterminated quote");
            size_t a = start, end = i;
            while (a < end && bytes.data[a] <= 32) ++a;
            while (end > a && bytes.data[end - 1] <= 32) --end;
            if (end > a && !admit_line(b, (qa_bytes){bytes.data + a, end - a}, ancestry, depth, error)) return false;
            start = i + 1;
        }
    }
    return true;
}
bool qa_llm_validate_batch(qa_console *console, const qa_command_context *context, qa_bytes bytes, qa_buffer *out, qa_error *error) {
    if (!console || !context || !out) return llm_fail(error, "invalid console batch admission");
    batch b = {.console = console, .context = context};
    /* Shared console accessor must report actual configured source limits. */
    if (!qa_console_limits(console, context, &b.maximum_command, &b.maximum_buffer, error)) return false;
    bool ok = visit(&b, bytes, NULL, 0, error);
    if (ok && !b.commands) ok = llm_fail(error, "model returned no commands; try a more specific request");
    if (ok) { *out = b.text.buffer; b.text.buffer = (qa_buffer){0}; }
    qa_buffer_free(&b.text.buffer); return ok;
}

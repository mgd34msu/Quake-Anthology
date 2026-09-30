#include "llm_internal.h"
#include "qa/text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct console_request {
    struct console_request *next;
    llm_console *binding;
    qa_command_context source;
    qa_llm_request_id id;
    qa_buffer answer;
    qa_error error;
    bool execute, ready;
} console_request;
struct llm_console {
    llm_console *next;
    qa_llm *owner;
    qa_console *console;
    console_request *requests;
};
static const struct { const char *name, *usage, *summary; } definitions[] = {
    {"llm_ask", "llm_ask <question>", "Ask the configured LLM and print its answer in this console."},
    {"llm_exec", "llm_exec <request>", "Ask the configured LLM for literal commands, validate the whole batch, then execute in this context."},
    {"llm_cancel", "llm_cancel", "Cancel this console's pending LLM request."}
};
static bool local(const qa_command_context *source) {
    return source->direct && !source->script && (source->origin == QA_COMMAND_LOCAL || source->origin == QA_COMMAND_SEAT);
}
static bool equal(const char *a, const char *b) {
    while (*a && *b) { unsigned x = (unsigned char)*a++, y = (unsigned char)*b++; if (x >= 'A' && x <= 'Z') x += 32; if (y >= 'A' && y <= 'Z') y += 32; if (x != y) return false; }
    return *a == *b;
}
static bool same_console(const qa_command_context *a, const qa_command_context *b) {
    return a->origin == b->origin && (a->origin != QA_COMMAND_SEAT || a->seat == b->seat);
}
static void print(llm_console *binding, const qa_command_context *source, const char *text) {
    qa_llm *s = binding->owner;
    if (!s->options.context_active(s->options.context, source)) return;
    if (qa_console_output_redirected(binding->console)) qa_console_emit(binding->console, source, text);
    else if (s->options.print) s->options.print(s->options.context, source, text);
}
static void retire(console_request *request) { if (request) { qa_buffer_free(&request->answer); free(request); } }
static void cancel(llm_console *binding, console_request **link) {
    console_request *request = *link; *link = request->next;
    qa_llm_cancel(binding->owner, request->id); retire(request);
}
static void completed(void *context, qa_llm_request_id id, qa_bytes answer, const qa_error *error) {
    console_request *request = context; (void)id;
    request->ready = true;
    if (error) { request->error = *error; return; }
    if (memchr(answer.data, 0, answer.size)) { llm_fail(&request->error, "LLM answer contains unsupported console NUL characters"); return; }
    request->answer.data = malloc(answer.size + 1);
    if (!request->answer.data) { qa_error_set(&request->error, QA_ERROR_MEMORY, 0, "copying LLM console answer"); return; }
    if (answer.size) memcpy(request->answer.data, answer.data, answer.size);
    request->answer.data[answer.size] = 0; request->answer.size = answer.size;
}
static bool command(void *context, const qa_command_invocation *call, qa_error *error) {
    llm_console *binding = context; qa_llm *s = binding->owner;
    if (s->busy) return llm_fail(error, "LLM commands require callbacks to return");
    if (!local(&call->context)) return llm_fail(error, "LLM commands require direct input from a local player console");
    ++s->busy; bool active = s->options.context_active(s->options.context, &call->context); --s->busy;
    if (!active) return llm_fail(error, "LLM console context has retired");
    console_request **link = &binding->requests;
    while (*link && !same_console(&(*link)->source, &call->context)) link = &(*link)->next;
    if (equal(call->argv[0], "llm_cancel")) {
        if (call->argc != 1) return llm_fail(error, "usage: llm_cancel");
        ++s->busy;
        if (*link) { cancel(binding, link); print(binding, &call->context, "LLM request canceled.\n"); }
        else print(binding, &call->context, "No LLM request is running in this console.\n");
        --s->busy; return true;
    }
    if (*link) return llm_fail(error, "an LLM request is already running in this console");
    llm_text prompt = {0}; qa_buffer instructions = {0}; console_request *request = NULL; bool ok = false;
    for (size_t i = 1; i < call->argc; ++i) if ((i > 1 && !llm_text_string(&prompt, " ", error)) || !llm_text_string(&prompt, call->argv[i], error)) goto done;
    qa_bytes trimmed = llm_trim((qa_bytes){prompt.buffer.data, prompt.buffer.size});
    if (!trimmed.size) { llm_fail(error, "usage: llm_ask <question> or llm_exec <request>; quote semicolons"); goto done; }
    size_t cursor = 0, units = 0; uint32_t scalar;
    while (qa_utf8_next(trimmed, &cursor, &scalar)) units += scalar > 0xffff ? 2 : 1;
    if (!qa_utf8_valid(trimmed) || cursor != trimmed.size || units > 4096) { llm_fail(error, "LLM prompt exceeds 4096 characters or contains invalid UTF-8"); goto done; }
    if (trimmed.data != prompt.buffer.data) memmove(prompt.buffer.data, trimmed.data, trimmed.size);
    prompt.buffer.size = trimmed.size; prompt.buffer.data[trimmed.size] = 0;
    bool execute = equal(call->argv[0], "llm_exec");
    if (!llm_console_instructions(binding->console, &call->context, (const char *)prompt.buffer.data, execute, &instructions, error)) goto done;
    request = calloc(1, sizeof *request);
    if (!request) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating console LLM request"); goto done; }
    request->binding = binding; request->source = call->context; request->execute = execute;
    if (s->options.capture_context) {
        ++s->busy; bool captured = s->options.capture_context(s->options.context, &call->context, &request->source, error); --s->busy;
        if (!captured) goto done;
    }
    request->source.script = NULL;
    qa_llm_observer observer = {.context = request, .complete = completed};
    if (!qa_llm_request(s, (const char *)prompt.buffer.data, (const char *)instructions.data, &observer, &request->id, error)) goto done;
    request->next = binding->requests; binding->requests = request; request = NULL;
    ++s->busy; print(binding, &call->context, "LLM request started. Use llm_cancel to cancel.\n"); --s->busy; ok = true;
done:
    retire(request); qa_buffer_free(&prompt.buffer); qa_buffer_free(&instructions); return ok;
}
bool qa_llm_attach_console(qa_llm *s, qa_console *console, qa_error *error) {
    if (!s || !console || s->busy) return llm_fail(error, "invalid LLM console admission");
    for (llm_console *b = s->consoles; b; b = b->next) if (b->console == console) return true;
    llm_console *b = calloc(1, sizeof *b);
    if (!b) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating LLM console binding"); return false; }
    b->owner = s; b->console = console; size_t count = 0;
    for (size_t i = 0; i < sizeof definitions / sizeof definitions[0]; ++i) {
        if (!qa_console_register_owned(console, definitions[i].name, definitions[i].summary, 0, s->options.owner, true, command, b, error)) goto failed;
        ++count;
        qa_console_documentation doc = {.usage = definitions[i].usage};
        if (!qa_console_document(console, definitions[i].name, 0, &doc, error)) goto failed;
    }
    b->next = s->consoles; s->consoles = b; return true;
failed:
    while (count) (void)qa_console_unregister(console, definitions[--count].name, 0);
    free(b); return false;
}
bool qa_llm_detach_console(qa_llm *s, qa_console *console, qa_error *error) {
    if (!s || s->busy) return llm_fail(error, "LLM console detach requires callbacks to return");
    llm_console **link = &s->consoles;
    while (*link && (*link)->console != console) link = &(*link)->next;
    if (!*link) return true;
    llm_console *b = *link; *link = b->next;
    while (b->requests) cancel(b, &b->requests);
    for (size_t i = 0; i < sizeof definitions / sizeof definitions[0]; ++i) (void)qa_console_unregister(console, definitions[i].name, 0);
    free(b); return true;
}
bool llm_console_detach_all(qa_llm *s, qa_error *error) {
    while (s->consoles) if (!qa_llm_detach_console(s, s->consoles->console, error)) return false;
    return true;
}
bool qa_llm_before_world_change(qa_llm *s, qa_error *error) {
    if (!s) return true;
    if (s->busy) return llm_fail(error, "LLM world retirement requires callbacks to return");
    return llm_console_detach_all(s, error);
}
bool llm_console_tick(qa_llm *s, qa_error *error) {
    (void)error;
    for (llm_console *b = s->consoles; b; b = b->next) {
        for (console_request **link = &b->requests; *link;) {
            console_request *request = *link;
            if (!s->options.context_active(s->options.context, &request->source)) { cancel(b, link); continue; }
            if (!request->ready) { link = &request->next; continue; }
            qa_error failure = request->error; qa_buffer batch = {0};
            if (failure.code == QA_OK && request->execute) {
                if (qa_llm_validate_batch(b->console, &request->source, (qa_bytes){request->answer.data, request->answer.size}, &batch, &failure)) {
                    print(b, &request->source, (const char *)batch.data);
                    qa_command_context source = request->source; source.direct = false; source.console_text = false;
                    /* Execute the admitted batch through ordinary handlers and
                     * permissions. Earlier effects remain if a handler fails. */
                    if (s->options.context_active(s->options.context, &source)) (void)qa_console_execute_now(b->console, &source, (const char *)batch.data, &failure);
                }
            } else if (failure.code == QA_OK) {
                print(b, &request->source, (const char *)request->answer.data); print(b, &request->source, "\n");
            }
            if (failure.code != QA_OK) { char text[512]; (void)snprintf(text, sizeof text, "LLM request failed: %s\n", failure.message); print(b, &request->source, text); }
            qa_buffer_free(&batch); *link = request->next; retire(request);
        }
    }
    return true;
}

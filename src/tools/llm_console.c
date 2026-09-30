#include "llm_internal.h"
#include "save_internal.h"
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
    char *script;
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
static void retire(console_request *request) { if (request) { qa_buffer_free(&request->answer); free(request->script); free(request); } }
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
bool llm_console_observer_encode(const qa_llm *s, const qa_llm_observer *observer, uint64_t *id) {
    if (observer->text || observer->complete != completed) return false;
    for (const llm_console *b = s->consoles; b; b = b->next)
        for (const console_request *r = b->requests; r; r = r->next)
            if (observer->context == r && !r->ready) { *id = r->id; return true; }
    return false;
}
bool llm_console_observer_decode(qa_llm *s, uint64_t id, qa_llm_observer *observer) {
    for (llm_console *b = s->consoles; b; b = b->next)
        for (console_request *r = b->requests; r; r = r->next)
            if (r->id == id && !r->ready) { *observer = (qa_llm_observer){.context = r, .complete = completed}; return true; }
    return false;
}
void llm_console_private_free(qa_llm *s) {
    while (s->consoles) {
        llm_console *b = s->consoles; s->consoles = b->next;
        while (b->requests) { console_request *r = b->requests; b->requests = r->next; retire(r); }
        free(b);
    }
}
void llm_console_exchange(qa_llm *stable, qa_llm *candidate) {
    llm_console *result = NULL, **tail = &result;
    while (candidate->consoles) {
        llm_console *stage = candidate->consoles; candidate->consoles = stage->next;
        llm_console **link = &stable->consoles; while ((*link)->console != stage->console) link = &(*link)->next;
        llm_console *binding = *link; *link = binding->next;
        while (binding->requests) { console_request *r = binding->requests; binding->requests = r->next; retire(r); }
        binding->requests = stage->requests; binding->owner = stable; binding->next = NULL;
        for (console_request *r = binding->requests; r; r = r->next) r->binding = binding;
        *tail = binding; tail = &binding->next; free(stage);
    }
    candidate->consoles = result;
}
bool llm_console_fields(qa_source_save_io *io, qa_llm *s, const qa_llm *installed, const qa_llm_checkpoint_refs *refs) {
    size_t count = 0; for (llm_console *b = s->consoles; b; b = b->next) ++count;
    if (!qa_source_save_count(io, &count, SIZE_MAX / sizeof(llm_console))) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        size_t actual = 0; for (llm_console *b = installed->consoles; b; b = b->next) ++actual;
        if (actual != count) return tool_save_fail(io, "saved LLM console roster differs from installed candidate");
    }
    llm_console *binding = s->consoles, **tail = &s->consoles;
    for (size_t i = 0; i < count; ++i) {
        uint64_t id = 0; llm_console *actual = NULL;
        if (io->direction == QA_SOURCE_SAVE_WRITE && !refs->console_encode(refs->context, binding->console, &id, io->error)) return false;
        if (!qa_source_save_u64(io, &id)) return false;
        if (io->direction == QA_SOURCE_SAVE_READ) {
            qa_console *console = NULL;
            if (!refs->console_decode(refs->context, id, &console, io->error) || !console) return tool_save_fail(io, "unresolved saved LLM console");
            for (llm_console *b = installed->consoles; b; b = b->next) if (b->console == console) actual = b;
            if (!actual) return tool_save_fail(io, "saved LLM console lacks installed callback bindings");
            for (llm_console *b = s->consoles; b; b = b->next) if (b->console == console) return tool_save_fail(io, "duplicate saved LLM console");
            binding = calloc(1, sizeof *binding); if (!binding) return tool_save_fail(io, "allocating saved LLM binding");
            binding->owner = s; binding->console = console; *tail = binding; tail = &binding->next;
        }
        size_t requests = 0; for (console_request *r = binding->requests; r; r = r->next) ++requests;
        if (!qa_source_save_count(io, &requests, SIZE_MAX / sizeof(console_request))) return false;
        console_request *request = binding->requests, **request_tail = &binding->requests;
        for (size_t j = 0; j < requests; ++j) {
            if (io->direction == QA_SOURCE_SAVE_READ) {
                request = calloc(1, sizeof *request); if (!request) return tool_save_fail(io, "allocating saved LLM console request");
                request->binding = actual; *request_tail = request; request_tail = &request->next;
            }
            if (!qa_source_save_u64(io, &request->id) || !request->id || (s->next_id && request->id >= s->next_id) ||
                !tool_save_context(io, &request->source, &request->script) || request->source.script || !local(&request->source) ||
                !qa_source_save_bool(io, &request->execute) || !qa_source_save_bool(io, &request->ready) ||
                !tool_save_blob(io, &request->answer, true) || !tool_save_error(io, &request->error)) return tool_save_fail(io, "invalid saved LLM console request");
            if (request->answer.data && memchr(request->answer.data, 0, request->answer.size)) return tool_save_fail(io, "saved console answer contains NUL");
            if (request->ready && request->error.code == QA_OK && !request->answer.data) return tool_save_fail(io, "saved successful console result lacks its owned answer");
            qa_command_context qualified = {0};
            if (!refs->command_context(refs->context, &request->source, &qualified, io->error)) return false;
            if (io->direction == QA_SOURCE_SAVE_READ) {
                if (qualified.dialect != request->source.dialect || qualified.origin != request->source.origin || qualified.seat != request->source.seat ||
                    qualified.direct != request->source.direct || qualified.console_text != request->source.console_text)
                    return tool_save_fail(io, "LLM continuation changed deferred command semantics");
                qualified.script = request->script; request->source = qualified;
                for (llm_console *b = s->consoles; b; b = b->next) for (console_request *r = b->requests; r; r = r->next)
                    if (r != request && r->id == request->id) return tool_save_fail(io, "duplicate saved LLM console request identity");
            } else request = request->next;
        }
        if (io->direction == QA_SOURCE_SAVE_WRITE) binding = binding->next;
    }
    return true;
}
static bool command(void *context, const qa_command_invocation *call, qa_error *error) {
    llm_console *binding = context; qa_llm *s = binding->owner;
    if (s->busy || s->pending_restore) return llm_fail(error, "LLM commands require restored continuation and returned callbacks");
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

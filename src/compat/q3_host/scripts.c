#include "internal.h"
#include "qa/bot_runtime.h"

#include <stdio.h>

static q3_script_namespace *namespaces;

static bool namespace_idle(void *context)
{
    q3_script_namespace *owner=context;
    if (owner->reporting) return false;
    for (size_t i=1;i<64;++i)
        if (owner->pending[i] || (owner->scripts[i] && owner->scripts[i]->operations)) return false;
    return true;
}

static bool namespace_close(void *context, bool source_close, qa_error *error)
{
    q3_script_namespace *owner=context;
    if (source_close && owner->generation==UINT64_MAX)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 script lifetime counter exhausted");
    owner->reporting=true;
    qa_q3_host *host=owner->members;
    if (source_close && host && host->options.common.print) {
        for (size_t i=1;i<64;++i) {
            q3_script *script=owner->scripts[i];
            if (!script) continue;
            qa_script_location position=qa_script_position(script->reader);
            const char *path=position.path?position.path:"";
            size_t length=strlen(path);
            if (length>SIZE_MAX-40) {
                owner->reporting=false;
                return q3_fail(error,QA_ERROR_MEMORY,0,"Open PC source filename exceeds reporting extent");
            }
            char *message=malloc(length+40);
            if (!message) {
                owner->reporting=false;
                return q3_fail(error,QA_ERROR_MEMORY,0,"Reporting open PC source filename");
            }
            snprintf(message,length+40,"file %s still open in precompiler\n",path);
            host->options.common.print(host->options.common.context,message); free(message);
        }
    }
    if (source_close) ++owner->generation;
    owner->reporting=false;
    for (size_t i=1;i<64;++i) {
        q3_script *script=owner->scripts[i]; owner->scripts[i]=NULL; q3_script_close(script);
    }
    owner->library=NULL;
    return true;
}

bool q3_script_namespace_library(qa_q3_host *host, qa_error *error)
{
    qa_bot_library *library=qa_bot_runtime_library(host->options.bots);
    q3_script_namespace *owner=host->script_namespace;
    if (!owner->globals || !library || library==owner->library) return true;
    if (owner->globals!=qa_bot_library_global_defines(library))
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"PC global definitions differ from their actual bot library");
    if (owner->library)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Shared PC sources have a different bot library owner");
    if (!qa_bot_library_pc_bind(library,owner,namespace_idle,namespace_close,error)) return false;
    owner->library=library; return true;
}

bool q3_script_namespace_shutdown(qa_q3_host *host, qa_error *error)
{
    q3_script_namespace *owner=host->script_namespace;
    qa_bot_library *library=owner->library;
    if (!namespace_idle(owner))
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"PC namespace shutdown overlaps entered source operations");
    if (!namespace_close(owner,true,error)) return false;
    if (library) qa_bot_library_pc_unbind(library,owner);
    return true;
}

bool q3_script_member(const qa_q3_host *host, size_t slot)
{
    q3_script *script = host->script_namespace->scripts[slot];
    return script && script->member == host;
}

bool q3_script_namespace_bind(qa_q3_host *host, qa_script_defines *globals, qa_error *error)
{
    if (host->script_namespace && host->script_namespace->globals == globals)
        return q3_script_namespace_library(host,error);
    if (host->script_namespace) {
        for (size_t i = 1; i < 64; ++i)
            if (host->script_namespace->scripts[i] || host->script_namespace->pending[i])
                return q3_fail(error, QA_ERROR_ARGUMENT, i, "PC namespace rebind has entered sources");
    }
    q3_script_namespace *owner = namespaces;
    while (owner && (!globals || owner->globals != globals)) owner = owner->next;
    if (!owner) {
        owner = calloc(1, sizeof(*owner));
        if (!owner) return q3_fail(error, QA_ERROR_MEMORY, 0, "Retaining shared PC source namespace");
        owner->globals = globals;
        if (globals) qa_script_defines_retain(globals);
        owner->next = namespaces; namespaces = owner;
    }
    q3_script_namespace_release(host);
    host->script_namespace = owner;
    host->script_member_next = owner->members; owner->members = host;
    if (!q3_script_namespace_library(host,error)) { q3_script_namespace_release(host); return false; }
    return true;
}

void q3_script_namespace_release(qa_q3_host *host)
{
    q3_script_namespace *owner = host->script_namespace;
    if (!owner) return;
    qa_q3_host **member = &owner->members;
    while (*member && *member != host) member = &(*member)->script_member_next;
    if (*member) *member = host->script_member_next;
    host->script_namespace = NULL; host->script_member_next = NULL;
    if (owner->members) {
        for (size_t i = 1; i < 64; ++i)
            if (owner->scripts[i] && owner->scripts[i]->member == host)
                owner->scripts[i]->member = owner->members;
        return;
    }
    q3_script_namespace **link = &namespaces;
    while (*link && *link != owner) link = &(*link)->next;
    if (*link) *link = owner->next;
    if (owner->library) qa_bot_library_pc_unbind(owner->library,owner);
    for (size_t i = 1; i < 64; ++i) q3_script_close(owner->scripts[i]);
    qa_script_defines_release(owner->globals); free(owner);
}

typedef struct script_resource {
    qa_resource *resource;
    char path[];
} script_resource;

static void diagnostic(void *context, const qa_script_diagnostic *diagnostic)
{
    qa_q3_host *host = context;
    if (host->options.common.print) {
        char text[2048];
        snprintf(text, sizeof(text), "file %s, line %u: %s\n",
                   diagnostic->location.path ? diagnostic->location.path : "",
                   diagnostic->location.line, diagnostic->message);
        host->options.common.print(host->options.common.context, text);
    }
}

static bool source(qa_q3_host *host, qa_vfs *mounts, const char *path, qa_script_resource *out,
                     bool *found, qa_error *error)
{
    size_t length = strlen(path);
    if (length >= 32000)
        return q3_fail(error, QA_ERROR_FORMAT, length, "bot script filename exceeds source formatting buffer");
    char lookup[64];
    size_t copied = length < sizeof(lookup) - 1 ? length : sizeof(lookup) - 1;
    memcpy(lookup, path, copied); lookup[copied] = 0;
    if (length >= sizeof(lookup) && host->options.common.print) {
        char text[96];
        snprintf(text, sizeof(text), "Com_sprintf: overflow of %zu in 64\n", length);
        host->options.common.print(host->options.common.context, text);
    }
    qa_resource *resource;
    qa_error local = {0};
    if (!qa_vfs_acquire(mounts, lookup, &resource, NULL, &local)) {
        if (local.code == QA_ERROR_NOT_FOUND) { *found = false; return true; }
        if (error) *error = local;
        return false;
    }
    script_resource *lease = malloc(sizeof(*lease) + length + 1);
    if (!lease) {
        qa_resource_release(resource);
        return q3_fail(error, QA_ERROR_MEMORY, 0, "retaining Q3 script source");
    }
    lease->resource = resource; memcpy(lease->path, path, length + 1);
    *out = (qa_script_resource){lease->path, qa_resource_bytes(resource), lease};
    *found = true; return true;
}

static bool read_source_view(qa_q3_host *host, qa_vfs *mounts, const qa_script_include *request,
                          qa_script_resource *out, bool *found, qa_error *error)
{
    if (request->kind == QA_SCRIPT_ROOT) return source(host, mounts, request->requested_path, out, found, error);
    size_t length = strlen(request->requested_path);
    if (request->kind == QA_SCRIPT_INCLUDE_SYSTEM && length >= 64)
        return q3_fail(error, QA_ERROR_FORMAT, length, "system include exceeds source filename buffer");
    if (length >= 32000)
        return q3_fail(error, QA_ERROR_FORMAT, length, "script include exceeds source formatting buffer");
    char *path = malloc(length + 1);
    if (!path) return q3_fail(error, QA_ERROR_MEMORY, 0, "normalizing Q3 script include");
    size_t used = 0;
    for (size_t i = 0; i < length; ++i) {
        char c = request->requested_path[i];
        if (c == '\\') c = '/';
        if (c != '/' || !used || path[used - 1] != '/') path[used++] = c;
    }
    path[used] = 0;
    bool ok = source(host, mounts, path, out, found, error);
    if (ok && !*found && request->kind == QA_SCRIPT_INCLUDE_QUOTED) {
        const char *prefix = request->include_path ? request->include_path : "";
        size_t prefix_length = strlen(prefix);
        if (prefix_length >= 64 || used >= 64 - prefix_length)
            ok = q3_fail(error, QA_ERROR_FORMAT, 0, "quoted include retry exceeds source filename buffer");
        else {
            char retry[64];
            memcpy(retry, prefix, prefix_length); memcpy(retry + prefix_length, path, used + 1);
            ok = source(host, mounts, retry, out, found, error);
        }
    }
    free(path); return ok;
}

static void release_source(void *context, qa_script_resource *resource)
{
    (void)context;
    script_resource *lease = resource->lease;
    if (lease) { qa_resource_release(lease->resource); free(lease); }
    *resource = (qa_script_resource){0};
}

static bool read_source(void *context, const qa_script_include *request,
    qa_script_resource *out, bool *found, qa_error *error)
{
    qa_q3_host *host=context;
    return read_source_view(host,host->options.mounts,request,out,found,error);
}

qa_script_services q3_script_services(qa_q3_host *host)
{
    return (qa_script_services){host, read_source, release_source, diagnostic,
                                  host->options.script_date, host->options.script_time};
}

static bool handle_read(void *context, const qa_script_include *request,
    qa_script_resource *out, bool *found, qa_error *error)
{
    q3_script *script = context;
    if (!script->entered)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "PC include has no entered source host");
    return read_source_view(script->entered,script->mounts,request,out,found,error);
}

static void handle_diagnostic(void *context, const qa_script_diagnostic *value)
{
    q3_script *script = context;
    if (script->entered) diagnostic(script->entered, value);
}

qa_script_services q3_script_handle_services(q3_script *script, qa_q3_host *host)
{
    script->member = host; script->entered = host;
    return (qa_script_services){script, handle_read, release_source, handle_diagnostic,
        host->options.script_date, host->options.script_time};
}

void q3_script_close(q3_script *script)
{
    if (!script) return;
    script->retired = true;
    if (!script->operations) { qa_script_close(script->reader); qa_vfs_destroy(script->mounts); free(script); }
}

static bool load(q3_call *call, int32_t *result, qa_error *error)
{
    qa_q3_host *host = call->host;
    qa_buffer name = {0};
    if (!q3_string(call, call->arguments[0], &name, error)) return false;
    size_t slot = 1;
    while (slot < 64 && (host->script_namespace->scripts[slot] || host->script_namespace->pending[slot])) ++slot;
    if (slot == 64) { qa_buffer_free(&name); return true; }
    q3_script *script = calloc(1, sizeof(*script));
    if (!script) { qa_buffer_free(&name); return q3_fail(error, QA_ERROR_MEMORY, 0, "allocating Q3 script handle"); }
    if (!qa_vfs_retain(host->options.mounts,error)) { free(script); qa_buffer_free(&name); return false; }
    script->mounts=host->options.mounts;
    host->script_namespace->pending[slot] = true;
    uint64_t generation = host->script_namespace->generation;
    qa_script_services services = q3_script_handle_services(script, host);
    qa_script_options options = {.builtins = true, .globals = host->options.script_globals};
    qa_error local = {0};
    bool ok = qa_script_open((const char *)name.data, &services, &options, &script->reader, &local);
    script->entered = NULL;
    host->script_namespace->pending[slot] = false;
    if (ok && generation != host->script_namespace->generation)
        ok = q3_fail(&local, QA_ERROR_ARGUMENT, slot, "Q3 script owner shut down during source load");
    if (ok) { host->script_namespace->scripts[slot] = script; *result = (int32_t)slot; }
    else {
        q3_script_close(script);
        if (local.code == QA_ERROR_NOT_FOUND) ok = true;
        else if (error) *error = local;
    }
    qa_buffer_free(&name); return ok;
}

static bool publish_token(q3_call *call, const qa_script_token *token, qa_error *error)
{
    const uint8_t *end = token->text.size ? memchr(token->text.data, 0, token->text.size) : NULL;
    size_t length = end ? (size_t)(end - token->text.data) : token->text.size;
    if (length > 1023)
        return q3_fail(error, QA_ERROR_FORMAT, length, "Q3 script token exceeds pc_token_t string field");
    bool quoted = token->kind == QA_SCRIPT_STRING && length && token->text.data[0] == '"';
    size_t stripped = length - (quoted ? 1u : 0u);
    if (token->kind == QA_SCRIPT_STRING && !stripped)
        return q3_fail(error, QA_ERROR_FORMAT, 0, "invalid empty source string token");
    uint8_t text[1024];
    if (length) memcpy(text, token->text.data, length);
    text[length] = 0;
    uint32_t subtype = token->subtype, integer = (uint32_t)token->integer, type = token->kind;
    float number = (float)token->number; uint32_t bits; memcpy(&bits, &number, sizeof(bits));
    q3_record admitted;
    uint64_t pointer = call->arguments[1];
    if (!q3_record_open(call, pointer, 1040, &admitted, error)) return false;
    for (size_t i = 0; i <= length; ++i)
        if (!q3_write(call, pointer + 16 + i, (qa_bytes){text + i, 1}, error)) return false;
    if (!q3_write_word(call, pointer, type, error) ||
        !q3_write_word(call, pointer + 4, subtype, error) ||
        !q3_write_word(call, pointer + 8, integer, error) ||
        !q3_write_word(call, pointer + 12, bits, error)) return false;
    if (quoted) {
        for (size_t i = 0; i < length; ++i) {
            uint8_t byte;
            if (!q3_read(call, pointer + 17 + i, &byte, 1, error) ||
                !q3_write(call, pointer + 16 + i, (qa_bytes){&byte, 1}, error)) return false;
        }
    }
    if (type == QA_SCRIPT_STRING) {
        uint8_t byte;
        if (!q3_read(call, pointer + 16 + stripped - 1, &byte, 1, error)) return false;
        if (byte == '"') {
            byte = 0;
            return q3_write(call, pointer + 16 + stripped - 1, (qa_bytes){&byte, 1}, error);
        }
    }
    return true;
}

q3_service_result q3_scripts(q3_call *call, int32_t *result, qa_error *error)
{
    qa_qvm_role role = call->host->options.role;
    int32_t first = role == QA_QVM_UI ? 57 : role == QA_QVM_CGAME ? 64 : 577;
    int32_t operation = role == QA_QVM_GAME && call->service == 204 ? 0 : call->service - first;
    if (operation < 0 || operation > 4 || (role == QA_QVM_GAME && call->service == 577)) return Q3_UNHANDLED;
    qa_q3_host *host = call->host;
    if (operation && host->options.role == QA_QVM_GAME && host->options.bots &&
        qa_bot_runtime_closed(host->options.bots) && !host->script_namespace->reporting) {
        q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 bot script owner is closed"); return Q3_FAILED;
    }
    if (!q3_script_namespace_library(host,error)) return Q3_FAILED;
    if (!host->options.mounts || !host->options.script_globals) {
        q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 script content or global defines are unbound");
        return Q3_FAILED;
    }
    if (!operation) {
        qa_buffer text = {0};
        bool ok = q3_string(call, call->arguments[0], &text, error);
        if (ok) { *result = qa_script_defines_add(host->options.script_globals, (const char *)text.data, error); }
        qa_buffer_free(&text); return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    if (operation == 1) return load(call, result, error) ? Q3_COMPLETED : Q3_FAILED;
    int32_t handle = q3_integer(call, 0);
    q3_script *script = handle > 0 && handle < 64 ? host->script_namespace->scripts[handle] : NULL;
    if (!script) return Q3_COMPLETED;
    if (operation == 2) {
        host->script_namespace->scripts[handle] = NULL; q3_script_close(script); *result = 1; return Q3_COMPLETED;
    }
    if (operation == 4) {
        qa_script_location position = qa_script_source_position(script->reader);
        char path[65]; size_t length = position.path ? strlen(position.path) : 0;
        if (length > 64) length = 64;
        if (length) memcpy(path, position.path, length);
        path[length] = 0;
        uint32_t line = position.line;
        bool ok = q3_write_string(call, call->arguments[1], path,
                                    role == QA_QVM_GAME ? 128 : (int32_t)length + 1, error) &&
                  q3_write_word(call, call->arguments[2], line, error);
        *result = ok; return ok ? Q3_COMPLETED : Q3_FAILED;
    }
    if (script->operations) {
        q3_fail(error, QA_ERROR_ARGUMENT, 0, "cannot recursively read the same Q3 script handle");
        return Q3_FAILED;
    }
    ++script->operations;
    script->entered = host;
    qa_script_token token; bool found = false;
    qa_error local = {0};
    bool ok = qa_script_next(script->reader, &token, &found, &local);
    if (!ok && qa_script_source_failure(script->reader)) { ok = true; found = false; }
    else if (!ok && error) *error = local;
    if (ok && (script->retired || host->script_namespace->scripts[handle] != script))
        ok = q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 script handle was freed during token read");
    if (ok) ok = qa_script_raw_token(script->reader, &token);
    if (ok) ok = publish_token(call, &token, error);
    script->entered = NULL;
    --script->operations;
    if (script->retired) q3_script_close(script);
    if (ok) *result = found;
    return ok ? Q3_COMPLETED : Q3_FAILED;
}

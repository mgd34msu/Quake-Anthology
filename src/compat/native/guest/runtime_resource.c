#include "runtime_resource.h"
#include "qa/binary.h"
#include <stdlib.h>
#include <string.h>

typedef struct runtime_file {
    guest_runtime_file_view view;
    guest_runtime_file_capability capability;
    bool owns_capability;
} runtime_file;
struct guest_runtime_resources {
    runtime_file *files;
    size_t count, capacity;
    bool busy, closing, detached, failed, provisional;
};

static bool fail(qa_error *e, qa_status code, uint64_t id, const char *message)
{ qa_error_set(e, code, (size_t)id, "%s", message); return false; }

static bool capability_valid(const guest_runtime_file_capability *c)
{
    return c && c->id && !(c->mode & ~3u) &&
        (!(c->mode & GUEST_RUNTIME_FILE_READ) || c->read) &&
        (!(c->mode & GUEST_RUNTIME_FILE_WRITE) || c->write) &&
        c->size && c->flush && c->close;
}

bool guest_runtime_resources_create(guest_runtime_resources **out, qa_error *e)
{
    if (!out || *out) return fail(e, QA_ERROR_ARGUMENT, 0, "resource output must be empty");
    guest_runtime_resources *o = calloc(1, sizeof(*o));
    if (!o) return fail(e, QA_ERROR_MEMORY, 0, "owning runtime resource registry");
    *out = o; return true;
}

bool guest_runtime_resources_idle(const guest_runtime_resources *o)
{ return o && !o->busy && !o->closing && !o->detached && !o->failed; }
size_t guest_runtime_resources_count(const guest_runtime_resources *o)
{ return o ? o->count : 0; }

static runtime_file *file_at(guest_runtime_resources *o, uint64_t id)
{
    for (size_t i = 0; o && i < o->count; ++i) if (o->files[i].view.id == id) return &o->files[i];
    return NULL;
}

bool guest_runtime_resources_file(guest_runtime_resources *o, uint64_t id,
    const char *name, uint32_t creation, const guest_runtime_file_capability *c, qa_error *e)
{
    if (!guest_runtime_resources_idle(o) || o->provisional || !id || !name || !capability_valid(c) || file_at(o, id))
        return fail(e, QA_ERROR_ARGUMENT, id, "resource registration needs a unique actual file capability");
    for (size_t i = 0; i < o->count; ++i)
        if (o->files[i].view.capability == c->id)
            return fail(e, QA_ERROR_ARGUMENT, c->id, "file capability already has a close owner");
    size_t length = strlen(name);
    if (length == SIZE_MAX || o->count == SIZE_MAX / sizeof(*o->files))
        return fail(e, QA_ERROR_MEMORY, id, "resource registry extent overflows");
    char *copy = malloc(length + 1);
    if (!copy) return fail(e, QA_ERROR_MEMORY, id, "owning resource opening name");
    memcpy(copy, name, length + 1);
    if (o->count == o->capacity) {
        size_t n = o->capacity && o->capacity <= SIZE_MAX / 2 ? o->capacity * 2 : o->count + 1;
        if (n > SIZE_MAX / sizeof(*o->files)) n = o->count + 1;
        runtime_file *p = realloc(o->files, n * sizeof(*p));
        if (!p) { free(copy); return fail(e, QA_ERROR_MEMORY, id, "growing resource registry"); }
        o->files = p; o->capacity = n;
    }
    o->files[o->count++] = (runtime_file){
        {id, c->id, 0, c->mode, creation, copy, false}, *c, true};
    return true;
}

bool guest_runtime_resources_at(const guest_runtime_resources *o, size_t index,
    guest_runtime_file_view *out, qa_error *e)
{
    if (!o || !out || index >= o->count || o->busy)
        return fail(e, QA_ERROR_ARGUMENT, index, "resource row is unavailable");
    *out = o->files[index].view; return true;
}

bool guest_runtime_resources_find(const guest_runtime_resources *o, uint64_t id,
    guest_runtime_file_view *out, qa_error *e)
{
    runtime_file *f = file_at((guest_runtime_resources *)o, id);
    if (!f || !out || o->busy) return fail(e, QA_ERROR_NOT_FOUND, id, "resource handle is absent");
    *out = f->view; return true;
}

static runtime_file *begin(guest_runtime_resources *o, uint64_t id, uint32_t mode, qa_error *e)
{
    runtime_file *f = file_at(o, id);
    if (!o || o->busy || o->detached || o->closing || o->failed || o->provisional || !f || f->view.closed ||
        !f->owns_capability ||
        (f->view.mode & mode) != mode || !capability_valid(&f->capability)) {
        fail(e, QA_ERROR_ARGUMENT, id, "resource operation requires its live bound handle and mode"); return NULL;
    }
    o->busy = true; return f;
}

bool guest_runtime_resources_read(guest_runtime_resources *o, uint64_t id,
    void *data, size_t bytes, size_t *out, qa_error *e)
{
    if ((!data && bytes) || !out) return fail(e, QA_ERROR_ARGUMENT, id, "file read needs storage and actual byte output");
    runtime_file *f = begin(o, id, GUEST_RUNTIME_FILE_READ, e);
    if (!f) return false;
    size_t done = 0;
    bool okay = bytes <= UINT64_MAX - f->view.offset;
    if (okay) okay = f->capability.read(f->capability.context, f->view.offset, data, bytes, &done, e);
    else fail(e, QA_ERROR_ARGUMENT, id, "file read offset overflows");
    if (done > bytes) { o->failed = true; okay = fail(e, QA_ERROR_FORMAT, id, "file read capability reported impossible completion"); done = 0; }
    f->view.offset += done; *out = done; o->busy = false; return okay;
}

bool guest_runtime_resources_write(guest_runtime_resources *o, uint64_t id,
    qa_bytes data, size_t *out, qa_error *e)
{
    if ((!data.data && data.size) || !out) return fail(e, QA_ERROR_ARGUMENT, id, "file write needs bytes and actual completion output");
    runtime_file *f = begin(o, id, GUEST_RUNTIME_FILE_WRITE, e);
    if (!f) return false;
    size_t done = 0;
    bool okay = data.size <= UINT64_MAX - f->view.offset;
    if (okay) okay = f->capability.write(f->capability.context, f->view.offset, data, &done, e);
    else fail(e, QA_ERROR_ARGUMENT, id, "file write offset overflows");
    if (done > data.size) { o->failed = true; okay = fail(e, QA_ERROR_FORMAT, id, "file write capability reported impossible completion"); done = 0; }
    f->view.offset += done; *out = done; o->busy = false; return okay;
}

bool guest_runtime_resources_seek(guest_runtime_resources *o, uint64_t id, uint64_t offset, qa_error *e)
{
    runtime_file *f = begin(o, id, 0, e);
    if (!f) return false;
    f->view.offset = offset; o->busy = false; return true;
}

bool guest_runtime_resources_size(guest_runtime_resources *o, uint64_t id, uint64_t *out, qa_error *e)
{
    if (!out) return fail(e, QA_ERROR_ARGUMENT, id, "file size output is required");
    runtime_file *f = begin(o, id, 0, e);
    if (!f) return false;
    uint64_t size = 0; bool okay = f->capability.size(f->capability.context, &size, e);
    if (okay) *out = size;
    o->busy = false; return okay;
}

bool guest_runtime_resources_truncate(guest_runtime_resources *o, uint64_t id, uint64_t size, qa_error *e)
{
    runtime_file *f = begin(o, id, GUEST_RUNTIME_FILE_WRITE, e);
    if (!f) return false;
    bool okay = f->capability.truncate ? f->capability.truncate(f->capability.context, size, e) :
        fail(e, QA_ERROR_UNSUPPORTED, id, "file capability has no truncate operation");
    o->busy = false; return okay;
}

bool guest_runtime_resources_flush(guest_runtime_resources *o, uint64_t id, qa_error *e)
{
    runtime_file *f = begin(o, id, 0, e);
    if (!f) return false;
    bool okay = f->capability.flush(f->capability.context, e);
    o->busy = false; return okay;
}

static bool close_file(guest_runtime_resources *o, runtime_file *f, qa_error *e)
{
    if (f->view.closed || !f->owns_capability) return true;
    if (!capability_valid(&f->capability)) return fail(e, QA_ERROR_ARGUMENT, f->view.id, "file close requires its actual external capability");
    o->busy = true;
    bool okay = f->capability.close(f->capability.context, e);
    o->busy = false;
    if (okay) { f->view.closed = true; f->owns_capability = false; memset(&f->capability, 0, sizeof(f->capability)); }
    return okay;
}

bool guest_runtime_resources_close(guest_runtime_resources *o, uint64_t id, qa_error *e)
{
    runtime_file *f = file_at(o, id);
    if (!o || o->busy || o->detached || o->provisional || !f || (!f->view.closed && !f->owns_capability))
        return fail(e, QA_ERROR_ARGUMENT, id, "file close needs its retained resource owner");
    return close_file(o, f, e);
}

bool guest_runtime_resources_remove_closed(guest_runtime_resources *o, uint64_t id, qa_error *e)
{
    runtime_file *f = file_at(o, id);
    if (!guest_runtime_resources_idle(o) || o->provisional || !f || !f->view.closed || f->owns_capability)
        return fail(e, QA_ERROR_ARGUMENT, id, "resource removal requires its genuine completed close receipt");
    size_t index = (size_t)(f - o->files);
    free((void *)f->view.name);
    memmove(f, f + 1, (o->count - index - 1) * sizeof(*f));
    --o->count; return true;
}

void guest_runtime_resources_abandon(guest_runtime_resources **owner)
{
    if (!owner || !*owner || (*owner)->busy) return;
    guest_runtime_resources *o = *owner;
    for (size_t i = 0; i < o->count; ++i) free((void *)o->files[i].view.name);
    free(o->files); free(o); *owner = NULL;
}

bool guest_runtime_resources_destroy(guest_runtime_resources **owner, qa_error *e)
{
    if (!owner) return fail(e, QA_ERROR_ARGUMENT, 0, "resource destruction owner is required");
    guest_runtime_resources *o = *owner;
    if (!o) return true;
    if (o->busy || o->detached) return fail(e, QA_ERROR_ARGUMENT, 0, "resource destruction requires bound idle capabilities");
    if (o->provisional) { guest_runtime_resources_abandon(owner); return true; }
    o->closing = true;
    for (size_t i = 0; i < o->count; ++i) if (!close_file(o, &o->files[i], e)) return false;
    guest_runtime_resources_abandon(owner); return true;
}

/* QGRF1 is detached metadata only. It contains no host pointer or file bytes. */
bool guest_runtime_resources_checkpoint(const guest_runtime_resources *o, qa_buffer *out, qa_error *e)
{
    if (!guest_runtime_resources_idle(o) || !out || out->data || out->size)
        return fail(e, QA_ERROR_ARGUMENT, 0, "resource checkpoint requires idle ownership and empty output");
    size_t bytes = 16;
    for (size_t i = 0; i < o->count; ++i) {
        size_t length = strlen(o->files[i].view.name);
        if (bytes > SIZE_MAX - 48 || length > SIZE_MAX - bytes - 48)
            return fail(e, QA_ERROR_MEMORY, i, "resource checkpoint extent overflows");
        bytes += 48 + length;
    }
    uint8_t *data = calloc(1, bytes);
    if (!data) return fail(e, QA_ERROR_MEMORY, 0, "owning resource checkpoint");
    memcpy(data, "QGRF", 4); qa_store_u32le(data + 4, 1); qa_store_u64le(data + 8, o->count);
    size_t at = 16;
    for (size_t i = 0; i < o->count; ++i) {
        const guest_runtime_file_view *v = &o->files[i].view;
        size_t length = strlen(v->name);
        qa_store_u64le(data + at, v->id); qa_store_u64le(data + at + 8, v->capability);
        qa_store_u64le(data + at + 16, v->offset); qa_store_u32le(data + at + 24, v->mode);
        qa_store_u32le(data + at + 28, v->creation); qa_store_u64le(data + at + 32, v->closed ? 1 : 0);
        qa_store_u64le(data + at + 40, length); memcpy(data + at + 48, v->name, length); at += 48 + length;
    }
    *out = (qa_buffer){data, bytes}; return true;
}

bool guest_runtime_resources_decode(qa_bytes bytes, guest_runtime_resources **out, qa_error *e)
{
    if (!out || *out || !bytes.data || bytes.size < 16 || memcmp(bytes.data, "QGRF", 4) ||
        qa_load_u32le(bytes.data + 4) != 1)
        return fail(e, QA_ERROR_FORMAT, 0, "resource checkpoint header is invalid");
    uint64_t count = qa_load_u64le(bytes.data + 8);
    if (count > (bytes.size - 16) / 48 || count > SIZE_MAX / sizeof(runtime_file))
        return fail(e, QA_ERROR_FORMAT, 8, "resource checkpoint row extent is invalid");
    guest_runtime_resources *o = calloc(1, sizeof(*o));
    if (!o) return fail(e, QA_ERROR_MEMORY, 0, "owning detached resource candidate");
    o->detached = true;
    o->files = count ? calloc((size_t)count, sizeof(*o->files)) : NULL;
    if (count && !o->files) { free(o); return fail(e, QA_ERROR_MEMORY, 0, "owning detached resource rows"); }
    o->capacity = (size_t)count;
    size_t at = 16; bool okay = true;
    for (size_t i = 0; okay && i < count; ++i) {
        if (bytes.size - at < 48) { okay = false; break; }
        guest_runtime_file_view v = {qa_load_u64le(bytes.data + at), qa_load_u64le(bytes.data + at + 8),
            qa_load_u64le(bytes.data + at + 16), qa_load_u32le(bytes.data + at + 24),
            qa_load_u32le(bytes.data + at + 28), NULL, false};
        uint64_t closed = qa_load_u64le(bytes.data + at + 32), length = qa_load_u64le(bytes.data + at + 40);
        at += 48;
        if (!v.id || !v.capability || v.mode & ~3u || closed > 1 ||
            length >= SIZE_MAX || length > bytes.size - at || memchr(bytes.data + at, 0, (size_t)length)) { okay = false; break; }
        for (size_t j = 0; j < o->count; ++j)
            if (o->files[j].view.id == v.id || o->files[j].view.capability == v.capability) okay = false;
        if (!okay) break;
        char *name = malloc((size_t)length + 1);
        if (!name) { fail(e, QA_ERROR_MEMORY, at, "owning restored resource name"); okay = false; break; }
        memcpy(name, bytes.data + at, (size_t)length); name[length] = 0; at += (size_t)length;
        v.name = name; v.closed = closed != 0; o->files[o->count++].view = v;
    }
    if (!okay || at != bytes.size) {
        guest_runtime_resources_abandon(&o);
        if (!e || e->code == QA_OK) fail(e, QA_ERROR_FORMAT, at, "resource checkpoint has invalid rows or trailing bytes");
        return false;
    }
    *out = o; return true;
}

bool guest_runtime_resources_rebind(guest_runtime_resources *o,
    guest_runtime_file_resolve_fn resolve, void *context, qa_error *e)
{
    if (!o || !o->detached || o->busy || o->closing)
        return fail(e, QA_ERROR_ARGUMENT, 0, "resource rebind requires a detached candidate");
    guest_runtime_file_capability *rows = o->count ? calloc(o->count, sizeof(*rows)) : NULL;
    if (o->count && !rows) return fail(e, QA_ERROR_MEMORY, 0, "owning candidate capability bindings");
    o->busy = true; bool okay = true;
    for (size_t i = 0; okay && i < o->count; ++i) {
        const guest_runtime_file_view *v = &o->files[i].view;
        if (v->closed) continue;
        okay = resolve && resolve(context, v->capability, &rows[i], e) &&
            capability_valid(&rows[i]) && rows[i].id == v->capability && rows[i].mode == v->mode;
        if (!okay && (!e || e->code == QA_OK)) fail(e, QA_ERROR_FORMAT, v->id, "restored file capability identity or mode differs");
    }
    if (okay) {
        for (size_t i = 0; i < o->count; ++i) o->files[i].capability = rows[i];
        o->detached = false; o->provisional = true;
    }
    o->busy = false; free(rows); return okay;
}

static runtime_file *capability_at(guest_runtime_resources *o, uint64_t id)
{
    for (size_t i = 0; o && i < o->count; ++i) if (o->files[i].view.capability == id) return &o->files[i];
    return NULL;
}

static bool same_capability(const guest_runtime_file_capability *a,
    const guest_runtime_file_capability *b)
{
    return a->id == b->id && a->mode == b->mode && a->context == b->context &&
        a->read == b->read && a->write == b->write && a->size == b->size &&
        a->truncate == b->truncate && a->flush == b->flush && a->close == b->close;
}

bool guest_runtime_resources_adopt(guest_runtime_resources *o,
    guest_runtime_resources *previous, qa_error *e)
{
    if (!guest_runtime_resources_idle(o) || !o->provisional || previous == o ||
        (previous && (!guest_runtime_resources_idle(previous) || previous->provisional)))
        return fail(e, QA_ERROR_ARGUMENT, 0, "resource adoption requires actual quiescent candidate and previous close owners");
    /* Qualify the complete transfer before changing either owner's receipts. */
    for (size_t i = 0; i < o->count; ++i) {
        runtime_file *f = &o->files[i];
        if (f->view.closed) continue;
        if (!capability_valid(&f->capability))
            return fail(e, QA_ERROR_FORMAT, f->view.id, "candidate file has lost its rebound capability");
        if (previous) {
            runtime_file *p = capability_at(previous, f->view.capability);
            if (!p || p->view.closed || !p->owns_capability || !same_capability(&p->capability, &f->capability) ||
                p->view.id != f->view.id || p->view.creation != f->view.creation || strcmp(p->view.name, f->view.name))
                return fail(e, QA_ERROR_FORMAT, f->view.id, "candidate file capability lacks the exact previous close owner");
        }
    }
    for (size_t i = 0; i < o->count; ++i) {
        runtime_file *f = &o->files[i];
        if (f->view.closed) continue;
        if (previous) capability_at(previous, f->view.capability)->owns_capability = false;
        f->owns_capability = true;
    }
    if (previous) previous->closing = true;
    o->provisional = false; return true;
}

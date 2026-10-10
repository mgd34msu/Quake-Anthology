#include "internal.h"

bool native_host_fail(qa_error *error, qa_status status, size_t offset, const char *message)
{
    qa_error_set(error, status, offset, "%s", message);
    return false;
}

bool native_host_read(qa_native_host *host, qa_native_address address, void *out, size_t size,
                      qa_error *error)
{
    if (!host || !host->instance || (!out && size) || (!address && size))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "invalid native host read");
    return qa_native_read(host->instance, address, out, size, error);
}

bool native_host_write(qa_native_host *host, qa_native_address address, const void *data,
                       size_t size, qa_error *error)
{
    if (!host || !host->instance || (!data && size) || (!address && size))
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "invalid native host write");
    return qa_native_write(host->instance, address, (qa_bytes){data, size}, error);
}

bool native_host_read_u8(qa_native_host *host, qa_native_address address, uint8_t *out,
                         qa_error *error)
{
    return native_host_read(host, address, out, 1, error);
}

bool native_host_read_u32(qa_native_host *host, qa_native_address address, uint32_t *out,
                          qa_error *error)
{
    uint8_t bytes[4];
    if (!native_host_read(host, address, bytes, sizeof(bytes), error))
        return false;
    *out = qa_load_u32le(bytes);
    return true;
}

bool native_host_read_i32(qa_native_host *host, qa_native_address address, int32_t *out,
                          qa_error *error)
{
    uint32_t value;
    if (!native_host_read_u32(host, address, &value, error))
        return false;
    *out = (int32_t)value;
    return true;
}

bool native_host_write_u8(qa_native_host *host, qa_native_address address, uint8_t value,
                          qa_error *error)
{
    return native_host_write(host, address, &value, 1, error);
}

bool native_host_write_u32(qa_native_host *host, qa_native_address address, uint32_t value,
                           qa_error *error)
{
    uint8_t bytes[4];
    qa_store_u32le(bytes, value);
    return native_host_write(host, address, bytes, sizeof(bytes), error);
}

bool native_host_write_i32(qa_native_host *host, qa_native_address address, int32_t value,
                           qa_error *error)
{
    return native_host_write_u32(host, address, (uint32_t)value, error);
}

bool native_host_read_vec3(qa_native_host *host, qa_native_address address, qa_vec3 *out,
                           qa_error *error)
{
    uint8_t bytes[12];
    if (!out || !native_host_read(host, address, bytes, sizeof(bytes), error))
        return false;
    out->x = qa_load_f32le(bytes);
    out->y = qa_load_f32le(bytes + 4);
    out->z = qa_load_f32le(bytes + 8);
    return true;
}

static void store_f32(uint8_t *out, float value)
{
    uint32_t bits;
    memcpy(&bits, &value, sizeof(bits));
    qa_store_u32le(out, bits);
}

bool native_host_write_vec3(qa_native_host *host, qa_native_address address, qa_vec3 value,
                            qa_error *error)
{
    uint8_t bytes[12];
    store_f32(bytes, value.x);
    store_f32(bytes + 4, value.y);
    store_f32(bytes + 8, value.z);
    return native_host_write(host, address, bytes, sizeof(bytes), error);
}

bool native_host_string_read(qa_native_host *host, qa_native_address address, qa_bytes *out,
                             qa_error *error)
{
    if (!out)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "native string output is required");
    if (!address) {
        *out = (qa_bytes){(const uint8_t *)"", 0};
        return true;
    }
    qa_bytes source;
    if (!qa_native_string_span(host->instance, address, host->maximum_string_bytes,
            &source, error)) return false;
    uint8_t *text = qa_arena_alloc(&host->text_storage, source.size + 1, 1, error);
    if (!text) return false;
    if (source.data) memcpy(text, source.data, source.size + 1);
    else if (!qa_native_read(host->instance, address, text, source.size + 1, error)) return false;
    *out = (qa_bytes){text, source.size};
    return true;
}

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1u;
    char *copy = malloc(size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

static bool string_object(qa_native_host *host, const char *text,
                          native_host_string **out, qa_error *error)
{
    size_t length = strlen(text);
    if (length >= host->maximum_string_bytes)
        return native_host_fail(error, QA_ERROR_ARGUMENT, length,
                                "native host string exceeds its source limit");
    for (native_host_string *entry = host->strings; entry; entry = entry->next) {
        if (!strcmp(entry->text, text)) {
            *out = entry;
            return true;
        }
    }
    native_host_string *entry = calloc(1, sizeof(*entry));
    if (!entry)
        return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                "allocating native host string record");
    entry->text = copy_text(text);
    entry->bytes = length + 1u;
    if (!entry->text || !qa_native_allocate(host->instance, entry->bytes, INT32_MIN + 1,
                                             &entry->address, error) ||
        !native_host_write(host, entry->address, text, entry->bytes, error)) {
        if (entry->address) {
            qa_error ignored = {0};
            qa_native_free(host->instance, entry->address, &ignored);
        }
        free(entry->text);
        free(entry);
        return false;
    }
    entry->next = host->strings;
    host->strings = entry;
    *out = entry;
    return true;
}


bool native_host_string_address(qa_native_host *host, const char *text,
                                qa_native_address *out, qa_error *error)
{
    if (!host || !text || !out)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native host string and output are required");
    native_host_string *entry;
    if (!string_object(host, text, &entry, error)) return false;
    *out = entry->address;
    return true;
}

bool native_host_temporary_string(qa_native_host *host, const char *text,
                                  qa_native_address *out, qa_error *error)
{
    if (!host || !text || !out)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "temporary native string and output are required");
    size_t length = strlen(text);
    if (length >= host->maximum_string_bytes || length == SIZE_MAX)
        return native_host_fail(error, QA_ERROR_ARGUMENT, length,
                                "temporary native string exceeds its source limit");
    if (!qa_native_allocate(host->instance, length + 1u, INT32_MIN + 2, out, error))
        return false;
    if (native_host_write(host, *out, text, length + 1u, error))
        return true;
    qa_error ignored = {0};
    qa_native_free(host->instance, *out, &ignored);
    *out = 0;
    return false;
}

void native_host_temporary_free(qa_native_host *host, qa_native_address address)
{
    qa_error ignored = {0};
    if (host && host->instance && address)
        qa_native_free(host->instance, address, &ignored);
}

static native_host_cvar_record *find_cvar(qa_native_host *host, const char *name)
{
    for (native_host_cvar_record *record = host->cvar_shadows; record; record = record->next)
        if (!strcmp(record->name, name))
            return record;
    return NULL;
}

bool native_host_store_pointer(const qa_native_host *host, uint8_t *bytes,
                               qa_native_address value, qa_error *error)
{
    if (host->pointer_bytes == 4) {
        if (value > UINT32_MAX)
            return native_host_fail(error, QA_ERROR_FORMAT, 0,
                                    "native pointer exceeds guest width");
        qa_store_u32le(bytes, (uint32_t)value);
    } else {
        qa_store_u64le(bytes, value);
    }
    return true;
}

static bool cvar_bytes(qa_native_host *host, native_host_cvar_record *record,
                       const qa_cvar_view *view, native_host_cvar_record *next,
                       uint8_t bytes[56], qa_error *error)
{
    if ((!record->name_object && !string_object(host, view->name, &record->name_object, error)) ||
        ((!record->value_object || strcmp(record->value_object->text, view->value)) &&
         !string_object(host, view->value, &record->value_object, error))) return false;
    if (!view->latched_value) record->latched_object = NULL;
    else if ((!record->latched_object || strcmp(record->latched_object->text, view->latched_value)) &&
             !string_object(host, view->latched_value, &record->latched_object, error)) return false;
    qa_native_address name = record->name_object->address, value = record->value_object->address;
    qa_native_address latched = record->latched_object ? record->latched_object->address : 0;
    memset(bytes, 0, 56);
    if (host->profile == QA_NATIVE_Q2_GAME_API3) {
        const native_host_classic_layout *layout = host->classic;
        if (!native_host_store_pointer(host, bytes, name, error) ||
            !native_host_store_pointer(host, bytes + layout->cvar.string, value, error) ||
            !native_host_store_pointer(host, bytes + layout->cvar.latched, latched, error) ||
            !native_host_store_pointer(host, bytes + layout->cvar.next,
                                       next ? next->address : 0, error)) return false;
        qa_store_u32le(bytes + layout->cvar.flags, view->flags);
        qa_store_u32le(bytes + layout->cvar.modified, view->modified ? 1u : 0u);
        store_f32(bytes + layout->cvar.value, view->number);
    } else {
        qa_store_u64le(bytes, name);
        qa_store_u64le(bytes + 8, value);
        qa_store_u64le(bytes + 16, latched);
        qa_store_u32le(bytes + 24, view->flags);
        qa_store_u32le(bytes + 28, (uint32_t)view->modification_count);
        store_f32(bytes + 32, view->number);
        qa_store_u64le(bytes + 40, next ? next->address : 0);
        qa_store_u32le(bytes + 48, (uint32_t)view->integer);
    }
    return true;
}

static bool publish_cvar(qa_native_host *host, native_host_cvar_record *record,
                         const qa_cvar_view *view, native_host_cvar_record *next,
                         bool restore, qa_error *error)
{
    uint8_t bytes[56];
    if (!cvar_bytes(host, record, view, next, bytes, error)) return false;
    size_t size = host->classic ? host->classic->cvar.bytes : sizeof(bytes);
    if (!restore) {
        if (!record->published_valid) {
            if (!native_host_write(host, record->address, bytes, size, error)) return false;
        } else {
            size_t offsets[8] = {0, 8, 16, 24, 28, 32, 40, 48};
            size_t lengths[8] = {8, 8, 8, 4, 4, 4, 8, 4};
            size_t count = 8;
            if (host->classic) {
                const native_host_classic_layout *layout = host->classic;
                offsets[1] = layout->cvar.string; offsets[2] = layout->cvar.latched;
                offsets[3] = layout->cvar.flags; offsets[4] = layout->cvar.modified;
                offsets[5] = layout->cvar.value; offsets[6] = layout->cvar.next;
                lengths[0] = lengths[1] = lengths[2] = lengths[6] = host->pointer_bytes;
                count = 7;
            }
            bool value_changed = memcmp(record->published + offsets[1], bytes + offsets[1], lengths[1]) != 0;
            for (size_t i = 0; i < count; ++i) {
                size_t offset = offsets[i], length = lengths[i];
                bool changed = memcmp(record->published + offset, bytes + offset, length) != 0 ||
                    (i == 4 && record->modification != view->modification_count) ||
                    ((i == 5 || i == 7) && value_changed);
                if (!changed) continue;
                uint8_t actual[8];
                if (!native_host_read(host, record->address + offset, actual, length, error)) return false;
                if (memcmp(actual, bytes + offset, length) &&
                    !native_host_write(host, record->address + offset, bytes + offset, length, error)) return false;
            }
        }
    }
    memcpy(record->published, bytes, sizeof(bytes));
    record->published_valid = true;
    record->modification = view->modification_count;
    return true;
}

static void discard_unallocated_cvars(native_host_cvar_record *first)
{
    while (first) {
        native_host_cvar_record *next = first->order_next;
        if (!first->address) { free(first->name); free(first); }
        first = next;
    }
}

static bool refresh_cvars(qa_native_host *host, bool restore, qa_error *error)
{
    if (!host->cvars)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, 0,
                                "native host has no cvar registry");
    uint64_t identity = qa_cvars_view_identity(host->cvars), revision = qa_cvars_revision(host->cvars);
    if (!restore && revision && identity == host->cvar_view_identity && revision == host->cvar_revision)
        return true;
    if (restore || identity != host->cvar_view_identity) {
        for (native_host_cvar_record *row = host->cvar_shadows; row; row = row->next) {
            row->handle = qa_cvars_resolve(host->cvars, row->name);
            if (!restore) row->published_valid = false;
        }
    }
    native_host_cvar_record *cursor = host->cvar_order, *first = NULL, *last = NULL;
    for (const qa_cvar_view *view = qa_cvars_next(host->cvars, NULL); view;
         view = qa_cvars_next(host->cvars, view)) {
        native_host_cvar_record *record = cursor;
        while (record && qa_cvars_read(host->cvars, record->handle) != view) record = record->order_next;
        if (record) cursor = record->order_next;
        else {
            record = find_cvar(host, view->name);
            if (!record) {
                record = calloc(1, sizeof(*record));
                if (record) record->name = copy_text(view->name);
                if (!record || !record->name) {
                    if (record) free(record);
                    discard_unallocated_cvars(first);
                    host->cvar_order = NULL;
                    return native_host_fail(error, QA_ERROR_MEMORY, 0, "allocating native cvar shadow");
                }
            }
            record->handle = qa_cvars_resolve(host->cvars, view->name);
        }
        record->order_previous = last; record->order_next = NULL;
        if (last) last->order_next = record; else first = record;
        last = record;
    }
    host->cvar_order = first;
    for (native_host_cvar_record *record = last; record; record = record->order_previous) {
        bool created = !record->address;
        if (created) {
            size_t size = host->classic ? host->classic->cvar.bytes : 56u;
            if (!qa_native_allocate(host->instance, size, INT32_MIN + 3, &record->address, error)) {
                discard_unallocated_cvars(first); host->cvar_order = NULL; return false;
            }
            record->next = host->cvar_shadows; host->cvar_shadows = record;
        }
        const qa_cvar_view *view = qa_cvars_read(host->cvars, record->handle);
        if (!publish_cvar(host, record, view, record->order_next, restore && !created, error)) {
            discard_unallocated_cvars(first); host->cvar_order = NULL; return false;
        }
    }
    host->cvar_view_identity = identity; host->cvar_revision = revision;
    return true;
}

static bool cvar_guest_modified(qa_native_host *host, native_host_cvar_record *record,
                                qa_error *error)
{
    if (!host->classic || !record->published_valid) return true;
    const qa_cvar_view *view = qa_cvars_read(host->cvars, record->handle);
    if (!view || !view->modified || view->modification_count != record->modification) return true;
    uint32_t modified;
    if (!native_host_read_u32(host, record->address + host->classic->cvar.modified,
                             &modified, error)) return false;
    if (!modified) qa_cvars_clear_modified(host->cvars, view->name);
    return true;
}

bool native_host_refresh_cvars(qa_native_host *host, qa_error *error)
{
    if (host->cvars && qa_cvars_view_identity(host->cvars) == host->cvar_view_identity)
        for (native_host_cvar_record *record = host->cvar_shadows; record; record = record->next)
            if (!cvar_guest_modified(host, record, error)) return false;
    return refresh_cvars(host, false, error);
}

bool native_host_restore_cvars(qa_native_host *host, qa_error *error)
{
    host->cvar_order = NULL;
    return !host->cvars || refresh_cvars(host, true, error);
}

bool native_host_cvar(qa_native_host *host, const char *name, const char *value,
                      uint32_t flags, bool set, qa_native_address *out, qa_error *error)
{
    if (!host->cvars || !name || !out)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native cvar registry, name and output are required");
    const qa_cvar_view *view = qa_cvars_find(host->cvars, name);
    uint64_t owner = host->world.owner ? host->world.owner : host->command_context.owner;
    if (!view && !qa_cvars_register(host->cvars, name, value ? value : "", flags, owner,
                                    NULL, error))
        return false;
    view = qa_cvars_find(host->cvars, name);
    if (view) name = view->name;
    if (view && !set && (flags & ~view->flags) &&
        !qa_cvars_add_flags(host->cvars, name, flags, error))
        return false;
    if (view && view->save_policy == QA_CVAR_SAVE_UNCLASSIFIED &&
        !qa_cvars_declare_save_policy(host->cvars, name,
            host->kind == NATIVE_HOST_Q2_GAME ||
                (host->kind == NATIVE_HOST_Q3 && host->q3_role == QA_QVM_GAME)
                ? QA_CVAR_SAVE_GAMEPLAY : QA_CVAR_SAVE_SETTING, error)) return false;
    if (set && !qa_cvars_set(host->cvars, name, value ? value : "", flags != 0, error))
        return false;
    view = qa_cvars_find(host->cvars, name);
    native_host_cvar_record *record = view ? find_cvar(host, view->name) : NULL;
    if (record && qa_cvars_view_identity(host->cvars) == host->cvar_view_identity &&
        !cvar_guest_modified(host, record, error)) return false;
    if (!refresh_cvars(host, false, error)) return false;
    view = qa_cvars_find(host->cvars, name);
    record = view ? find_cvar(host, view->name) : NULL;
    if (!record)
        return native_host_fail(error, QA_ERROR_NOT_FOUND, 0,
                                "native cvar registration did not publish a record");
    *out = record->address;
    return true;
}

/* These are the actual host-created guest objects. Import owns metadata only;
 * the restored Source process already owns every tagged allocation and byte. */
void native_host_memory_dispose(native_host_memory_state *state)
{
    while (state->strings) {
        native_host_string *row=state->strings; state->strings=row->next;
        free(row->text); free(row);
    }
    while (state->cvar_shadows) {
        native_host_cvar_record *row=state->cvar_shadows; state->cvar_shadows=row->next;
        free(row->name); free(row);
    }
}
static bool memory_text(qa_source_save_io *io, char **text, size_t *bytes, size_t maximum)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (!reading) *bytes=strlen(*text)+1;
    if (!qa_source_save_count(io,bytes,maximum) || !*bytes)
        return native_host_fail(io->error,QA_ERROR_FORMAT,io->offset,"Native host text lacks its bounded terminator");
    if (reading) {
        if (io->offset>io->input.size || *bytes>io->input.size-io->offset)
            return native_host_fail(io->error,QA_ERROR_FORMAT,io->offset,"Native host text leaves its saved extent");
        *text=malloc(*bytes);
        if (!*text) return native_host_fail(io->error,QA_ERROR_MEMORY,io->offset,"Owning restored native host text");
    }
    if (!qa_source_save_bytes(io,*text,*bytes)) return false;
    return (!(*text)[*bytes-1] && !memchr(*text,0,*bytes-1)) ||
        native_host_fail(io->error,QA_ERROR_FORMAT,io->offset,"Native host text has an invalid terminator");
}
static bool memory_allocation(qa_native_host *host,qa_native_address address,size_t bytes,int32_t tag,
    bool qualify,qa_error *error)
{
    if (!address || (host->pointer_bytes==4 && address>UINT32_MAX))
        return native_host_fail(error,QA_ERROR_FORMAT,0,"Native host allocation leaves its actual pointer width");
    if (!qualify) return true;
    qa_native_allocation_info actual;
    if (!qa_native_allocation_query(host->instance,address,&actual,error)) return false;
    if (actual.base!=address || actual.bytes!=bytes || actual.tag!=tag)
        return native_host_fail(error,QA_ERROR_FORMAT,0,"Native host cache differs from its actual tagged Source object");
    return qa_native_range_check(host->instance,address,bytes,QA_NATIVE_MEMORY_READ|QA_NATIVE_MEMORY_WRITE,error);
}
bool native_host_memory_fields(qa_source_save_io *io,qa_native_host *host,
    native_host_memory_state *state,bool qualify_addresses)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    if (reading && (state->strings || state->cvar_shadows))
        return native_host_fail(io->error,QA_ERROR_ARGUMENT,io->offset,"Native memory import requires empty metadata custody");
    size_t count=0;
    if (!reading) for (native_host_string *row=state->strings;row;row=row->next) ++count;
    if (!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if (reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/17))
        return native_host_fail(io->error,QA_ERROR_FORMAT,io->offset,"Native string count exceeds its saved objects");
    native_host_string **strings=&state->strings;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            *strings=calloc(1,sizeof(**strings));
            if (!*strings) return native_host_fail(io->error,QA_ERROR_MEMORY,i,"Owning restored native string custody");
        }
        native_host_string *row=*strings;
        if (!reading && row->bytes!=strlen(row->text)+1)
            return native_host_fail(io->error,QA_ERROR_FORMAT,i,"Native string custody changes its actual allocation extent");
        if (!qa_source_save_u64(io,&row->address) || !memory_text(io,&row->text,&row->bytes,host->maximum_string_bytes) ||
            !memory_allocation(host,row->address,row->bytes,INT32_MIN+1,qualify_addresses,io->error)) return false;
        for (native_host_string *prior=state->strings;prior!=row;prior=prior->next)
            if (prior->address==row->address || !strcmp(prior->text,row->text))
                return native_host_fail(io->error,QA_ERROR_FORMAT,i,"Native string custody repeats a Source object");
        if (qualify_addresses) {
            qa_buffer actual={0};
            bool ok=qa_native_read_string(host->instance,row->address,row->bytes,&actual,io->error);
            if (ok && (actual.size!=row->bytes-1 || memcmp(actual.data,row->text,actual.size)))
                ok=native_host_fail(io->error,QA_ERROR_FORMAT,i,"Native string bytes differ from their restored Source RAM");
            qa_buffer_free(&actual); if (!ok) return false;
        }
        strings=&row->next;
    }
    count=0;
    if (!reading) for (native_host_cvar_record *row=state->cvar_shadows;row;row=row->next) ++count;
    if (!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if (reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/26))
        return native_host_fail(io->error,QA_ERROR_FORMAT,io->offset,"Native cvar-shadow count exceeds its saved objects");
    native_host_cvar_record **cvars=&state->cvar_shadows;
    for (size_t i=0;i<count;++i) {
        if (reading) {
            *cvars=calloc(1,sizeof(**cvars));
            if (!*cvars) return native_host_fail(io->error,QA_ERROR_MEMORY,i,"Owning restored native cvar-shadow custody");
        }
        native_host_cvar_record *row=*cvars; size_t bytes=0;
        if (!qa_source_save_u64(io,&row->address) || !qa_source_save_u64(io,&row->modification) ||
            !memory_text(io,&row->name,&bytes,host->maximum_string_bytes) ||
            !memory_allocation(host,row->address,host->classic?host->classic->cvar.bytes:56u,
                INT32_MIN+3,qualify_addresses,io->error)) return false;
        if (!row->name[0]) return native_host_fail(io->error,QA_ERROR_FORMAT,i,"Native cvar-shadow name is empty");
        if (qualify_addresses) {
            uint8_t pointer[8];
            if (!qa_native_read(host->instance,row->address,pointer,host->pointer_bytes,io->error)) return false;
            qa_native_address name=host->pointer_bytes==4?qa_load_u32le(pointer):qa_load_u64le(pointer);
            native_host_string *source=state->strings;
            while (source && (source->address!=name || strcmp(source->text,row->name))) source=source->next;
            if (!source) return native_host_fail(io->error,QA_ERROR_FORMAT,i,
                "Native cvar-shadow name lacks its literal Source string object");
        }
        for (native_host_cvar_record *prior=state->cvar_shadows;prior!=row;prior=prior->next)
            if (prior->address==row->address || !strcmp(prior->name,row->name))
                return native_host_fail(io->error,QA_ERROR_FORMAT,i,"Native cvar-shadow custody repeats a Source object");
        cvars=&row->next;
    }
    return true;
}

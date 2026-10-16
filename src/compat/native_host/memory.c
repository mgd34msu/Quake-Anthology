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

bool native_host_text_read(qa_native_host *host, qa_native_address address, bool copy,
    qa_bytes *out, qa_error *error)
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
    if (!copy && source.data) { *out = source; return true; }
    uint8_t *text = qa_arena_alloc(&host->text_storage, source.size + 1, 1, error);
    if (!text) return false;
    if (source.data) memcpy(text, source.data, source.size + 1);
    else if (!qa_native_read(host->instance, address, text, source.size + 1, error)) return false;
    *out = (qa_bytes){text, source.size};
    return true;
}

bool qa_native_host_string_span(qa_native_host *host, qa_native_address address,
    const char **out, qa_error *error)
{
    qa_bytes text;
    if (!native_host_text_read(host, address, false, &text, error)) return false;
    *out = (const char *)text.data;
    return true;
}

bool native_host_string_read(qa_native_host *host, qa_native_address address, qa_bytes *out,
    qa_error *error)
{
    return native_host_text_read(host, address, true, out, error);
}

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1u;
    char *copy = malloc(size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

bool qa_native_host_retain_string(qa_native_host *host, qa_string_id name,
    qa_native_address *out, qa_error *error)
{
    if (!host || !host->instance || !out)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "Native string requires its live host");
    if (!name) name = host->empty_string;
    qa_id_index_cursor cursor = qa_id_index_begin(&host->strings, name, 0);
    size_t address;
    if (qa_id_index_next(&host->strings, &cursor, &address)) { *out = address; return true; }
    qa_bytes text = qa_strings_text(host->string_names, name);
    if (!text.data || text.size >= host->maximum_string_bytes)
        return native_host_fail(error, QA_ERROR_ARGUMENT, text.size, "native host string exceeds its source limit");
    if (host->strings.count == host->strings.capacity)
        return native_host_fail(error, QA_ERROR_CAPACITY, 0, "native host retained string storage is full");
    qa_native_address value = 0;
    if (!qa_native_allocate(host->instance, text.size + 1, INT32_MIN + 1, &value, error) ||
        !native_host_write(host, value, text.data, text.size + 1, error)) {
        if (value) { qa_error ignored = {0}; qa_native_free(host->instance, value, &ignored); }
        return false;
    }
    qa_id_index_insert(&host->strings, name, 0, value);
    *out = value;
    return true;
}
bool native_host_temporary_bytes(qa_native_host *host, size_t capacity,
    const void *bytes, size_t size, qa_native_address *out, qa_error *error)
{
    if (!qa_native_allocate(host->instance, capacity, INT32_MIN + 2, out, error)) return false;
    if (native_host_write(host, *out, bytes, size, error)) return true;
    native_host_temporary_free(host, *out);
    *out = 0;
    return false;
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
    return native_host_temporary_bytes(host, length + 1u, text, length + 1u, out, error);
}

void native_host_temporary_free(qa_native_host *host, qa_native_address address)
{
    qa_error ignored = {0};
    if (host && host->instance && address)
        qa_native_free(host->instance, address, &ignored);
}

static bool string_buffer_write(qa_native_host *host, native_host_string_buffer *buffer,
    const char *text, qa_error *error)
{
    size_t bytes = strlen(text) + 1;
    if (bytes > host->maximum_string_bytes)
        return native_host_fail(error, QA_ERROR_ARGUMENT, bytes, "native string exceeds its source limit");
    if (bytes > buffer->capacity) {
        size_t capacity = buffer->capacity ? buffer->capacity : 64;
        while (capacity < bytes && capacity <= host->maximum_string_bytes / 2) capacity *= 2;
        if (capacity < bytes || capacity > host->maximum_string_bytes) capacity = bytes;
        qa_native_address address = 0;
        if (!native_host_temporary_bytes(host, capacity, text, bytes, &address, error)) return false;
        native_host_temporary_free(host, buffer->address);
        *buffer = (native_host_string_buffer){address, capacity};
        return true;
    }
    return native_host_write(host, buffer->address, text, bytes, error);
}

void native_host_command_dispose(qa_native_host *host)
{
    for (size_t i = 0; i < NATIVE_HOST_COMMAND_TOKENS; ++i) {
        native_host_temporary_free(host, host->command_arguments[i].address);
        host->command_arguments[i] = (native_host_string_buffer){0};
    }
    native_host_temporary_free(host, host->command_tail.address);
    host->command_tail = (native_host_string_buffer){0};
}

bool native_host_command_string(qa_native_host *host, size_t index, size_t count,
    const char *text, qa_native_address *out, qa_error *error)
{
    native_host_string_buffer *buffer = &host->command_tail;
    if (index < count) buffer = &host->command_arguments[index];
    if (!string_buffer_write(host, buffer, text, error)) return false;
    *out = buffer->address;
    return true;
}

static native_host_cvar_record *cvar_record(qa_native_host *host, const char *name)
{
    qa_string_id id = qa_strings_find(host->string_names,
        (qa_bytes){(const uint8_t *)name, strlen(name)});
    qa_id_index_cursor cursor = qa_id_index_begin(&host->cvar_records, id, 0);
    size_t value;
    if (qa_id_index_next(&host->cvar_records, &cursor, &value))
        return (native_host_cvar_record *)(uintptr_t)value;
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

static bool cvar_bytes(qa_native_host *host,
                       native_host_cvar_record *record, const qa_cvar_view *view, native_host_cvar_record *next,
                       uint8_t bytes[56], qa_error *error)
{
    if (!record->name_address) {
        if (!qa_native_host_retain_string(host, record->name_id, &record->name_address, error)) return false;
    }
    bool changed = !record->published_valid || record->modification != view->modification_count;
    if (changed || record->value_text != view->value) {
        if (!string_buffer_write(host, &record->value, view->value, error)) return false;
        record->value_text = view->value;
    }
    if (view->latched_value && (changed || record->latched_text != view->latched_value)) {
        if (!string_buffer_write(host, &record->latched, view->latched_value, error)) return false;
    }
    record->latched_text = view->latched_value;
    qa_native_address name = record->name_address, value = record->value.address;
    qa_native_address latched = view->latched_value ? record->latched.address : 0;
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
    size_t flags_offset = host->classic ? host->classic->cvar.flags : 24;
    size_t next_offset = host->classic ? host->classic->cvar.next : 40;
    qa_native_address published_next = host->pointer_bytes == 4 ?
        qa_load_u32le(record->published + next_offset) : qa_load_u64le(record->published + next_offset);
    if (!restore && record->published_valid && record->modification == view->modification_count &&
        record->value_text == view->value && record->latched_text == view->latched_value &&
        qa_load_u32le(record->published + flags_offset) == view->flags &&
        (!host->classic || qa_load_u32le(record->published + host->classic->cvar.modified) == (uint32_t)view->modified) &&
        published_next == (next ? next->address : 0)) return true;
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
            bool value_changed = record->modification != view->modification_count ||
                memcmp(record->published + offsets[1], bytes + offsets[1], lengths[1]) != 0;
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
    if (restore) {
        size_t offsets[2] = {host->classic ? host->classic->cvar.string : 8,
            host->classic ? host->classic->cvar.latched : 16};
        for (size_t i = 0; i < 2; ++i) {
            uint8_t actual[8];
            if (!native_host_read(host, record->address + offsets[i], actual, host->pointer_bytes, error)) return false;
            if (memcmp(actual, bytes + offsets[i], host->pointer_bytes) &&
                !native_host_write(host, record->address + offsets[i], bytes + offsets[i], host->pointer_bytes, error)) return false;
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
    native_host_cvar_record *first = NULL, *last = NULL;
    size_t additions = 0;
    for (const qa_cvar_view *view = qa_cvars_next(host->cvars, NULL); view;
         view = qa_cvars_next(host->cvars, view)) {
        native_host_cvar_record *record = cvar_record(host, view->name);
        if (!record) {
            record = calloc(1, sizeof(*record));
            if (record) record->name = copy_text(view->name);
            if (!record || !record->name) {
                if (record) free(record);
                discard_unallocated_cvars(first);
                return native_host_fail(error, QA_ERROR_MEMORY, 0, "allocating native cvar shadow");
            }
            if (!qa_strings_intern_cstr(host->string_names, view->name, &record->name_id, error) ||
                !qa_id_index_reserve(&host->cvar_records, ++additions, error)) {
                free(record->name); free(record);
                discard_unallocated_cvars(first); return false;
            }
        }
        if (qa_cvars_read(host->cvars, record->handle) != view)
            record->handle = qa_cvars_resolve(host->cvars, view->name);
        record->order_previous = last; record->order_next = NULL;
        if (last) last->order_next = record; else first = record;
        last = record;
    }
    for (native_host_cvar_record *record = last; record; record = record->order_previous) {
        bool created = !record->address;
        if (created) {
            size_t size = host->classic ? host->classic->cvar.bytes : 56u;
            if (!qa_native_allocate(host->instance, size, INT32_MIN + 3, &record->address, error)) {
                discard_unallocated_cvars(first); return false;
            }
            record->next = host->cvar_shadows; host->cvar_shadows = record;
            qa_id_index_insert(&host->cvar_records, record->name_id, 0, (size_t)(uintptr_t)record);
        }
        const qa_cvar_view *view = qa_cvars_read(host->cvars, record->handle);
        if (!publish_cvar(host, record, view, record->order_next, restore && !created, error)) {
            discard_unallocated_cvars(first); return false;
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
    if ((!view || !view->declared) &&
        !qa_cvars_register(host->cvars, name, value ? value : "", flags, owner, NULL, error))
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
    native_host_cvar_record *record = view ? cvar_record(host, view->name) : NULL;
    if (record && qa_cvars_view_identity(host->cvars) == host->cvar_view_identity &&
        !cvar_guest_modified(host, record, error)) return false;
    if (!refresh_cvars(host, false, error)) return false;
    view = qa_cvars_find(host->cvars, name);
    record = view ? cvar_record(host, view->name) : NULL;
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
    qa_id_index_destroy(&state->strings);
    qa_id_index_destroy(&state->cvar_records);
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
    if (reading && (state->strings.count || state->cvar_shadows))
        return native_host_fail(io->error,QA_ERROR_ARGUMENT,io->offset,"Native memory import requires empty metadata custody");
    size_t count = state->strings.count;
    if (!qa_source_save_count(io, &count, host->strings.capacity)) return false;
    if (reading && !qa_id_index_reserve(&state->strings, host->strings.capacity, io->error)) return false;
    for (size_t i = 0; i < count; ++i) {
        qa_native_address address = reading ? 0 : state->strings.entries[i].value;
        char *text = reading ? NULL : (char *)qa_strings_cstr(host->string_names, state->strings.entries[i].key);
        size_t bytes = 0;
        bool ok = qa_source_save_u64(io, &address) && memory_text(io, &text, &bytes, host->maximum_string_bytes) &&
            memory_allocation(host, address, bytes, INT32_MIN + 1, qualify_addresses, io->error);
        qa_string_id name = QA_STRING_NONE;
        if (ok && reading) ok = qa_strings_intern_cstr(host->string_names, text, &name, io->error);
        if (ok && qualify_addresses) {
            qa_buffer actual = {0};
            ok = qa_native_read_string(host->instance, address, bytes, &actual, io->error);
            if (ok && (actual.size != bytes - 1 || memcmp(actual.data, text, actual.size)))
                ok = native_host_fail(io->error, QA_ERROR_FORMAT, i, "Native string bytes differ from their restored Source RAM");
            qa_buffer_free(&actual);
        }
        if (reading) free(text);
        if (!ok) return false;
        if (reading) qa_id_index_insert(&state->strings, name, 0, address);
    }
    count=0;
    if (!reading) for (native_host_cvar_record *row=state->cvar_shadows;row;row=row->next) ++count;
    if (!qa_source_save_count(io,&count,SIZE_MAX)) return false;
    if (reading && (io->offset>io->input.size || count>(io->input.size-io->offset)/26))
        return native_host_fail(io->error,QA_ERROR_FORMAT,io->offset,"Native cvar-shadow count exceeds its saved objects");
    if (reading && !qa_id_index_reserve(&state->cvar_records, count, io->error)) return false;
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
        if (reading && !qa_strings_intern_cstr(host->string_names, row->name, &row->name_id, io->error)) return false;
        if (qualify_addresses) {
            uint8_t pointer[8];
            if (!qa_native_read(host->instance,row->address,pointer,host->pointer_bytes,io->error)) return false;
            qa_native_address name=host->pointer_bytes==4?qa_load_u32le(pointer):qa_load_u64le(pointer);
            if (reading) {
                row->name_address = name;
                size_t offsets[2] = {host->classic ? host->classic->cvar.string : 8,
                    host->classic ? host->classic->cvar.latched : 16};
                native_host_string_buffer *buffers[2] = {&row->value, &row->latched};
                for (size_t j = 0; j < 2; ++j) {
                    if (!native_host_read(host, row->address + offsets[j], pointer, host->pointer_bytes, io->error)) return false;
                    buffers[j]->address = host->pointer_bytes == 4 ? qa_load_u32le(pointer) : qa_load_u64le(pointer);
                    if (buffers[j]->address) {
                        qa_native_allocation_info allocation;
                        if (!qa_native_allocation_query(host->instance, buffers[j]->address, &allocation, io->error)) return false;
                        if (allocation.tag == INT32_MIN + 2) buffers[j]->capacity = allocation.bytes;
                        else *buffers[j] = (native_host_string_buffer){0};
                    }
                }
            }
            qa_string_id id = qa_strings_find(host->string_names, (qa_bytes){(const uint8_t *)row->name, strlen(row->name)});
            qa_id_index_cursor cursor = qa_id_index_begin(&state->strings, id, 0);
            size_t address;
            if (!qa_id_index_next(&state->strings, &cursor, &address) || address != name)
                return native_host_fail(io->error, QA_ERROR_FORMAT, i,
                    "Native cvar-shadow name lacks its literal Source string object");
        }
        for (native_host_cvar_record *prior=state->cvar_shadows;prior!=row;prior=prior->next)
            if (prior->address==row->address || !strcmp(prior->name,row->name))
                return native_host_fail(io->error,QA_ERROR_FORMAT,i,"Native cvar-shadow custody repeats a Source object");
        if (reading) qa_id_index_insert(&state->cvar_records, row->name_id, 0, (size_t)(uintptr_t)row);
        cvars=&row->next;
    }
    /* Older checkpoints ended after the cvar records. */
    if (reading && io->offset == io->input.size) return true;
    count = 0;
    if (!reading) for (size_t i = 0; i < NATIVE_HOST_COMMAND_TOKENS; ++i)
        if (state->command_arguments[i].address) count = i + 1;
    if (!qa_source_save_count(io, &count, NATIVE_HOST_COMMAND_TOKENS)) return false;
    for (size_t i = 0; i <= count; ++i) {
        native_host_string_buffer *buffer = i == count ? &state->command_tail : &state->command_arguments[i];
        uint64_t capacity = buffer->capacity;
        if (!qa_source_save_u64(io, &buffer->address) || !qa_source_save_u64(io, &capacity)) return false;
        if (capacity > host->maximum_string_bytes)
            return native_host_fail(io->error, QA_ERROR_FORMAT, io->offset, "native command buffer exceeds its source limit");
        buffer->capacity = (size_t)capacity;
        if (buffer->address && !memory_allocation(host, buffer->address, buffer->capacity,
                INT32_MIN + 2, qualify_addresses, io->error)) return false;
    }
    return true;
}

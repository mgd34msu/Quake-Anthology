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

bool native_host_string_read(qa_native_host *host, qa_native_address address, qa_buffer *out,
                             qa_error *error)
{
    if (!out)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0, "native string output is required");
    if (!address) {
        *out = (qa_buffer){0};
        return true;
    }
    return qa_native_read_string(host->instance, address, host->maximum_string_bytes, out,
                                 error);
}

static char *copy_text(const char *text)
{
    size_t size = strlen(text) + 1u;
    char *copy = malloc(size);
    if (copy)
        memcpy(copy, text, size);
    return copy;
}

bool native_host_string_address(qa_native_host *host, const char *text,
                                qa_native_address *out, qa_error *error)
{
    if (!host || !text || !out)
        return native_host_fail(error, QA_ERROR_ARGUMENT, 0,
                                "native host string and output are required");
    size_t length = strlen(text);
    if (length >= host->maximum_string_bytes)
        return native_host_fail(error, QA_ERROR_ARGUMENT, length,
                                "native host string exceeds its source limit");
    for (native_host_string *entry = host->strings; entry; entry = entry->next) {
        if (!strcmp(entry->text, text)) {
            *out = entry->address;
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

static native_host_cvar *find_cvar(qa_native_host *host, const char *name)
{
    for (native_host_cvar *record = host->cvar_shadows; record; record = record->next)
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

static bool write_cvar(qa_native_host *host, native_host_cvar *record,
                       const qa_cvar_view *view, native_host_cvar *next, qa_error *error)
{
    qa_native_address name, value, latched = 0;
    if (!native_host_string_address(host, view->name, &name, error) ||
        !native_host_string_address(host, view->value, &value, error) ||
        (view->latched_value &&
         !native_host_string_address(host, view->latched_value, &latched, error)))
        return false;
    if (host->profile == QA_NATIVE_Q2_GAME_API3) {
        const native_host_classic_layout *layout = host->classic;
        uint8_t bytes[48] = {0};
        if (!native_host_store_pointer(host, bytes, name, error) ||
            !native_host_store_pointer(host, bytes + layout->cvar.string, value, error) ||
            !native_host_store_pointer(host, bytes + layout->cvar.latched, latched, error) ||
            !native_host_store_pointer(host, bytes + layout->cvar.next,
                                       next ? next->address : 0, error))
            return false;
        qa_store_u32le(bytes + layout->cvar.flags, view->flags);
        qa_store_u32le(bytes + layout->cvar.modified, view->modified ? 1u : 0u);
        store_f32(bytes + layout->cvar.value, view->number);
        return native_host_write(host, record->address, bytes, layout->cvar.bytes, error);
    }
    uint8_t bytes[56] = {0};
    qa_store_u64le(bytes, name);
    qa_store_u64le(bytes + 8, value);
    qa_store_u64le(bytes + 16, latched);
    qa_store_u32le(bytes + 24, view->flags);
    qa_store_u32le(bytes + 28, (uint32_t)view->modification_count);
    store_f32(bytes + 32, view->number);
    qa_store_u64le(bytes + 40, next ? next->address : 0);
    qa_store_u32le(bytes + 48, (uint32_t)view->integer);
    return native_host_write(host, record->address, bytes, sizeof(bytes), error);
}

static bool ensure_cvar_shadow(qa_native_host *host, const qa_cvar_view *view,
                               native_host_cvar **out, qa_error *error)
{
    native_host_cvar *record = find_cvar(host, view->name);
    if (record) {
        *out = record;
        return true;
    }
    record = calloc(1, sizeof(*record));
    if (!record)
        return native_host_fail(error, QA_ERROR_MEMORY, 0,
                                "allocating native cvar shadow");
    record->name = copy_text(view->name);
    size_t size = host->classic ? host->classic->cvar.bytes : 56u;
    if (!record->name ||
        !qa_native_allocate(host->instance, size, INT32_MIN + 3, &record->address, error)) {
        free(record->name);
        free(record);
        return false;
    }
    record->next = host->cvar_shadows;
    host->cvar_shadows = record;
    *out = record;
    return true;
}

bool native_host_refresh_cvars(qa_native_host *host, qa_error *error)
{
    if (!host->cvars)
        return native_host_fail(error, QA_ERROR_UNSUPPORTED, 0,
                                "native host has no cvar registry");
    native_host_cvar *previous = NULL;
    size_t count = qa_cvars_count(host->cvars);
    for (size_t index = count; index > 0; --index) {
        const qa_cvar_view *view = qa_cvars_at(host->cvars, index - 1u);
        native_host_cvar *record;
        if (!view || !ensure_cvar_shadow(host, view, &record, error) ||
            !write_cvar(host, record, view, previous, error))
            return false;
        record->modification = view->modification_count;
        previous = record;
    }
    return true;
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
    if (set && !qa_cvars_set(host->cvars, name, value ? value : "", flags != 0, error))
        return false;
    if (!native_host_refresh_cvars(host, error))
        return false;
    view = qa_cvars_find(host->cvars, name);
    native_host_cvar *record = view ? find_cvar(host, view->name) : NULL;
    if (!record)
        return native_host_fail(error, QA_ERROR_NOT_FOUND, 0,
                                "native cvar registration did not publish a record");
    *out = record->address;
    return true;
}

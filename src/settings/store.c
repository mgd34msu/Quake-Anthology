#include "internal.h"
#include <limits.h>

bool qa_settings_read(qa_settings_store store, const char *path, qa_resource **out, bool *found,
                      qa_error *e) {
    if (!out || !found || !store.vfs || !path)
        return settings_fail(e, "Invalid settings read");
    qa_resource *resource;
    qa_error missing = {0};
    if (!qa_vfs_acquire_from(store.vfs, store.mount, path, &resource, &missing)) {
        if (missing.code != QA_ERROR_NOT_FOUND) {
            if (e)
                *e = missing;
            return false;
        }
        *out = NULL;
        *found = false;
        return true;
    }
    *out = resource;
    *found = true;
    return true;
}
bool qa_settings_write(qa_settings_store store, const char *path, qa_bytes bytes, qa_error *e) {
    return qa_vfs_write(store.vfs, store.mount, path, bytes, e);
}
static bool write_owned(qa_settings_store store, const char *path, qa_buffer *bytes, qa_error *e) {
    bool ok = qa_settings_write(store, path, (qa_bytes){bytes->data, bytes->size}, e);
    qa_buffer_free(bytes);
    return ok;
}
bool qa_settings_load_seat(qa_settings_store store, const char *path, qa_seat_settings *out,
                           bool *found, qa_error *e) {
    qa_resource *resource;
    bool present;
    if (!out || !found)
        return settings_fail(e, "Missing seat settings output");
    if (!qa_settings_read(store, path, &resource, &present, e))
        return false;
    bool ok = !present || qa_seat_settings_parse(qa_resource_bytes(resource), out, e);
    qa_resource_release(resource);
    if (ok)
        *found = present;
    return ok;
}
bool qa_settings_save_seat(qa_settings_store store, const char *path,
                           const qa_seat_settings *settings, qa_error *e) {
    qa_buffer bytes;
    return qa_seat_settings_encode(settings, &bytes, e) && write_owned(store, path, &bytes, e);
}
bool qa_settings_load_gyro(qa_settings_store store, const char *path, qa_gyro_profile *out,
                           bool *found, qa_error *e) {
    qa_resource *resource;
    bool present;
    if (!out || !found)
        return settings_fail(e, "Missing gyro settings output");
    if (!qa_settings_read(store, path, &resource, &present, e))
        return false;
    bool ok = !present || qa_gyro_profile_parse(qa_resource_bytes(resource), out, e);
    qa_resource_release(resource);
    if (ok)
        *found = present;
    return ok;
}
bool qa_settings_save_gyro(qa_settings_store store, const char *path,
                           const qa_gyro_profile *profile, qa_error *e) {
    qa_buffer bytes;
    return qa_gyro_profile_encode(profile, &bytes, e) && write_owned(store, path, &bytes, e);
}
bool qa_settings_load_routing(qa_settings_store store, const char *path, int *seat, bool *found,
                              qa_error *e) {
    if (!seat || !found)
        return settings_fail(e, "Missing input routing output");
    qa_resource *resource;
    bool present;
    if (!qa_settings_read(store, path, &resource, &present, e))
        return false;
    if (!present) {
        *found = false;
        return true;
    }
    qa_json_document *d;
    bool ok = qa_json_parse(qa_resource_bytes(resource), &d, e);
    int result = -1;
    if (ok) {
        qa_json_id root = qa_json_root(d), value = qa_json_get(d, root, "keyboardSeat");
        ok = settings_version(d, root, e);
        if (ok && qa_json_type(d, value) != QA_JSON_NULL) {
            uint32_t number;
            ok = settings_u32(d, value, &number, e);
            if (ok && number > INT_MAX)
                ok = settings_fail(e, "Input routing seat exceeds native range");
            if (ok)
                result = (int)number;
        }
        qa_json_destroy(d);
    }
    qa_resource_release(resource);
    if (ok) {
        *seat = result;
        *found = true;
    }
    return ok;
}
bool qa_settings_save_routing(qa_settings_store store, const char *path, int seat, qa_error *e) {
    if (seat < -1)
        return settings_fail(e, "Invalid keyboard routing seat");
    qa_json_writer w = {0};
    qa_json_writer_object(&w);
    settings_key_number(&w, "version", 1);
    qa_json_writer_key(&w, "keyboardSeat");
    if (seat < 0)
        qa_json_writer_null(&w);
    else
        qa_json_writer_number(&w, seat);
    qa_json_writer_end(&w);
    qa_buffer bytes;
    return settings_finish(&w, &bytes, e) && write_owned(store, path, &bytes, e);
}
bool qa_settings_save_config(qa_settings_store store, const char *path, const qa_cvars *vars,
                             const qa_input_seat *seat, bool controllers, qa_error *e) {
    qa_buffer bindings = {0}, cvars = {0};
    if (!qa_cvars_config(vars, &cvars, e))
        return false;
    if (seat && !qa_input_bindings_config(seat, controllers, &bindings, e)) {
        qa_buffer_free(&cvars);
        return false;
    }
    if (bindings.size > SIZE_MAX - cvars.size - 1) {
        qa_buffer_free(&bindings);
        qa_buffer_free(&cvars);
        return settings_fail(e, "Config size overflow");
    }
    qa_buffer bytes = {.size = bindings.size + cvars.size};
    bytes.data = malloc(bytes.size + 1);
    if (!bytes.data) {
        qa_buffer_free(&bindings);
        qa_buffer_free(&cvars);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating config text");
        return false;
    }
    if (bindings.size)
        memcpy(bytes.data, bindings.data, bindings.size);
    if (cvars.size)
        memcpy(bytes.data + bindings.size, cvars.data, cvars.size);
    bytes.data[bytes.size] = 0;
    qa_buffer_free(&bindings);
    qa_buffer_free(&cvars);
    return write_owned(store, path, &bytes, e);
}
bool qa_settings_execute(qa_settings_store store, const char *path, qa_console *console,
                         const qa_command_context *context, qa_error *e) {
    if (!console || !context)
        return settings_fail(e, "Missing config command owner");
    qa_resource *resource;
    bool found;
    if (!qa_settings_read(store, path, &resource, &found, e))
        return false;
    if (!found) {
        qa_error_set(e, QA_ERROR_NOT_FOUND, 0, "Settings script not found: %s", path);
        return false;
    }
    qa_bytes bytes = qa_resource_bytes(resource);
    if (bytes.size > SIZE_MAX - 2 || (bytes.size && memchr(bytes.data, 0, bytes.size))) {
        qa_resource_release(resource);
        return settings_fail(e, "Invalid settings script text");
    }
    char *text = malloc(bytes.size + 2);
    if (!text) {
        qa_resource_release(resource);
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating settings script");
        return false;
    }
    if (bytes.size)
        memcpy(text, bytes.data, bytes.size);
    text[bytes.size] = '\n';
    text[bytes.size + 1] = 0;
    qa_command_context script = *context;
    script.script = path;
    script.direct = false;
    bool ok = qa_console_append(console, &script, text, e);
    free(text);
    qa_resource_release(resource);
    return ok;
}
static bool owner_path(const char *const *owner, size_t count, char **out, qa_error *e) {
    if (!owner || !count || !owner[0] ||
        (strcmp(owner[0], "source") && strcmp(owner[0], "client") && strcmp(owner[0], "input") &&
         strcmp(owner[0], "movement") && strcmp(owner[0], "fallback")))
        return settings_fail(e, "Invalid cvar archive owner");
    size_t length = 12;
    for (size_t i = 0; i < count; ++i) {
        if (!owner[i] || !*owner[i] || !strcmp(owner[i], ".") || !strcmp(owner[i], ".."))
            return settings_fail(e, "Invalid cvar archive owner component");
        size_t n = strlen(owner[i]);
        if (n > (SIZE_MAX - length - 1) / 3)
            return settings_fail(e, "Cvar archive path overflow");
        length += 3 * n + 1;
    }
    char *path = malloc(length);
    if (!path) {
        qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating cvar archive path");
        return false;
    }
    char *p = path;
    memcpy(p, "cvars/", 6);
    p += 6;
    static const char hex[] = "0123456789ABCDEF";
    for (size_t i = 0; i < count; ++i) {
        if (i)
            *p++ = '/';
        for (const unsigned char *c = (const unsigned char *)owner[i]; *c; ++c) {
            if ((*c >= 'A' && *c <= 'Z') || (*c >= 'a' && *c <= 'z') || (*c >= '0' && *c <= '9') ||
                strchr("-_.!~*'()", *c))
                *p++ = (char)*c;
            else {
                *p++ = '%';
                *p++ = hex[*c >> 4];
                *p++ = hex[*c & 15];
            }
        }
    }
    memcpy(p, ".json", 6);
    *out = path;
    return true;
}
static const char *const dialects[] = {"q1-netquake", "q1-quakeworld", "q2-classic", "q2-rerelease",
                                       "q3"};
void qa_cvar_archive_free(qa_cvar_archive *archive) {
    if (!archive)
        return;
    for (size_t i = 0; i < archive->count; ++i) {
        free(archive->entries[i].name);
        free(archive->entries[i].value);
    }
    free(archive->entries);
    *archive = (qa_cvar_archive){0};
}
bool qa_settings_load_cvars(qa_settings_store store, const char *const *owner, size_t count,
                            qa_console_dialect dialect, qa_cvar_archive *out, qa_error *e) {
    if (!out || dialect < QA_CONSOLE_Q1 || dialect > QA_CONSOLE_Q3)
        return settings_fail(e, "Invalid cvar archive dialect");
    char *path;
    if (!owner_path(owner, count, &path, e))
        return false;
    qa_resource *resource;
    bool found;
    bool ok = qa_settings_read(store, path, &resource, &found, e);
    free(path);
    if (!ok)
        return false;
    if (!found) {
        *out = (qa_cvar_archive){0};
        return true;
    }
    qa_json_document *d = NULL;
    qa_cvar_archive result = {0};
    if (!qa_json_parse(qa_resource_bytes(resource), &d, e)) {
        qa_resource_release(resource);
        return false;
    }
    qa_json_id root = qa_json_root(d), entries = qa_json_get(d, root, "entries");
    ok = settings_version(d, root, e);
    if (ok && !qa_json_string_equal(d, qa_json_get(d, root, "dialect"), dialects[dialect]))
        ok = settings_fail(e, "Cvar archive dialect mismatch");
    if (ok && qa_json_type(d, entries) != QA_JSON_ARRAY)
        ok = settings_fail(e, "Expected cvar archive entries");
    size_t total = ok ? qa_json_size(d, entries) : 0;
    if (total > SIZE_MAX / sizeof(*result.entries))
        ok = settings_fail(e, "Cvar archive is too large");
    if (ok && total) {
        result.entries = calloc(total, sizeof(*result.entries));
        if (!result.entries) {
            qa_error_set(e, QA_ERROR_MEMORY, 0, "Allocating cvar archive");
            ok = false;
        }
    }
    for (size_t i = 0; ok && i < total; ++i) {
        qa_json_id entry = qa_json_at(d, entries, i);
        ++result.count;
        ok = settings_object(d, entry, e) &&
             settings_string(d, qa_json_get(d, entry, "name"), &result.entries[i].name, e) &&
             settings_string(d, qa_json_get(d, entry, "value"), &result.entries[i].value, e);
        for (size_t j = 0; ok && j < i; ++j)
            if (!strcmp(result.entries[j].name, result.entries[i].name))
                ok = settings_fail(e, "Duplicate archived cvar");
    }
    qa_json_destroy(d);
    qa_resource_release(resource);
    if (ok)
        *out = result;
    else
        qa_cvar_archive_free(&result);
    return ok;
}
bool qa_settings_save_cvars(qa_settings_store store, const char *const *owner, size_t count,
                            const qa_cvars *vars, qa_error *e) {
    if (!vars)
        return settings_fail(e, "Missing cvar archive registry");
    char *path;
    if (!owner_path(owner, count, &path, e))
        return false;
    qa_json_writer w = {0};
    qa_json_writer_object(&w);
    settings_key_number(&w, "version", 1);
    settings_key_string(&w, "dialect", dialects[qa_cvars_dialect(vars)]);
    qa_json_writer_key(&w, "entries");
    qa_json_writer_array(&w);
    for (const qa_cvar_view *entry = qa_cvars_next(vars, NULL); entry;
         entry = qa_cvars_next(vars, entry)) {
        const char *value = qa_cvars_archive_value(vars, entry);
        if (!value)
            continue;
        qa_json_writer_object(&w);
        settings_key_string(&w, "name", entry->name);
        settings_key_string(&w, "value", value);
        qa_json_writer_end(&w);
    }
    qa_json_writer_end(&w);
    qa_json_writer_end(&w);
    qa_buffer bytes;
    bool ok = settings_finish(&w, &bytes, e) && write_owned(store, path, &bytes, e);
    free(path);
    return ok;
}
bool qa_cvar_archive_apply(qa_cvars *vars, const qa_cvar_archive *archive, qa_error *e) {
    if (!vars || !archive || (archive->count && !archive->entries))
        return settings_fail(e, "Invalid cvar archive application");
    for (size_t i = 0; i < archive->count; ++i)
        if (!qa_cvars_set_flags(vars, archive->entries[i].name, archive->entries[i].value,
                                QA_CVAR_ARCHIVE, e))
            return false;
    return true;
}

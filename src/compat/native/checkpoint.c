#include "internal.h"

#define CHECKPOINT_HEADER_BYTES 100u

static bool checkpoint_kind_matches_profile(const qa_native_checkpoint *checkpoint) {
    if ((unsigned)checkpoint->q3_role > QA_QVM_UI ||
        (checkpoint->profile != QA_NATIVE_Q3_VMMAIN && checkpoint->q3_role != QA_QVM_GAME))
        return false;
    return (checkpoint->kind == QA_NATIVE_CHECKPOINT_OWNED_PROCESS &&
            checkpoint->profile == QA_NATIVE_Q2_CGAME_API2023) ||
           (checkpoint->kind == QA_NATIVE_CHECKPOINT_Q2_CLASSIC &&
            checkpoint->profile == QA_NATIVE_Q2_GAME_API3) ||
           (checkpoint->kind == QA_NATIVE_CHECKPOINT_Q2_RERELEASE &&
            checkpoint->profile == QA_NATIVE_Q2_GAME_API2023) ||
           (checkpoint->kind == QA_NATIVE_CHECKPOINT_HOST_ONLY &&
            (checkpoint->profile == QA_NATIVE_Q3_VMMAIN ||
             checkpoint->profile == QA_NATIVE_QUAKE_LIVE_GAME_API10));
}

static bool checkpoint_ready(qa_native_instance *instance, qa_error *error) {
    if (!instance || instance->lifecycle != QA_NATIVE_INITIALIZED || instance->process_host_pending)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "initialized native instance is required for checkpointing");
    if (instance->active_depth || instance->callback_depth || instance->region_depth ||
        instance->write_depth || instance->region_scopes ||
        instance->write_scope || instance->call_scope || instance->checkpointing ||
        instance->destroying || instance->unloading ||
        !(instance->process_kind == QA_NATIVE_PROCESS_SYSV ?
             qa_native_sysv_process_idle(instance->sysv_process) :
             qa_native_windows_process_idle(instance->windows_process)))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native checkpoint requires an idle instance");
    return true;
}

static bool call_entry(qa_native_instance *instance, const char *name,
                       const qa_native_value *arguments, size_t count, qa_native_value *result,
                       qa_error *error) {
    const native_entry_binding *entry = native_entry(instance, name);
    if (!entry) {
        qa_error_set(error, QA_ERROR_UNSUPPORTED, 0, "native checkpoint entry %s is unavailable",
                     name);
        return false;
    }
    return native_call_binding(instance, entry, arguments, count, result, error);
}

static bool temporary_create(qa_native_instance *instance,
    native_process_temporary **out, qa_error *error) {
    native_process_temporary *temporary = calloc(1, sizeof(*temporary));
    if (!temporary) return native_fail(error, QA_ERROR_MEMORY, 0, "owning native save directory");
    if (!qa_fs_root_temporary_create(&temporary->root, error)) { free(temporary); return false; }
    temporary->next = instance->process_temporaries;
    instance->process_temporaries = temporary;
    *out = temporary;
    return true;
}

static bool temporary_release(qa_native_instance *instance, native_process_temporary *temporary,
    bool okay, qa_error *error) {
    qa_error cleanup = {0};
    bool released = !temporary->resource_root || instance->process_resources.root_remove(
        instance->process_resources.context, temporary->resource_root, &cleanup);
    if (released) {
        temporary->resource_root = 0;
        released = qa_fs_root_temporary_dispose(&temporary->root, &cleanup);
    }
    if (released) {
        native_process_temporary **link = &instance->process_temporaries;
        while (*link != temporary) link = &(*link)->next;
        *link = temporary->next;
        free(temporary);
    }
    if (okay && !released && error) *error = cleanup;
    return okay && released;
}

static bool temporary_file(native_process_temporary *temporary, const char *name,
    qa_bytes bytes, char **out, qa_error *error) {
    qa_fs_stream *stream = NULL;
    if (!qa_fs_root_stream_open(temporary->root, name, QA_FS_STREAM_WRITE, false, &stream, NULL, error))
        return false;
    bool okay = true;
    size_t offset = 0;
    while (okay && offset < bytes.size) {
        size_t written = 0;
        okay = qa_fs_stream_write_some(stream, (qa_bytes){bytes.data + offset, bytes.size - offset},
            offset, &written, error);
        if (okay && !written) okay = native_fail(error, QA_ERROR_IO, offset, "native save file made no write progress");
        offset += written;
    }
    qa_error cleanup = {0};
    bool closed = qa_fs_stream_close_checked(stream, &cleanup);
    if (okay && !closed && error) *error = cleanup;
    return okay && closed && qa_fs_root_join(temporary->root, name, out, error);
}

static bool temporary_read(native_process_temporary *temporary, const char *name,
    qa_buffer *out, qa_error *error) {
    qa_fs_file *file = NULL;
    qa_fs_identity identity;
    if (!qa_fs_root_file_open(temporary->root, name, &file, &identity, error)) return false;
    bool okay = qa_fs_file_read_snapshot(file, &identity, out, error);
    qa_fs_file_close(file);
    return okay;
}

static bool source_string(qa_native_instance *instance, const char *text,
    qa_native_address *out, qa_error *error)
{
    size_t bytes = strlen(text) + 1;
    return qa_native_allocate(instance, bytes, INT32_C(0x4e534156), out, error) &&
        qa_native_write(instance, *out, (qa_bytes){(const uint8_t *)text, bytes}, error);
}

static bool release_source_string(qa_native_instance *instance, qa_native_address address,
    bool okay, qa_error *error)
{
    if (!address) return okay;
    qa_error cleanup = {0};
    bool released = qa_native_free(instance, address, &cleanup);
    if (okay && !released && error) *error = cleanup;
    return okay && released;
}

typedef struct source_file {
    uint64_t handle;
    qa_native_sysv_file capability;
    qa_native_windows_file windows_capability;
    bool opened, registered;
} source_file;

static bool source_file_admit(qa_native_instance *instance, native_process_temporary *temporary,
    const char *path, bool write, source_file *file, qa_error *error)
{
    if (!instance->process_resources.context)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native save file requires its retained resource graph");
    uint32_t mode = QA_FS_OPENED_READ | (write ? QA_FS_OPENED_WRITE : 0u);
    bool okay = true;
    if (!temporary->resource_root) {
        char *directory = NULL;
        if (!qa_fs_root_join(temporary->root, "", &directory, error)) return false;
        size_t length = strlen(directory);
        char *prefix = realloc(directory, length + 2);
        if (!prefix) { free(directory); return native_fail(error, QA_ERROR_MEMORY, 0, "owning native save directory prefix"); }
        prefix[length] = '/'; prefix[length + 1] = 0;
        okay = instance->process_resources.root_add(instance->process_resources.context,
            prefix, temporary->root, mode, &temporary->resource_root, error);
        free(prefix);
        if (!okay) return false;
    }
    bool windows = instance->process_kind == QA_NATIVE_PROCESS_WINDOWS;
    okay = windows ? instance->process_resources.open_windows_file(instance->process_resources.context, path, mode,
        QA_FS_OPEN_EXISTING, &file->windows_capability, &file->opened, error) :
        instance->process_resources.open_sysv_file(instance->process_resources.context, path, mode,
        QA_FS_OPEN_EXISTING, &file->capability, &file->opened, error);
    if (file->opened) file->handle = windows ? file->windows_capability.id : file->capability.handle;
    if (okay && !file->opened)
        okay = native_fail(error, QA_ERROR_NOT_FOUND, 0,
            "actual native save file is absent from its temporary directory");
    if (okay) {
        okay = windows ? qa_native_windows_process_file_add(instance->windows_process, path,
            QA_FS_OPEN_EXISTING, &file->windows_capability, error) :
            qa_native_sysv_process_file_add(instance->sysv_process, &file->capability, error);
        file->registered = okay;
    }
    return okay;
}

static bool source_file_release(qa_native_instance *instance, source_file *file,
    bool okay, qa_error *error)
{
    qa_error cleanup = {0}; bool closed = true;
    bool windows = instance->process_kind == QA_NATIVE_PROCESS_WINDOWS;
    if (file->registered) {
        closed = windows ? qa_native_windows_process_file_close(instance->windows_process, file->handle, &cleanup) :
            qa_native_sysv_process_file_close(instance->sysv_process, file->handle, &cleanup);
        if (closed) closed = windows ? qa_native_windows_process_file_remove(instance->windows_process, file->handle, &cleanup) :
            qa_native_sysv_process_file_remove(instance->sysv_process, file->handle, &cleanup);
    } else if (file->opened) closed = windows ? file->windows_capability.close(file->windows_capability.context, &cleanup) :
        file->capability.close(file->capability.context, &cleanup);
    if (okay && !closed && error) *error = cleanup;
    return okay && closed;
}

static bool capture_classic(qa_native_instance *instance, qa_native_checkpoint_request request,
                            qa_native_checkpoint *checkpoint, qa_error *error) {
    native_process_temporary *temporary = NULL;
    char *game_path = NULL, *level_path = NULL;
    if (!temporary_create(instance, &temporary, error))
        return false;
    bool ok = true;
    if (request.game) {
        ok = temporary_file(temporary, "game.ssv", (qa_bytes){0}, &game_path, error);
        if (ok) {
            source_file file = {0};
            qa_native_address source = 0;
            ok = source_file_admit(instance, temporary, game_path, true, &file, error) &&
                source_string(instance, game_path, &source, error);
            qa_native_value arguments[] = {
                {.type = QA_NATIVE_ADDRESS, .as.address = source},
                {.type = QA_NATIVE_I32, .as.i32 = request.autosave ? 1 : 0}};
            ok = ok && call_entry(instance, "WriteGame", arguments, 2, NULL, error) &&
                 temporary_read(temporary, "game.ssv", &checkpoint->game, error);
            ok = release_source_string(instance, source, ok, error);
            ok = source_file_release(instance, &file, ok, error);
        }
        checkpoint->has_game = ok;
    }
    if (ok && request.level) {
        ok = temporary_file(temporary, "level.sav", (qa_bytes){0}, &level_path, error);
        if (ok) {
            source_file file = {0};
            qa_native_address source = 0;
            ok = source_file_admit(instance, temporary, level_path, true, &file, error) &&
                source_string(instance, level_path, &source, error);
            qa_native_value argument = {.type = QA_NATIVE_ADDRESS,
                                        .as.address = source};
            ok = ok && call_entry(instance, "WriteLevel", &argument, 1, NULL, error) &&
                 temporary_read(temporary, "level.sav", &checkpoint->level, error);
            ok = release_source_string(instance, source, ok, error);
            ok = source_file_release(instance, &file, ok, error);
        }
        checkpoint->has_level = ok;
    }
    free(game_path);
    free(level_path);
    return temporary_release(instance, temporary, ok, error);
}

static bool capture_json_entry(qa_native_instance *instance, const char *entry_name, bool flag,
                               qa_buffer *out, qa_error *error) {
    uint64_t length = 0;
    qa_native_address source_length = 0;
    if (!qa_native_allocate(instance, sizeof(length), INT32_C(0x4e534156), &source_length, error)) return false;
    qa_native_value arguments[] = {
        {.type = QA_NATIVE_U8, .as.u8 = flag ? 1u : 0u},
        {.type = QA_NATIVE_ADDRESS, .as.address = source_length}};
    qa_native_value result = {0};
    bool called = call_entry(instance, entry_name, arguments, 2, &result, error);
    if (called) called = qa_native_read(instance, source_length, &length, sizeof(length), error);
    called = release_source_string(instance, source_length, called, error);
    if (!called)
        return false;
#if SIZE_MAX < UINT64_MAX
    if (length > (uint64_t)SIZE_MAX)
        return native_fail(error, QA_ERROR_FORMAT, 0,
                           "native rerelease checkpoint exceeds the host");
#endif
    if (!result.as.address && length)
        return native_fail(error, QA_ERROR_FORMAT, 0,
                           "native rerelease checkpoint returned invalid JSON");
    qa_buffer copy = {0};
    if (length) {
        copy.data = malloc((size_t)length);
        if (!copy.data)
            return native_fail(error, QA_ERROR_MEMORY, 0,
                               "allocating native rerelease checkpoint JSON");
        copy.size = (size_t)length;
        if (!qa_native_read(instance, result.as.address, copy.data, copy.size, error)) {
            free(copy.data);
            copy = (qa_buffer){0};
        }
    }
    qa_error free_error = {0};
    bool freed = qa_native_free(instance, result.as.address, &free_error);
    if (!copy.data && length)
        return false;
    if (!freed) {
        qa_buffer_free(&copy);
        if (error)
            *error = free_error;
        return false;
    }
    *out = copy;
    return true;
}

static bool capture_rerelease(qa_native_instance *instance, qa_native_checkpoint_request request,
                              qa_native_checkpoint *checkpoint, qa_error *error) {
    if (request.game) {
        if (!capture_json_entry(instance, "WriteGameJson", request.autosave, &checkpoint->game,
                                error))
            return false;
        checkpoint->has_game = true;
    }
    if (request.level) {
        if (!capture_json_entry(instance, "WriteLevelJson", request.transition, &checkpoint->level,
                                error))
            return false;
        checkpoint->has_level = true;
    }
    return true;
}

bool qa_native_checkpoint_capture(qa_native_instance *instance,
                                  qa_native_checkpoint_request request, qa_native_checkpoint *out,
                                  qa_error *error) {
    if (!out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native checkpoint output is required");
    if (!checkpoint_ready(instance, error))
        return false;
    qa_native_profile profile = instance->module->info.profile;
    if ((profile == QA_NATIVE_Q3_VMMAIN || profile == QA_NATIVE_QUAKE_LIVE_GAME_API10) &&
        (!instance->options.checkpoint || !instance->options.restore))
        return native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                           "native Q3 continuation requires a complete profile "
                           "checkpoint binding");
    qa_native_checkpoint checkpoint = {
        .kind = profile == QA_NATIVE_Q2_GAME_API3      ? QA_NATIVE_CHECKPOINT_Q2_CLASSIC
                : profile == QA_NATIVE_Q2_GAME_API2023 ? QA_NATIVE_CHECKPOINT_Q2_RERELEASE
                : profile == QA_NATIVE_Q2_CGAME_API2023 ? QA_NATIVE_CHECKPOINT_OWNED_PROCESS
                                                       : QA_NATIVE_CHECKPOINT_HOST_ONLY,
        .profile = profile,
        .q3_role = instance->options.q3_role,
        .image = instance->module->info.image,
        .has_declaration = instance->has_declaration,
        .autosave = request.autosave,
        .transition = request.transition};
    if (!native_copy_bytes((qa_bytes){(const uint8_t *)instance->module->source,
        strlen(instance->module->source)}, &checkpoint.source, error)) return false;
    instance->checkpointing = true;
    bool ok;
    if (profile == QA_NATIVE_Q2_GAME_API3)
        ok = capture_classic(instance, request, &checkpoint, error);
    else if (profile == QA_NATIVE_Q2_GAME_API2023)
        ok = capture_rerelease(instance, request, &checkpoint, error);
    else
        ok = true;
    if (ok && request.host && instance->options.checkpoint) {
        ok = instance->options.checkpoint(instance->options.context, &checkpoint.host, error);
        checkpoint.has_host = ok;
    }
    if (ok && profile != QA_NATIVE_Q2_GAME_API3 && profile != QA_NATIVE_Q2_GAME_API2023) {
        ok = checkpoint.has_host && instance->options.restore &&
            native_process_checkpoint_host(instance, (qa_bytes){checkpoint.host.data, checkpoint.host.size},
                &checkpoint.process, error);
        checkpoint.has_process = ok;
    }
    instance->checkpointing = false;
    if (!ok) {
        qa_native_checkpoint_free(&checkpoint);
        return false;
    }
    *out = checkpoint;
    return true;
}

static bool same_identity(const qa_native_instance *instance,
                          const qa_native_checkpoint *checkpoint, qa_error *error) {
    const qa_native_image_info *image = &instance->module->info.image;
    if (!checkpoint_kind_matches_profile(checkpoint) ||
        checkpoint->profile != instance->module->info.profile ||
        checkpoint->q3_role != instance->options.q3_role ||
        checkpoint->image.format != image->format ||
        checkpoint->image.target.os != image->target.os ||
        checkpoint->image.target.arch != image->target.arch ||
        checkpoint->image.target.abi != image->target.abi ||
        checkpoint->image.target.pointer_bytes != image->target.pointer_bytes ||
        checkpoint->image.preferred_base != image->preferred_base ||
        checkpoint->image.image_bytes != image->image_bytes ||
        checkpoint->source.size != strlen(instance->module->source) ||
        (checkpoint->source.size && (!checkpoint->source.data ||
         memcmp(checkpoint->source.data, instance->module->source, checkpoint->source.size))) ||
        checkpoint->has_declaration != instance->has_declaration)
        return native_fail(error, QA_ERROR_FORMAT, 0,
                           "native checkpoint identity does not match the loaded module");
    return true;
}

static bool restore_classic(qa_native_instance *instance, qa_bytes bytes, const char *file_name,
                            const char *entry_name, qa_error *error) {
    native_process_temporary *temporary = NULL;
    char *path = NULL;
    if (!temporary_create(instance, &temporary, error))
        return false;
    bool ok = temporary_file(temporary, file_name, bytes, &path, error);
    if (ok) {
        source_file file = {0};
        qa_native_address source = 0;
        ok = source_file_admit(instance, temporary, path, false, &file, error) &&
            source_string(instance, path, &source, error);
        qa_native_value argument = {.type = QA_NATIVE_ADDRESS,
                                    .as.address = source};
        ok = ok && call_entry(instance, entry_name, &argument, 1, NULL, error);
        ok = release_source_string(instance, source, ok, error);
        ok = source_file_release(instance, &file, ok, error);
    }
    free(path);
    return temporary_release(instance, temporary, ok, error);
}

static bool restore_json(qa_native_instance *instance, qa_bytes bytes, const char *entry_name,
                         qa_error *error) {
    if (bytes.size == SIZE_MAX)
        return native_fail(error, QA_ERROR_MEMORY, 0, "native JSON checkpoint length overflow");
    qa_native_address address;
    if (!qa_native_allocate(instance, bytes.size + 1u, INT32_MIN, &address, error))
        return false;
    bool ok = qa_native_write(instance, address, bytes, error);
    uint8_t terminator = 0;
    if (ok)
        ok = qa_native_write(instance, address + bytes.size, (qa_bytes){&terminator, 1}, error);
    if (ok) {
        qa_native_value argument = {.type = QA_NATIVE_ADDRESS, .as.address = address};
        ok = call_entry(instance, entry_name, &argument, 1, NULL, error);
    }
    qa_error free_error = {0};
    if (!qa_native_free(instance, address, &free_error) && ok) {
        if (error)
            *error = free_error;
        ok = false;
    }
    return ok;
}

bool qa_native_checkpoint_restore(qa_native_instance *instance,
                                  const qa_native_checkpoint *checkpoint,
                                  qa_native_restore_part part, qa_error *error) {
    if (!checkpoint || part > QA_NATIVE_RESTORE_HOST)
        return native_fail(error, QA_ERROR_ARGUMENT, part,
                           "native checkpoint and restore part are required");
    if (!checkpoint_ready(instance, error) || !same_identity(instance, checkpoint, error))
        return false;
    if (instance->entry_observers || instance->write_observers)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native restore requires detached source entry and write observers");
    for (size_t i = 0; i < instance->region_count; ++i)
        if (instance->regions[i].first)
            return native_fail(error, QA_ERROR_ARGUMENT, i,
                               "native restore requires detached source region observers");
    bool present = part == QA_NATIVE_RESTORE_GAME    ? checkpoint->has_game
                   : part == QA_NATIVE_RESTORE_LEVEL ? checkpoint->has_level
                                                     : checkpoint->has_host;
    if (!present)
        return native_fail(error, QA_ERROR_NOT_FOUND, part,
                           "native checkpoint does not contain the requested part");
    native_entity_changed(instance, QA_NATIVE_ENTITIES_INVALIDATE, UINT32_MAX);
    instance->checkpointing = true;
    bool ok = false;
    if (part == QA_NATIVE_RESTORE_HOST) {
        if (!instance->options.restore)
            ok = native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                             "native host checkpoint restore is unbound");
        else
            ok = instance->options.restore(instance->options.context,
                                           (qa_bytes){checkpoint->host.data, checkpoint->host.size},
                                           error);
    } else if (checkpoint->kind == QA_NATIVE_CHECKPOINT_Q2_CLASSIC) {
        const qa_buffer *buffer =
            part == QA_NATIVE_RESTORE_GAME ? &checkpoint->game : &checkpoint->level;
        ok = restore_classic(instance, (qa_bytes){buffer->data, buffer->size},
                             part == QA_NATIVE_RESTORE_GAME ? "game.ssv" : "level.sav",
                             part == QA_NATIVE_RESTORE_GAME ? "ReadGame" : "ReadLevel", error);
    } else if (checkpoint->kind == QA_NATIVE_CHECKPOINT_Q2_RERELEASE) {
        const qa_buffer *buffer =
            part == QA_NATIVE_RESTORE_GAME ? &checkpoint->game : &checkpoint->level;
        ok = restore_json(instance, (qa_bytes){buffer->data, buffer->size},
                          part == QA_NATIVE_RESTORE_GAME ? "ReadGameJson" : "ReadLevelJson", error);
    } else {
        ok = native_fail(error, QA_ERROR_UNSUPPORTED, 0,
                         "host-only native checkpoint has no guest save part");
    }
    instance->checkpointing = false;
    if (ok && instance->entity_views_retired) {
        qa_native_entity_table table;
        qa_error admission = {0};
        if (qa_native_entity_table_refresh(instance, &table, &admission) && instance->entity_views_retired)
            native_entity_changed(instance, QA_NATIVE_ENTITIES_TABLE, UINT32_MAX);
    }
    return ok;
}

void qa_native_checkpoint_free(qa_native_checkpoint *checkpoint) {
    if (!checkpoint)
        return;
    qa_buffer_free(&checkpoint->source);
    qa_buffer_free(&checkpoint->game);
    qa_buffer_free(&checkpoint->level);
    qa_buffer_free(&checkpoint->host);
    qa_buffer_free(&checkpoint->process);
    memset(checkpoint, 0, sizeof(*checkpoint));
}

static void store_u32(uint8_t **cursor, uint32_t value) {
    qa_store_u32le(*cursor, value);
    *cursor += 4;
}

static void store_u64(uint8_t **cursor, uint64_t value) {
    qa_store_u64le(*cursor, value);
    *cursor += 8;
}

bool qa_native_checkpoint_encode(const qa_native_checkpoint *checkpoint, qa_buffer *out,
                                 qa_error *error) {
    if (!checkpoint || !out)
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native checkpoint and encoded output are required");
    if ((checkpoint->source.size && !checkpoint->source.data) ||
        (checkpoint->source.size && memchr(checkpoint->source.data, 0, checkpoint->source.size)) ||
        (checkpoint->game.size && !checkpoint->game.data) ||
        (checkpoint->level.size && !checkpoint->level.data) ||
        (checkpoint->host.size && !checkpoint->host.data) ||
        (checkpoint->process.size && !checkpoint->process.data))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native checkpoint part has no owned bytes");
    if ((!checkpoint->has_game && checkpoint->game.size) ||
        (!checkpoint->has_level && checkpoint->level.size) ||
        (!checkpoint->has_host && checkpoint->host.size) ||
        (!checkpoint->has_process && checkpoint->process.size) ||
        (checkpoint->has_process && (!checkpoint->process.size || !checkpoint->has_host)) ||
        (checkpoint->kind == QA_NATIVE_CHECKPOINT_OWNED_PROCESS && !checkpoint->has_process))
        return native_fail(error, QA_ERROR_ARGUMENT, 0,
                           "native checkpoint has bytes for an absent part");
    if (!checkpoint_kind_matches_profile(checkpoint) ||
        checkpoint->image.format > QA_NATIVE_IMAGE_ELF64 ||
        checkpoint->image.target.os > QA_NATIVE_OS_MACOS ||
        checkpoint->image.target.arch > QA_NATIVE_ARCH_AARCH64 ||
        checkpoint->image.target.abi > QA_NATIVE_ABI_AAPCS64 ||
        (checkpoint->image.target.pointer_bytes != 4 &&
         checkpoint->image.target.pointer_bytes != 8))
        return native_fail(error, QA_ERROR_ARGUMENT, 0, "native checkpoint metadata is invalid");
    size_t size = CHECKPOINT_HEADER_BYTES;
    if (!native_size_add(size, checkpoint->source.size, &size) ||
        !native_size_add(size, checkpoint->game.size, &size) ||
        !native_size_add(size, checkpoint->level.size, &size) ||
        !native_size_add(size, checkpoint->host.size, &size) ||
        !native_size_add(size, checkpoint->process.size, &size))
        return native_fail(error, QA_ERROR_MEMORY, 0, "native checkpoint encoding size overflow");
    uint8_t *data = malloc(size);
    if (!data)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating native checkpoint encoding");
    uint8_t *cursor = data;
    memcpy(cursor, "QANCP\0\0\0", 8);
    cursor += 8;
    store_u32(&cursor, (uint32_t)checkpoint->kind);
    store_u32(&cursor, (uint32_t)checkpoint->profile);
    store_u32(&cursor, (uint32_t)checkpoint->q3_role);
    store_u32(&cursor, (uint32_t)checkpoint->image.format);
    store_u32(&cursor, (uint32_t)checkpoint->image.target.os);
    store_u32(&cursor, (uint32_t)checkpoint->image.target.arch);
    store_u32(&cursor, (uint32_t)checkpoint->image.target.abi);
    store_u32(&cursor, checkpoint->image.target.pointer_bytes);
    store_u64(&cursor, checkpoint->image.preferred_base);
    store_u64(&cursor, checkpoint->image.image_bytes);
    uint32_t flags = (checkpoint->has_declaration ? 1u : 0u) | (checkpoint->autosave ? 2u : 0u) |
                     (checkpoint->transition ? 4u : 0u) | (checkpoint->has_game ? 8u : 0u) |
                     (checkpoint->has_level ? 16u : 0u) | (checkpoint->has_host ? 32u : 0u) |
                     (checkpoint->has_process ? 64u : 0u);
    store_u32(&cursor, flags);
    store_u64(&cursor, checkpoint->source.size);
    store_u64(&cursor, checkpoint->game.size);
    store_u64(&cursor, checkpoint->level.size);
    store_u64(&cursor, checkpoint->host.size);
    store_u64(&cursor, checkpoint->process.size);
    if (checkpoint->source.size) {
        memcpy(cursor, checkpoint->source.data, checkpoint->source.size);
        cursor += checkpoint->source.size;
    }
    if (checkpoint->game.size) {
        memcpy(cursor, checkpoint->game.data, checkpoint->game.size);
        cursor += checkpoint->game.size;
    }
    if (checkpoint->level.size) {
        memcpy(cursor, checkpoint->level.data, checkpoint->level.size);
        cursor += checkpoint->level.size;
    }
    if (checkpoint->host.size) {
        memcpy(cursor, checkpoint->host.data, checkpoint->host.size);
        cursor += checkpoint->host.size;
    }
    if (checkpoint->process.size) memcpy(cursor, checkpoint->process.data, checkpoint->process.size);
    *out = (qa_buffer){data, size};
    return true;
}

static uint32_t load_u32(const uint8_t **cursor) {
    uint32_t value = qa_load_u32le(*cursor);
    *cursor += 4;
    return value;
}

static uint64_t load_u64(const uint8_t **cursor) {
    uint64_t value = qa_load_u64le(*cursor);
    *cursor += 8;
    return value;
}

static bool decode_buffer(const uint8_t **cursor, size_t size, qa_buffer *out, qa_error *error) {
    if (!size)
        return true;
    out->data = malloc(size);
    if (!out->data)
        return native_fail(error, QA_ERROR_MEMORY, 0, "allocating decoded native checkpoint part");
    memcpy(out->data, *cursor, size);
    out->size = size;
    *cursor += size;
    return true;
}

bool qa_native_checkpoint_decode(qa_bytes encoded, qa_native_checkpoint *out, qa_error *error) {
    if (!out || (!encoded.data && encoded.size) || encoded.size < CHECKPOINT_HEADER_BYTES)
        return native_fail(error, QA_ERROR_FORMAT, encoded.size,
                           "native checkpoint header is truncated");
    if (memcmp(encoded.data, "QANCP\0\0\0", 8))
        return native_fail(error, QA_ERROR_FORMAT, 0, "native checkpoint magic is invalid");
    const uint8_t *cursor = encoded.data + 8;
    qa_native_checkpoint checkpoint = {0};
    checkpoint.kind = (qa_native_checkpoint_kind)load_u32(&cursor);
    checkpoint.profile = (qa_native_profile)load_u32(&cursor);
    checkpoint.q3_role = (qa_qvm_role)load_u32(&cursor);
    checkpoint.image.format = (qa_native_image_format)load_u32(&cursor);
    checkpoint.image.target.os = (qa_native_os)load_u32(&cursor);
    checkpoint.image.target.arch = (qa_native_arch)load_u32(&cursor);
    checkpoint.image.target.abi = (qa_native_abi)load_u32(&cursor);
    uint32_t pointer_bytes = load_u32(&cursor);
    if (pointer_bytes != 4 && pointer_bytes != 8)
        return native_fail(error, QA_ERROR_FORMAT, 36,
                           "native checkpoint pointer width is invalid");
    checkpoint.image.target.pointer_bytes = (uint8_t)pointer_bytes;
    checkpoint.image.preferred_base = load_u64(&cursor);
    checkpoint.image.image_bytes = load_u64(&cursor);
    uint32_t flags = load_u32(&cursor);
    uint64_t source_size = load_u64(&cursor);
    uint64_t game_size = load_u64(&cursor);
    uint64_t level_size = load_u64(&cursor);
    uint64_t host_size = load_u64(&cursor);
    uint64_t process_size = load_u64(&cursor);
    uint64_t payload = source_size;
    if (UINT64_MAX - payload < game_size)
        return native_fail(error, QA_ERROR_FORMAT, CHECKPOINT_HEADER_BYTES,
                           "native checkpoint source length overflows");
    payload += game_size;
    if (UINT64_MAX - payload < level_size) {
        return native_fail(error, QA_ERROR_FORMAT, CHECKPOINT_HEADER_BYTES,
                           "native checkpoint payload length overflows");
    }
    payload += level_size;
    if (UINT64_MAX - payload < host_size)
        return native_fail(error, QA_ERROR_FORMAT, CHECKPOINT_HEADER_BYTES,
                           "native checkpoint payload length overflows");
    payload += host_size;
    if (UINT64_MAX - payload < process_size)
        return native_fail(error, QA_ERROR_FORMAT, CHECKPOINT_HEADER_BYTES,
                           "native process continuation length overflows");
    payload += process_size;
#if SIZE_MAX < UINT64_MAX
    if (payload > (uint64_t)SIZE_MAX)
        return native_fail(error, QA_ERROR_FORMAT, CHECKPOINT_HEADER_BYTES,
                           "native checkpoint payload exceeds the host");
#endif
    if ((size_t)payload != encoded.size - CHECKPOINT_HEADER_BYTES)
        return native_fail(error, QA_ERROR_FORMAT, CHECKPOINT_HEADER_BYTES,
                           "native checkpoint payload length is invalid");
    if (checkpoint.kind > QA_NATIVE_CHECKPOINT_OWNED_PROCESS ||
        checkpoint.profile > QA_NATIVE_QUAKE_LIVE_GAME_API10 ||
        checkpoint.image.format > QA_NATIVE_IMAGE_ELF64 ||
        checkpoint.image.target.os > QA_NATIVE_OS_MACOS ||
        checkpoint.image.target.arch > QA_NATIVE_ARCH_AARCH64 ||
        checkpoint.image.target.abi > QA_NATIVE_ABI_AAPCS64 || (flags & ~127u))
        return native_fail(error, QA_ERROR_FORMAT, 8, "native checkpoint metadata is invalid");
    if (!checkpoint_kind_matches_profile(&checkpoint))
        return native_fail(error, QA_ERROR_FORMAT, 8,
                           "native checkpoint kind does not match its profile");
    checkpoint.has_declaration = (flags & 1u) != 0;
    checkpoint.autosave = (flags & 2u) != 0;
    checkpoint.transition = (flags & 4u) != 0;
    checkpoint.has_game = (flags & 8u) != 0;
    checkpoint.has_level = (flags & 16u) != 0;
    checkpoint.has_host = (flags & 32u) != 0;
    checkpoint.has_process = (flags & 64u) != 0;
    if ((!checkpoint.has_game && game_size) || (!checkpoint.has_level && level_size) ||
        (!checkpoint.has_host && host_size) || (!checkpoint.has_process && process_size) ||
        (checkpoint.has_process && (!process_size || !checkpoint.has_host)) ||
        (checkpoint.kind == QA_NATIVE_CHECKPOINT_OWNED_PROCESS && !checkpoint.has_process))
        return native_fail(error, QA_ERROR_FORMAT, 60,
                           "native checkpoint contains an undeclared part");
    if (source_size && memchr(cursor, 0, (size_t)source_size))
        return native_fail(error, QA_ERROR_FORMAT, CHECKPOINT_HEADER_BYTES,
                           "native checkpoint source name contains an embedded terminator");
    if (!decode_buffer(&cursor, (size_t)source_size, &checkpoint.source, error) ||
        !decode_buffer(&cursor, (size_t)game_size, &checkpoint.game, error) ||
        !decode_buffer(&cursor, (size_t)level_size, &checkpoint.level, error) ||
        !decode_buffer(&cursor, (size_t)host_size, &checkpoint.host, error) ||
        !decode_buffer(&cursor, (size_t)process_size, &checkpoint.process, error)) {
        qa_native_checkpoint_free(&checkpoint);
        return false;
    }
    *out = checkpoint;
    return true;
}

#undef CHECKPOINT_HEADER_BYTES

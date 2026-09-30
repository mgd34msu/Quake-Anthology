#include "internal.h"

typedef struct checkpoint_writer { qa_buffer bytes; size_t capacity; } checkpoint_writer;
typedef struct checkpoint_reader { qa_bytes bytes; size_t offset; } checkpoint_reader;

static void store_vector(uint8_t *out, qa_vec3 value)
{
    float values[3] = {value.x, value.y, value.z};
    for (size_t i = 0; i < 3; ++i) {
        uint32_t bits;
        memcpy(&bits, values + i, sizeof(bits));
        qa_store_u32le(out + i * 4, bits);
    }
}

static qa_vec3 load_vector(const uint8_t *in)
{
    return qa_v3(qa_load_f32le(in), qa_load_f32le(in + 4), qa_load_f32le(in + 8));
}

static bool append(checkpoint_writer *writer, const void *data, size_t size, qa_error *error)
{
    if (size > SIZE_MAX - writer->bytes.size)
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 host checkpoint length overflow");
    size_t needed = writer->bytes.size + size;
    if (needed > writer->capacity) {
        size_t capacity = writer->capacity ? writer->capacity : 1024;
        while (capacity < needed) {
            if (capacity > SIZE_MAX / 2) { capacity = needed; break; }
            capacity *= 2;
        }
        void *grown = realloc(writer->bytes.data, capacity);
        if (!grown) return q3_fail(error, QA_ERROR_MEMORY, 0, "growing Q3 host checkpoint");
        writer->bytes.data = grown; writer->capacity = capacity;
    }
    if (size) memcpy(writer->bytes.data + writer->bytes.size, data, size);
    writer->bytes.size = needed; return true;
}

static bool take(checkpoint_reader *reader, size_t size, qa_bytes *out, qa_error *error)
{
    if (size > reader->bytes.size - reader->offset)
        return q3_fail(error, QA_ERROR_FORMAT, reader->offset, "truncated Q3 host checkpoint");
    *out = (qa_bytes){reader->bytes.data + reader->offset, size};
    reader->offset += size; return true;
}

static bool idle(qa_q3_host *host, qa_error *error)
{
    if (!host || host->retired || host->restore_pending || host->calls)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 checkpoint requires an idle live host");
    for (size_t i = 1; i < 64; ++i)
        if (host->script_pending[i] || (host->scripts[i] && host->scripts[i]->operations))
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "Q3 script checkpoint operation is active");
    return true;
}

static bool save_file(checkpoint_writer *writer, size_t slot, const q3_file *file, qa_error *error)
{
    qa_bytes bytes = {0};
    const char *path;
    qa_vfs_file_state state = {0};
    qa_sha256_digest digest = {0};
    if (file->kind == Q3_FILE_READ) {
        bytes = file->resource ? qa_resource_bytes(file->resource) :
                                  (qa_bytes){file->restored_bytes.data, file->restored_bytes.size};
        path = file->resource ? qa_resource_path(file->resource) : file->restored_path;
        digest = file->resource ? *qa_resource_digest(file->resource) : file->restored_digest;
        state.position = file->position;
    } else {
        state = qa_vfs_file_capture(file->writable); path = state.path;
    }
    if (!path || strlen(path) >= UINT32_MAX)
        return q3_fail(error, QA_ERROR_FORMAT, slot, "invalid Q3 checkpoint file path");
    size_t length = strlen(path) + 1;
    uint8_t row[72] = {0};
    qa_store_u32le(row, (uint32_t)slot); qa_store_u32le(row + 4, (uint32_t)file->kind);
    qa_store_u32le(row + 8, file->zip); qa_store_u32le(row + 12, state.mode);
    qa_store_u64le(row + 16, state.position); qa_store_u64le(row + 24, bytes.size);
    qa_store_u32le(row + 32, (uint32_t)length); memcpy(row + 40, digest.bytes, sizeof(digest.bytes));
    return append(writer, row, sizeof(row), error) && append(writer, path, length, error) &&
           append(writer, bytes.data, bytes.size, error);
}

bool qa_q3_host_checkpoint(qa_q3_host *host, qa_buffer *out, qa_error *error)
{
    if (!out || !idle(host, error)) return false;
    if (host->native)
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Native Q3 continuation requires an executor memory checkpoint");
    qa_buffer game = {0};
    if (!q3_game_checkpoint_capture(host, &game, error)) return false;
    uint32_t files = 0, scripts = 0;
    for (size_t i = 1; i < 64; ++i) {
        files += host->files[i].kind != Q3_FILE_CLOSED; scripts += host->scripts[i] != NULL;
    }
    checkpoint_writer writer = {0};
    uint8_t header[32] = {'Q','3','H','C'};
    qa_store_u32le(header + 4, 3); qa_store_u32le(header + 8, host->options.role);
    qa_store_u32le(header + 12, host->options.abi); qa_store_u32le(header + 16, files);
    qa_store_u32le(header + 20, scripts);
    qa_store_u64le(header + 24, game.size);
    ++host->calls;
    bool ok = append(&writer, header, sizeof(header), error);
    uint8_t clipping[48];
    store_vector(clipping, host->clip_bounds.mins); store_vector(clipping + 12, host->clip_bounds.maxs);
    store_vector(clipping + 24, host->clip_brush.mins); store_vector(clipping + 36, host->clip_brush.maxs);
    if (ok) ok = append(&writer, clipping, sizeof(clipping), error);
    qa_common_parser_state parser = qa_common_parser_capture(&host->entity_parser);
    qa_common_cursor_state cursor = qa_common_cursor_capture(&host->entity_cursor);
    uint8_t entity[32];
    qa_store_u64le(entity, host->entity_cursor.source.size); qa_store_u64le(entity + 8, cursor.offset);
    qa_store_u32le(entity + 16, (uint32_t)parser.token.size); qa_store_u32le(entity + 20, (uint32_t)parser.name.size);
    qa_store_u32le(entity + 24, (uint32_t)parser.line); qa_store_u32le(entity + 28, cursor.ended);
    if (ok) ok = append(&writer, entity, sizeof(entity), error) &&
        append(&writer, host->entity_cursor.source.data, host->entity_cursor.source.size, error) &&
        append(&writer, parser.token.data, parser.token.size, error) && append(&writer, parser.name.data, parser.name.size, error) &&
        append(&writer, game.data, game.size, error);
    qa_buffer_free(&game);
    for (size_t i = 1; ok && i < 64; ++i)
        if (host->files[i].kind != Q3_FILE_CLOSED) ok = save_file(&writer, i, &host->files[i], error);
    for (size_t i = 1; ok && i < 64; ++i) {
        if (!host->scripts[i]) continue;
        qa_script_checkpoint state = {0}; qa_buffer encoded = {0};
        ok = qa_script_capture(host->scripts[i]->reader, &state, error) &&
             qa_script_checkpoint_encode(&state, &encoded, error);
        if (ok) {
            uint8_t row[16] = {0};
            qa_store_u32le(row, (uint32_t)i); qa_store_u64le(row + 8, encoded.size);
            ok = append(&writer, row, sizeof(row), error) &&
                 append(&writer, encoded.data, encoded.size, error);
        }
        qa_script_checkpoint_free(&state); qa_buffer_free(&encoded);
    }
    --host->calls;
    if (!ok) { qa_buffer_free(&writer.bytes); return false; }
    *out = writer.bytes; return true;
}

static bool restore_file(qa_q3_host *host, checkpoint_reader *reader, q3_file staged[64],
                           uint64_t serial, qa_error *error)
{
    qa_bytes row, path, bytes;
    if (!take(reader, 72, &row, error)) return false;
    uint32_t slot = qa_load_u32le(row.data), kind = qa_load_u32le(row.data + 4);
    uint32_t zip = qa_load_u32le(row.data + 8), mode = qa_load_u32le(row.data + 12);
    uint64_t position = qa_load_u64le(row.data + 16), length = qa_load_u64le(row.data + 24);
    uint32_t path_length = qa_load_u32le(row.data + 32);
    if (!slot || slot >= 64 || staged[slot].kind != Q3_FILE_CLOSED ||
        (kind != Q3_FILE_READ && kind != Q3_FILE_WRITE) || zip > 1 || !path_length ||
        length > SIZE_MAX || qa_load_u32le(row.data + 36) ||
        (kind == Q3_FILE_READ ? mode != 0 || length > INT32_MAX : zip || length || mode > QA_VFS_APPEND_SYNC))
        return q3_fail(error, QA_ERROR_FORMAT, reader->offset, "invalid Q3 checkpoint file record");
    if (!take(reader, path_length, &path, error) || !take(reader, (size_t)length, &bytes, error)) return false;
    if (path.data[path.size - 1] || memchr(path.data, 0, path.size - 1))
        return q3_fail(error, QA_ERROR_FORMAT, reader->offset, "invalid Q3 checkpoint file name");
    q3_file *file = &staged[slot];
    file->kind = (q3_file_kind)kind; file->zip = zip != 0; file->position = position; file->serial = serial;
    if (kind == Q3_FILE_WRITE) {
        if (!host->options.mounts || !host->options.writable_mount)
            return q3_fail(error, QA_ERROR_UNSUPPORTED, slot, "Q3 restore requires a writable content owner");
        qa_vfs_file_state state = {(const char *)path.data, (qa_vfs_write_mode)mode, position};
        return qa_vfs_file_resume(host->options.mounts, host->options.writable_mount, &state, &file->writable, error);
    }
    memcpy(file->restored_digest.bytes, row.data + 40, sizeof(file->restored_digest.bytes));
    qa_sha256_digest actual; qa_sha256(bytes, &actual);
    if (!qa_sha256_equal(&actual, &file->restored_digest))
        return q3_fail(error, QA_ERROR_FORMAT, slot, "Q3 retained file digest mismatch");
    if (host->options.mounts) {
        qa_error local = {0};
        if (qa_vfs_acquire(host->options.mounts, (const char *)path.data, &file->resource, NULL, &local)) {
            if (qa_sha256_equal(qa_resource_digest(file->resource), &actual)) return true;
            qa_resource_release(file->resource); file->resource = NULL;
        } else if (local.code != QA_ERROR_NOT_FOUND) {
            if (error) *error = local;
            return false;
        }
    }
    file->restored_path = malloc(path.size);
    file->restored_bytes.data = bytes.size ? malloc(bytes.size) : NULL;
    if (!file->restored_path || (bytes.size && !file->restored_bytes.data))
        return q3_fail(error, QA_ERROR_MEMORY, slot, "restoring immutable Q3 file version");
    memcpy(file->restored_path, path.data, path.size);
    if (bytes.size) memcpy(file->restored_bytes.data, bytes.data, bytes.size);
    file->restored_bytes.size = bytes.size; return true;
}

bool qa_q3_host_restore(qa_q3_host *host, qa_bytes input, qa_error *error)
{
    if (!idle(host, error)) return false;
    if (host->native)
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Native Q3 continuation requires an executor memory checkpoint");
    if (host->game) {
        if (host->game->portal_count) return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 restore requires empty portal ownership");
        for (size_t i = 0; i < 1024; ++i)
            if (host->game->slots[i].actor.registry || host->game->slots[i].input_motion || host->game->slots[i].input_retired)
                return q3_fail(error, QA_ERROR_ARGUMENT, i, "Q3 restore requires unpublished source actor bindings");
    }
    for (size_t i = 1; i < 64; ++i)
        if (host->files[i].kind != Q3_FILE_CLOSED || host->scripts[i])
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "Q3 restore requires an unpublished empty host");
    if (!input.data || input.size < 32 || memcmp(input.data, "Q3HC", 4) ||
        qa_load_u32le(input.data + 4) != 3 || qa_load_u32le(input.data + 8) != (uint32_t)host->options.role ||
        qa_load_u32le(input.data + 12) != (uint32_t)host->options.abi)
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 host checkpoint identity mismatch");
    uint32_t file_count = qa_load_u32le(input.data + 16), script_count = qa_load_u32le(input.data + 20);
    uint64_t game_size = qa_load_u64le(input.data + 24);
    if (file_count >= 64 || script_count >= 64)
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 checkpoint handle count exceeds source capacity");
    checkpoint_reader reader = {input, 32};
    qa_bytes clipping;
    if (!take(&reader, 48, &clipping, error)) return false;
    qa_bounds clip_bounds = {load_vector(clipping.data), load_vector(clipping.data + 12)};
    qa_bounds clip_brush = {load_vector(clipping.data + 24), load_vector(clipping.data + 36)};
    if (!qa_vec_finite(clip_bounds.mins) || !qa_vec_finite(clip_bounds.maxs) ||
        !qa_vec_finite(clip_brush.mins) || !qa_vec_finite(clip_brush.maxs))
        return q3_fail(error, QA_ERROR_FORMAT, reader.offset, "nonfinite Q3 temporary clip state");
    qa_bytes entity, source, token, name, game_bytes;
    if (!take(&reader, 32, &entity, error)) return false;
    uint64_t source_size = qa_load_u64le(entity.data), offset = qa_load_u64le(entity.data + 8);
    uint32_t token_size = qa_load_u32le(entity.data + 16), name_size = qa_load_u32le(entity.data + 20);
    uint32_t ended = qa_load_u32le(entity.data + 28);
    if (source_size > SIZE_MAX || offset > SIZE_MAX || game_size > SIZE_MAX || ended > 1 ||
        !take(&reader, (size_t)source_size, &source, error) || !take(&reader, token_size, &token, error) ||
        !take(&reader, name_size, &name, error) || !take(&reader, (size_t)game_size, &game_bytes, error))
        return q3_fail(error, QA_ERROR_FORMAT, reader.offset, "invalid Q3 entity parser checkpoint");
    if (source.size != host->entity_cursor.source.size ||
        (source.size && memcmp(source.data, host->entity_cursor.source.data, source.size)))
        return q3_fail(error, QA_ERROR_FORMAT, reader.offset, "Q3 entity text checkpoint belongs to another source");
    qa_common_cursor restored_cursor = host->entity_cursor;
    qa_common_parser restored_parser;
    qa_common_parser_state parser = {.token = token, .name = name, .line = qa_load_i32le(entity.data + 24)};
    if (!qa_common_cursor_restore(&restored_cursor, (qa_common_cursor_state){(size_t)offset, ended != 0}, error) ||
        !qa_common_parser_restore(&restored_parser, &parser, error)) return false;
    q3_game_data *game = NULL;
    if (!q3_game_checkpoint_decode(host, game_bytes, &game, error)) return false;
    ++host->calls;
    q3_file files[64] = {0}; q3_script *scripts[64] = {0};
    bool ok = true;
    for (uint32_t i = 0; ok && i < file_count; ++i)
        ok = restore_file(host, &reader, files, (uint64_t)i + 1, error);
    qa_script_services services = q3_script_services(host);
    for (uint32_t i = 0; ok && i < script_count; ++i) {
        qa_bytes row, bytes;
        if (!take(&reader, 16, &row, error)) { ok = false; break; }
        uint32_t slot = qa_load_u32le(row.data);
        uint64_t size = qa_load_u64le(row.data + 8);
        if (!slot || slot >= 64 || scripts[slot] || qa_load_u32le(row.data + 4) || size > SIZE_MAX) {
            ok = q3_fail(error, QA_ERROR_FORMAT, reader.offset, "invalid Q3 checkpoint script record"); break;
        }
        if (!take(&reader, (size_t)size, &bytes, error)) { ok = false; break; }
        scripts[slot] = calloc(1, sizeof(*scripts[slot]));
        if (!scripts[slot]) { ok = q3_fail(error, QA_ERROR_MEMORY, slot, "restoring Q3 script handle"); break; }
        qa_script_checkpoint state = {0};
        ok = qa_script_checkpoint_decode(bytes, &state, error) &&
             qa_script_restore(&services, &state, &scripts[slot]->reader, error);
        qa_script_checkpoint_free(&state);
    }
    if (ok && reader.offset != input.size)
        ok = q3_fail(error, QA_ERROR_FORMAT, reader.offset, "trailing Q3 host checkpoint bytes");
    if (ok) {
        ok = q3_game_checkpoint_install(host, game, error);
        game = NULL;
    }
    if (ok) {
        memcpy(host->files, files, sizeof(files)); memcpy(host->scripts, scripts, sizeof(scripts));
        host->file_serial = file_count;
        host->clip_bounds = clip_bounds; host->clip_brush = clip_brush;
        host->entity_cursor = restored_cursor; host->entity_parser = restored_parser;
        host->restore_pending = true;
    } else {
        for (size_t i = 1; i < 64; ++i) { q3_file_close(&files[i]); q3_script_close(scripts[i]); }
    }
    q3_game_checkpoint_free(game);
    --host->calls;
    return ok;
}

bool qa_q3_host_finish_restore(qa_q3_host *host, qa_error *error)
{
    if (!host || host->retired || host->calls)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 restore publication requires an idle live host");
    host->restore_pending = false; return true;
}

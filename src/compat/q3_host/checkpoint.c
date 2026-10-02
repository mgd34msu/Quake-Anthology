#include "internal.h"
#include "files.h"
#include "qa/bot_runtime.h"
#include "qa/q3_host_save.h"
#include "qa/source_save.h"

typedef struct checkpoint_writer { qa_buffer bytes; size_t capacity; } checkpoint_writer;
typedef struct checkpoint_reader { qa_bytes bytes; size_t offset; } checkpoint_reader;
static bool decode_file(checkpoint_reader *, q3_file [64],
    q3_write_file_state [64], uint64_t, qa_error *);

bool qa_q3_host_checkpoint_portable_ready(const qa_q3_host *host, qa_error *error)
{
    if (!host || host->retired || host->native || !host->vm || host->calls)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Portable Q3 host requires an idle original QVM owner");
    bool client_globals = host->options.role != QA_QVM_GAME && host->options.script_globals &&
        host->options.script_globals_owner;
    if (host->options.script_globals && !client_globals && (!host->options.bots ||
        host->options.script_globals!=qa_bot_runtime_global_defines(host->options.bots)))
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 shared script defines require their real detached owner");
    for (size_t i = 1; i < 64; ++i) {
        const q3_file *file = &host->files[i];
        if (file->kind != Q3_FILE_WRITE) continue;
        if (!file->serial || file->serial > host->file_serial || file->zip)
            return q3_fail(error, QA_ERROR_FORMAT, i, "Portable writable file lost its source handle identity");
        for (size_t j = 1; j < 64; ++j)
            if (j != i && host->files[j].kind != Q3_FILE_CLOSED && host->files[j].serial == file->serial)
                return q3_fail(error, QA_ERROR_FORMAT, i, "Duplicate portable writable file identity");
        if (!q3_write_file_portable_ready(&host->options.write_view, file->writable, error)) return false;
    }
    return true;
}

static bool service_text(qa_source_save_io *io, const char *text)
{
    bool present = text != NULL;
    size_t length = present ? strlen(text) + 1 : 0;
    return qa_source_save_bool(io, &present) && (!present ||
        (qa_source_save_count(io, &length, SIZE_MAX) &&
         qa_source_save_bytes(io, (void *)text, length)));
}

bool qa_q3_host_checkpoint_services(const qa_q3_host *host, qa_buffer *out, qa_error *error)
{
    if (!out || !qa_q3_host_checkpoint_portable_ready(host, error)) return false;
    const qa_q3_host_options *o = &host->options;
    const qa_command_context *c = &o->command_context;
    if (c->session || c->client || c->registry || c->generation || c->actor.registry ||
        c->actor.generation || c->actor.slot || c->owner != o->owner)
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Q3 retained command context requires its qualified source registry owner");
    qa_source_save_io io = {0};
    uint8_t magic[8] = {'Q','A','G','3','S','V',0,0};
    uint32_t version = 16, role = o->role, abi = o->abi, owner = o->owner;
    bool write_present=o->write_view.root!=NULL;
    bool write_context=o->write_view.resolver.context!=NULL;
    bool write_resolver=o->write_view.resolver.root!=NULL;
    bool engine_present = o->engine_cvars != NULL;
    bool engine_alias = engine_present && o->engine_cvars == o->cvars;
    uint32_t engine_dialect = engine_present ? qa_cvars_dialect(o->engine_cvars) : 0;
    uint32_t time_owner = o->client_time_owner;
    uint32_t time_dialect = o->client_time_cvars ? qa_cvars_dialect(o->client_time_cvars) : 0;
    bool time_present = o->client_time_cvars != NULL;
    bool time_alias = time_present && o->client_time_cvars == o->cvars;
    uint64_t service_owner = o->service_owner, input_owner = o->input_owner;
    uint32_t client_base = o->bot_client_base, entity_base = o->bot_entity_base;
    uint32_t maximum_clients = o->server.maximum_clients, command_seat = c->seat;
    uint32_t dialect = c->dialect, origin = c->origin;
    uint64_t maximum_string = o->maximum_string_bytes, writable = UINT64_MAX;
    size_t mount_count = o->mounts ? qa_vfs_mount_count(o->mounts) : 0;
    for (size_t i = 0; i < mount_count; ++i) {
        qa_vfs_mount_info mount;
        if (!qa_vfs_mount_at(o->mounts, i, &mount))
            return q3_fail(error, QA_ERROR_FORMAT, i, "Q3 source mount inventory changed during qualification");
        if (mount.id == o->writable_mount) writable = i;
    }
    if (o->writable_mount && writable == UINT64_MAX)
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 writable mount is outside its actual source inventory");
    bool shared = o->shared_bot_lifetime, direct = c->direct, console_text = c->console_text;
    bool remapped = o->remapped_bot_namespace;
    bool globals=o->script_globals!=NULL;
    bool globals_alias=globals && o->bots && o->script_globals==qa_bot_runtime_global_defines(o->bots);
    uint64_t globals_owner = o->script_globals_owner;
    bool ok = qa_source_save_writer(&io, NULL, error) &&
        qa_source_save_bytes(&io, magic, sizeof(magic)) && qa_source_save_u32(&io, &version) &&
        qa_source_save_u32(&io, &role) && qa_source_save_u32(&io, &abi) && qa_source_save_u32(&io, &owner) &&
        qa_source_save_bool(&io, &engine_present) && qa_source_save_bool(&io, &engine_alias) &&
        qa_source_save_u32(&io, &engine_dialect) &&
        qa_source_save_bool(&io, &time_present) && qa_source_save_bool(&io, &time_alias) &&
        qa_source_save_u32(&io, &time_owner) && qa_source_save_u32(&io, &time_dialect) &&
        qa_source_save_u64(&io, &service_owner) && qa_source_save_u64(&io, &input_owner) &&
        qa_source_save_u32(&io, &client_base) && qa_source_save_u32(&io, &entity_base) &&
        qa_source_save_u32(&io, &maximum_clients) && qa_source_save_u64(&io, &maximum_string) &&
        qa_source_save_count(&io, &mount_count, SIZE_MAX) && qa_source_save_u64(&io, &writable) &&
        qa_source_save_bool(&io, &write_present) &&
        qa_source_save_bool(&io, &write_context) && qa_source_save_bool(&io, &write_resolver) &&
        qa_source_save_bool(&io, &shared) && qa_source_save_bool(&io, &remapped) &&
        qa_source_save_bool(&io, &globals) && qa_source_save_bool(&io, &globals_alias) &&
        qa_source_save_u64(&io, &globals_owner) &&
        qa_source_save_u32(&io, &command_seat) &&
        qa_source_save_u32(&io, &dialect) && qa_source_save_u32(&io, &origin) &&
        qa_source_save_bool(&io, &direct) && qa_source_save_bool(&io, &console_text) &&
        service_text(&io, c->script) && service_text(&io, o->game_directory) &&
        service_text(&io, o->script_date) && service_text(&io, o->script_time);
    const bool installed[] = {
        o->session != NULL, o->world != NULL, o->cvars != NULL, o->console != NULL, o->mounts != NULL,
        o->scene_resources != NULL, o->scene_world != NULL, o->scene_frame != NULL,
        o->sound_bank != NULL, o->sound_mixer != NULL, o->seat != NULL, o->console_field != NULL,
        o->keys != NULL, o->bots != NULL, o->frontend_lifetime != NULL, o->release_frontend != NULL,
        o->common.print != NULL, o->common.milliseconds != NULL, o->common.calendar != NULL,
        o->common.arguments != NULL, o->common.client_command != NULL, o->common.installed_mods != NULL,
        o->common.clipboard != NULL, o->server.configstring != NULL, o->server.set_configstring != NULL,
        o->server.userinfo != NULL, o->server.set_userinfo != NULL, o->server.user_command != NULL,
        o->server.drop_client != NULL, o->server.send_command != NULL, o->server.allocate_bot != NULL,
        o->server.free_bot != NULL, o->server.bot_snapshot_entity != NULL,
        o->server.bot_console_message != NULL, o->server.bot_user_command != NULL,
        o->server.admit_actor != NULL, o->server.player_velocity != NULL, o->server.world_actor != NULL,
        o->client.gamestate != NULL, o->client.current_snapshot != NULL, o->client.snapshot != NULL,
        o->client.server_command != NULL, o->client.current_command != NULL, o->client.user_command != NULL,
        o->client.command_values != NULL, o->client.source_actor != NULL,
        o->collision.geometry != NULL, o->collision.load_map != NULL,
        o->presentation.seat != NULL, o->presentation.fonts != NULL,
        o->presentation.configuration != NULL, o->presentation.update_screen != NULL,
        o->source_entity != NULL, o->source_entity_context != NULL,
        o->render.context != NULL, o->render.enter != NULL, o->render.leave != NULL,
        o->cvar_namespaces.context != NULL, o->cvar_namespaces.reference != NULL,
        o->cvar_namespaces.resolve != NULL,
        o->input.context != NULL, o->input.bindings != NULL,
        o->client.configstring_absent != NULL,
        o->cvar_entry.context != NULL, o->cvar_entry.entered != NULL,
        o->cvar_status.context != NULL, o->cvar_status.visible != NULL,
        o->browser.context != NULL,
        o->client.ui_state_context != NULL, o->client.ui_state != NULL,
        o->presentation.system_movie_context != NULL, o->presentation.system_movie != NULL
    };
    for (size_t i = 0; ok && i < sizeof(installed) / sizeof(*installed); ++i) {
        bool present = installed[i];
        ok = qa_source_save_bool(&io, &present);
    }
    uint32_t browser_mask=qa_q3_host_browser_services_mask(&o->browser);
    if (ok) ok=qa_source_save_u32(&io,&browser_mask);
    if (ok) ok=q3_collision_scene_services(host,&io);
    if (ok) ok = qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok;
}

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

bool qa_q3_host_checkpoint_portable_state(qa_bytes input, qa_error *error)
{
    if (!input.data || input.size < 64 || memcmp(input.data, "Q3HC", 4) ||
        qa_load_u32le(input.data + 4) != 6 || qa_load_u32le(input.data + 16) >= 64 ||
        qa_load_u32le(input.data + 20) >= 64)
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Invalid portable Q3 host stream");
    checkpoint_reader reader = {input, 64};
    qa_bytes bytes, entity;
    if (!take(&reader, 48, &bytes, error) || !take(&reader, 32, &entity, error)) return false;
    uint64_t source = qa_load_u64le(entity.data), game = qa_load_u64le(input.data + 24);
    if (source > SIZE_MAX || game > SIZE_MAX)
        return q3_fail(error, QA_ERROR_FORMAT, reader.offset, "Portable Q3 source extent exceeds native address space");
    uint64_t bindings_size=qa_load_u64le(input.data+56);
    if (bindings_size>SIZE_MAX || !take(&reader, (size_t)source, &bytes, error) ||
        !take(&reader, qa_load_u32le(entity.data + 16), &bytes, error) ||
        !take(&reader, qa_load_u32le(entity.data + 20), &bytes, error) ||
        !take(&reader, (size_t)game, &bytes, error) ||
        !take(&reader,(size_t)bindings_size,&bytes,error)) return false;
    q3_cvar_binding *bindings=NULL; size_t binding_count=0;
    q3_cvar_cache *caches=NULL; size_t cache_count=0;
    q3_cvar_status status={0};
    if (!q3_cvars_bindings_decode(bytes,&bindings,&binding_count,&caches,&cache_count,&status,error)) return false;
    bool cgame=qa_load_u32le(input.data+8)==QA_QVM_CGAME;
    bool valid=cgame || (!cache_count && !status.read);
    q3_cvars_bindings_free(bindings,binding_count);
    free(caches);
    free(status.previous_value);
    if (!valid) return q3_fail(error,QA_ERROR_FORMAT,0,"Portable cvar status continuation has no CGAME owner");
    q3_file files[64] = {0};
    q3_write_file_state writable[64] = {0};
    uint64_t file_serial = qa_load_u64le(input.data + 32);
    bool ok = true;
    for (uint32_t i = 0; ok && i < qa_load_u32le(input.data + 16); ++i)
        ok = decode_file(&reader, files, writable, file_serial, error);
    bool scripts[64] = {0};
    for (uint32_t i = 0; ok && i < qa_load_u32le(input.data + 20); ++i) {
        qa_bytes row;
        if (!take(&reader, 16, &row, error)) { ok = false; break; }
        uint32_t slot = qa_load_u32le(row.data);
        uint64_t length = qa_load_u64le(row.data + 8);
        if (!slot || slot >= 64 || scripts[slot] || qa_load_u32le(row.data + 4) || length > SIZE_MAX) {
            ok = q3_fail(error, QA_ERROR_FORMAT, reader.offset, "Invalid portable Q3 script record"); break;
        }
        scripts[slot] = true;
        if (!take(&reader, (size_t)length, &bytes, error)) { ok = false; break; }
        qa_script_checkpoint state = {0};
        ok = qa_script_checkpoint_decode(bytes, &state, error);
        qa_script_checkpoint_free(&state);
    }
    if (ok && reader.offset != input.size)
        ok = q3_fail(error, QA_ERROR_FORMAT, reader.offset, "Trailing portable Q3 host stream bytes");
    for (size_t i = 1; i < 64; ++i) q3_file_close(&files[i]);
    return ok;
}

static bool idle(qa_q3_host *host, qa_error *error)
{
    if (!host || host->retired || host->restore_pending || host->calls)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 checkpoint requires an idle live host");
    for (size_t i = 1; i < 64; ++i)
        if (host->script_pending[i] || (host->scripts[i] &&
            (host->scripts[i]->operations || host->scripts[i]->retired)))
            return q3_fail(error, QA_ERROR_ARGUMENT, i, "Q3 script checkpoint operation is active");
    return true;
}

static bool save_file(checkpoint_writer *writer, size_t slot, const q3_file *file, qa_error *error)
{
    qa_bytes bytes = {0};
    const char *path;
    q3_write_file_state state = {0};
    qa_sha256_digest digest = {0};
    if (file->kind == Q3_FILE_READ) {
        bytes = file->resource ? qa_resource_bytes(file->resource) :
                                  (qa_bytes){file->restored_bytes.data, file->restored_bytes.size};
        path = file->resource ? qa_resource_path(file->resource) : file->restored_path;
        digest = file->resource ? *qa_resource_digest(file->resource) : file->restored_digest;
        state.position = file->position;
    } else {
        state = q3_write_file_capture(file->writable); path = state.path;
    }
    if (!path || strlen(path) >= UINT32_MAX)
        return q3_fail(error, QA_ERROR_FORMAT, slot, "invalid Q3 checkpoint file path");
    size_t length = strlen(path) + 1;
    uint8_t row[144] = {0};
    qa_store_u32le(row, (uint32_t)slot); qa_store_u32le(row + 4, (uint32_t)file->kind);
    qa_store_u32le(row + 8, file->zip); qa_store_u32le(row + 12, state.mode);
    qa_store_u64le(row + 16, state.position); qa_store_u64le(row + 24, bytes.size);
    qa_store_u32le(row + 32, (uint32_t)length); memcpy(row + 40, digest.bytes, sizeof(digest.bytes));
    qa_store_u64le(row + 72, file->serial);
    if (file->kind==Q3_FILE_WRITE) {
        if (!qa_fs_stream_reference_valid(&state.reference,error)) return false;
        char *normalized=qa_vfs_normalize_path(state.path,error);
        if (!normalized) return false;
        bool same_path=!strcmp(normalized,state.path); free(normalized);
        if (!same_path) return q3_fail(error,QA_ERROR_FORMAT,slot,"Writable checkpoint path is not normalized");
        qa_store_u32le(row+80,state.reference.root.platform);
        qa_store_u32le(row+84,state.reference.object.platform);
        qa_store_u32le(row+88,state.reference.mode);
        for (unsigned i=0;i<3;++i) {
            qa_store_u64le(row+96+i*8,state.reference.root.words[i]);
            qa_store_u64le(row+120+i*8,state.reference.object.words[i]);
        }
    }
    return append(writer, row, sizeof(row), error) && append(writer, path, length, error) &&
           append(writer, bytes.data, bytes.size, error);
}

bool qa_q3_host_checkpoint(qa_q3_host *host, qa_buffer *out, qa_error *error)
{
    if (!out || !idle(host, error)) return false;
    if (host->native)
        return q3_fail(error, QA_ERROR_UNSUPPORTED, 0, "Native Q3 continuation requires an executor memory checkpoint");
    qa_buffer game = {0},bindings={0};
    if (!q3_game_checkpoint_capture(host, &game, error)) return false;
    if (!q3_cvars_bindings_capture(host,&bindings,error)) { qa_buffer_free(&game); return false; }
    uint32_t files = 0, scripts = 0;
    for (size_t i = 1; i < 64; ++i) {
        files += host->files[i].kind != Q3_FILE_CLOSED; scripts += host->scripts[i] != NULL;
    }
    checkpoint_writer writer = {0};
    uint8_t header[64] = {'Q','3','H','C'};
    qa_store_u32le(header + 4, 6); qa_store_u32le(header + 8, host->options.role);
    qa_store_u32le(header + 12, host->options.abi); qa_store_u32le(header + 16, files);
    qa_store_u32le(header + 20, scripts);
    qa_store_u64le(header + 24, game.size);
    qa_store_u64le(header + 32, host->file_serial);
    qa_store_u64le(header + 40, host->script_generation);
    qa_store_u32le(header + 48, host->bots_shutdown);
    qa_store_u64le(header+56,bindings.size);
    for (size_t i = 1; i < 64; ++i)
        if (host->files[i].kind != Q3_FILE_CLOSED) {
            if (!host->files[i].serial || host->files[i].serial > host->file_serial) {
                qa_buffer_free(&game);
                qa_buffer_free(&bindings);
                return q3_fail(error, QA_ERROR_FORMAT, i, "Q3 file serial leaves its actual owner generation");
            }
            for (size_t j = 1; j < i; ++j)
                if (host->files[j].kind != Q3_FILE_CLOSED &&
                    host->files[j].serial == host->files[i].serial) {
                    qa_buffer_free(&game);
                    qa_buffer_free(&bindings);
                    return q3_fail(error, QA_ERROR_FORMAT, i, "Duplicate Q3 file lifetime identity");
                }
        }
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
        append(&writer, game.data, game.size, error) &&
        append(&writer,bindings.data,bindings.size,error);
    qa_buffer_free(&game); qa_buffer_free(&bindings);
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

static bool decode_file(checkpoint_reader *reader, q3_file staged[64],
                           q3_write_file_state writable[64], uint64_t file_serial, qa_error *error)
{
    qa_bytes row, path, bytes;
    if (!take(reader, 144, &row, error)) return false;
    uint32_t slot = qa_load_u32le(row.data), kind = qa_load_u32le(row.data + 4);
    uint32_t zip = qa_load_u32le(row.data + 8), mode = qa_load_u32le(row.data + 12);
    uint64_t position = qa_load_u64le(row.data + 16), length = qa_load_u64le(row.data + 24);
    uint32_t path_length = qa_load_u32le(row.data + 32);
    uint64_t serial = qa_load_u64le(row.data + 72);
    if (!slot || slot >= 64 || staged[slot].kind != Q3_FILE_CLOSED ||
        (kind != Q3_FILE_READ && kind != Q3_FILE_WRITE) || zip > 1 || !path_length ||
        length > SIZE_MAX || qa_load_u32le(row.data + 36) || !serial || serial > file_serial ||
        (kind == Q3_FILE_READ ? mode != 0 || length > INT32_MAX :
            zip || length || position>INT64_MAX || mode > QA_FS_STREAM_APPEND_SYNC))
        return q3_fail(error, QA_ERROR_FORMAT, reader->offset, "invalid Q3 checkpoint file record");
    for (size_t i = 1; i < 64; ++i)
        if (staged[i].kind != Q3_FILE_CLOSED && staged[i].serial == serial)
            return q3_fail(error, QA_ERROR_FORMAT, reader->offset, "Duplicate Q3 file lifetime identity");
    if (!take(reader, path_length, &path, error) || !take(reader, (size_t)length, &bytes, error)) return false;
    if (path.data[path.size - 1] || memchr(path.data, 0, path.size - 1))
        return q3_fail(error, QA_ERROR_FORMAT, reader->offset, "invalid Q3 checkpoint file name");
    q3_file *file = &staged[slot];
    file->kind = (q3_file_kind)kind; file->zip = zip != 0; file->position = position; file->serial = serial;
    if (kind == Q3_FILE_WRITE) {
        q3_write_file_state state = {.path=(const char *)path.data,.mode=(qa_fs_stream_mode)mode,
            .position=position,.reference={.path=(const char *)path.data,
                .root.platform=qa_load_u32le(row.data+80),
                .object.platform=qa_load_u32le(row.data+84),
                .mode=(qa_fs_stream_mode)qa_load_u32le(row.data+88)}};
        if (qa_load_u32le(row.data+92))
            return q3_fail(error,QA_ERROR_FORMAT,slot,"Invalid writable native reference padding");
        for (unsigned i=0;i<3;++i) {
            state.reference.root.words[i]=qa_load_u64le(row.data+96+i*8);
            state.reference.object.words[i]=qa_load_u64le(row.data+120+i*8);
        }
        if (!qa_fs_stream_reference_valid(&state.reference,error)) return false;
        if (state.reference.mode!=(mode==QA_FS_STREAM_APPEND_SYNC?QA_FS_STREAM_APPEND:mode))
            return q3_fail(error,QA_ERROR_FORMAT,slot,"Writable source mode differs from its native reference");
        char *normalized = qa_vfs_normalize_path(state.path, error);
        if (!normalized) return false;
        bool same_path = !strcmp(normalized, state.path);
        free(normalized);
        if (!same_path)
            return q3_fail(error, QA_ERROR_FORMAT, slot, "Writable checkpoint path is not normalized");
        for (size_t i=40;i<72;++i) if (row.data[i])
            return q3_fail(error,QA_ERROR_FORMAT,slot,"Writable file carries immutable read digest");
        writable[slot]=state; return true;
    }
    for (size_t i=80;i<144;++i) if (row.data[i])
        return q3_fail(error,QA_ERROR_FORMAT,slot,"Read-only file carries writable native reference");
    memcpy(file->restored_digest.bytes, row.data + 40, sizeof(file->restored_digest.bytes));
    qa_sha256_digest actual; qa_sha256(bytes, &actual);
    if (!qa_sha256_equal(&actual, &file->restored_digest))
        return q3_fail(error, QA_ERROR_FORMAT, slot, "Q3 retained file digest mismatch");
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
    if (host->cvar_binding_count || host->cvar_cache_count || host->cvar_status.read)
        return q3_fail(error,QA_ERROR_ARGUMENT,0,"Q3 restore requires empty routed cvar binding ownership");
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
    if (!input.data || input.size < 64 || memcmp(input.data, "Q3HC", 4) ||
        qa_load_u32le(input.data + 4) != 6 || qa_load_u32le(input.data + 8) != (uint32_t)host->options.role ||
        qa_load_u32le(input.data + 12) != (uint32_t)host->options.abi)
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 host checkpoint identity mismatch");
    uint32_t file_count = qa_load_u32le(input.data + 16), script_count = qa_load_u32le(input.data + 20);
    uint64_t game_size = qa_load_u64le(input.data + 24);
    uint64_t file_serial = qa_load_u64le(input.data + 32);
    uint64_t script_generation = qa_load_u64le(input.data + 40);
    uint64_t bindings_size=qa_load_u64le(input.data+56);
    uint32_t bots_shutdown = qa_load_u32le(input.data + 48);
    if (file_count >= 64 || script_count >= 64 || bots_shutdown > 1 || qa_load_u32le(input.data + 52))
        return q3_fail(error, QA_ERROR_FORMAT, 0, "Q3 checkpoint handle count exceeds source capacity");
    checkpoint_reader reader = {input, 64};
    qa_bytes clipping;
    if (!take(&reader, 48, &clipping, error)) return false;
    qa_bounds clip_bounds = {load_vector(clipping.data), load_vector(clipping.data + 12)};
    qa_bounds clip_brush = {load_vector(clipping.data + 24), load_vector(clipping.data + 36)};
    if (!qa_vec_finite(clip_bounds.mins) || !qa_vec_finite(clip_bounds.maxs) ||
        !qa_vec_finite(clip_brush.mins) || !qa_vec_finite(clip_brush.maxs))
        return q3_fail(error, QA_ERROR_FORMAT, reader.offset, "nonfinite Q3 temporary clip state");
    qa_bytes entity, source, token, name, game_bytes,bindings_bytes;
    if (!take(&reader, 32, &entity, error)) return false;
    uint64_t source_size = qa_load_u64le(entity.data), offset = qa_load_u64le(entity.data + 8);
    uint32_t token_size = qa_load_u32le(entity.data + 16), name_size = qa_load_u32le(entity.data + 20);
    uint32_t ended = qa_load_u32le(entity.data + 28);
    if (source_size > SIZE_MAX || offset > SIZE_MAX || game_size > SIZE_MAX || bindings_size>SIZE_MAX || ended > 1 ||
        !take(&reader, (size_t)source_size, &source, error) || !take(&reader, token_size, &token, error) ||
        !take(&reader, name_size, &name, error) || !take(&reader, (size_t)game_size, &game_bytes, error) ||
        !take(&reader,(size_t)bindings_size,&bindings_bytes,error))
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
    q3_cvar_binding *bindings=NULL; size_t binding_count=0;
    q3_cvar_cache *caches=NULL; size_t cache_count=0;
    q3_cvar_status status={0};
    if (!q3_cvars_bindings_decode(bindings_bytes,&bindings,&binding_count,&caches,&cache_count,&status,error)) {
        q3_game_checkpoint_free(game); return false;
    }
    if (host->options.role!=QA_QVM_CGAME && (cache_count || status.read)) {
        q3_game_checkpoint_free(game); q3_cvars_bindings_free(bindings,binding_count);
        free(caches); free(status.previous_value);
        return q3_fail(error,QA_ERROR_FORMAT,0,"Restored cvar status continuation has no CGAME owner");
    }
    ++host->calls;
    q3_file files[64] = {0}; q3_script *scripts[64] = {0};
    q3_write_file_state writable[64]={0};
    qa_script_checkpoint script_states[64]={0};
    bool script_present[64]={0};
    bool ok = true;
    for (uint32_t i = 0; ok && i < file_count; ++i)
        ok = decode_file(&reader, files, writable, file_serial, error);
    for (uint32_t i = 0; ok && i < script_count; ++i) {
        qa_bytes row, bytes;
        if (!take(&reader, 16, &row, error)) { ok = false; break; }
        uint32_t slot = qa_load_u32le(row.data);
        uint64_t size = qa_load_u64le(row.data + 8);
        if (!slot || slot >= 64 || script_present[slot] || qa_load_u32le(row.data + 4) || size > SIZE_MAX) {
            ok = q3_fail(error, QA_ERROR_FORMAT, reader.offset, "invalid Q3 checkpoint script record"); break;
        }
        if (!take(&reader, (size_t)size, &bytes, error)) { ok = false; break; }
        script_present[slot]=true;
        ok = qa_script_checkpoint_decode(bytes, &script_states[slot], error);
    }
    if (ok && reader.offset != input.size)
        ok = q3_fail(error, QA_ERROR_FORMAT, reader.offset, "trailing Q3 host checkpoint bytes");
    /* Decode the complete continuation before any resolver or writable reopen
     * can affect its external owner. */
    for (size_t i=1;ok && i<64;++i) if (files[i].kind==Q3_FILE_WRITE)
        ok=q3_write_file_resume(&host->options.write_view,&writable[i],&files[i].writable,error);
    qa_script_services services = q3_script_services(host);
    for (size_t i=1;ok && i<64;++i) if (script_present[i]) {
        scripts[i]=calloc(1,sizeof(*scripts[i]));
        if (!scripts[i]) { ok=q3_fail(error,QA_ERROR_MEMORY,i,"restoring Q3 script handle"); break; }
        ok=qa_script_restore(&services,&script_states[i],&scripts[i]->reader,error);
    }
    if (ok) {
        ok = q3_game_checkpoint_install(host, game, error);
        game = NULL;
    }
    if (ok) {
        memcpy(host->files, files, sizeof(files)); memcpy(host->scripts, scripts, sizeof(scripts));
        host->file_serial = file_serial;
        host->script_generation = script_generation;
        host->bots_shutdown = bots_shutdown != 0;
        host->clip_bounds = clip_bounds; host->clip_brush = clip_brush;
        host->entity_cursor = restored_cursor; host->entity_parser = restored_parser;
        host->cvar_bindings=bindings; host->cvar_binding_count=binding_count;
        bindings=NULL; binding_count=0;
        host->cvar_caches=caches; host->cvar_cache_count=cache_count;
        caches=NULL; cache_count=0;
        host->cvar_status=status; status=(q3_cvar_status){0};
        host->restore_pending = true;
    } else {
        for (size_t i = 1; i < 64; ++i) { q3_file_close(&files[i]); q3_script_close(scripts[i]); }
    }
    q3_game_checkpoint_free(game);
    q3_cvars_bindings_free(bindings,binding_count);
    free(caches);
    free(status.previous_value);
    for (size_t i=1;i<64;++i) qa_script_checkpoint_free(&script_states[i]);
    --host->calls;
    return ok;
}

bool qa_q3_host_finish_restore(qa_q3_host *host, qa_error *error)
{
    if (!host || host->retired || host->calls)
        return q3_fail(error, QA_ERROR_ARGUMENT, 0, "Q3 restore publication requires an idle live host");
    if (host->restore_pending && !q3_cvars_bindings_restore_ready(host,error)) return false;
    host->restore_pending = false; return true;
}

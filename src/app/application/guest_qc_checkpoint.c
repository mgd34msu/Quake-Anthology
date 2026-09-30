#include "guest_qc_internal.h"

#define QC_ENGINE_LIMIT (64u * 1024u * 1024u)
static bool add_size(size_t *total, size_t amount, qa_error *error)
{
    if (amount > QC_ENGINE_LIMIT - *total)
        return application_fail(error, QA_ERROR_MEMORY, "QuakeC engine checkpoint exceeds its size limit");
    *total += amount; return true;
}
static bool write_text(qa_net_writer *writer, const char *text)
{
    size_t length = text ? strlen(text) : 0;
    return length <= UINT32_MAX && qa_net_write_u32(writer, (uint32_t)length) &&
           qa_net_write_data(writer, text, length);
}
static char *read_text(qa_net_reader *reader)
{
    uint32_t length = qa_net_read_u32(reader);
    if (reader->failed || length > QC_ENGINE_LIMIT || length > qa_net_reader_remaining(reader)) {
        qa_net_reader_fail(reader, "Invalid QuakeC checkpoint text length"); return NULL;
    }
    char *text = malloc((size_t)length + 1);
    if (text == NULL) { qa_net_reader_fail(reader, "Allocating QuakeC checkpoint text"); return NULL; }
    if (!qa_net_read_data(reader, text, length) || memchr(text, 0, length)) {
        free(text); qa_net_reader_fail(reader, "Invalid QuakeC checkpoint text"); return NULL;
    }
    text[length] = 0; return text;
}
static bool write_actor(qa_net_writer *writer, const qa_actor_registry *actors, qa_actor_id actor)
{
    qa_saved_actor_id saved = {0};
    if (actor.registry && !qa_actors_save_reference(actors, actor, &saved, writer->error)) return false;
    return qa_net_write_u8(writer, actor.registry ? 1 : 0) &&
           qa_net_write_u64(writer, saved.generation) && qa_net_write_u32(writer, saved.slot);
}
static bool read_actor(qa_net_reader *reader, const qa_actor_registry *actors,
                       bool live, qa_actor_id *out)
{
    uint8_t present = qa_net_read_u8(reader);
    qa_saved_actor_id saved = {qa_net_read_u64(reader), qa_net_read_u32(reader)};
    if (reader->failed || present > 1 || (!present && (saved.generation || saved.slot)))
        return qa_net_reader_fail(reader, "Invalid QuakeC checkpoint actor reference");
    if (!present) { *out = (qa_actor_id){0}; return true; }
    if (live) {
        const qa_actor_record *record = qa_actors_resolve_saved(actors, saved);
        if (record == NULL) return qa_net_reader_fail(reader, "QuakeC checkpoint client actor is absent");
        *out = record->id; return true;
    }
    return qa_actors_reference_saved(actors, saved, true, out, reader->error);
}
static bool engine_console_safe(struct application_qc_state *engine, qa_error *error)
{
    if (engine->provider->application->operation == APPLICATION_PERSISTING)
        return qa_console_idle(engine->console) ||
            application_fail(error, QA_ERROR_ARGUMENT,
                             "Portable QuakeC continuation requires an idle console owner");
    if (qa_console_pending(engine->console) || qa_console_alias_at(engine->console, engine->provider->owner, 0) != NULL)
        return application_fail(error, QA_ERROR_UNSUPPORTED,
            "QuakeC checkpoints with queued console commands or aliases require a console continuation codec");
    return true;
}
static bool client_binding_matches(struct application_qc_state *engine, uint32_t slot,
                                    const application_qc_client *client, qa_error *error)
{
    qa_qc_slot_binding binding;
    if ((client->colors >> 4) > 13 || (client->colors & 15u) > 13 ||
        !qa_qc_slot(engine->provider->state.qc.instance, slot, &binding) ||
        (client->connected ? binding.kind != QA_QC_SLOT_BORROWED ||
            !qa_actor_id_equal(binding.actor, client->actor) : binding.kind != QA_QC_SLOT_FREE || client->actor.registry != 0))
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC client metadata differs from its reserved guest binding");
    return true;
}
bool application_qc_capture_engine(void *opaque, qa_buffer *out, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (out == NULL || engine->has_frame || engine->input_scope)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC engine checkpoint requires an idle frame");
    if (!engine_console_safe(engine, error)) return false;
    for (uint32_t i = 1; i <= engine->max_clients; ++i)
        if (!client_binding_matches(engine, i, &engine->clients[i], error)) return false;
    size_t capacity = 256;
    if (!add_size(&capacity, ((size_t)engine->max_clients + 1) * 96, error)) return false;
    for (size_t i = 0; i < engine->resource_count; ++i)
        if (!add_size(&capacity, strlen(engine->resources[i].name) + 80, error)) return false;
    for (size_t i = 0; i < engine->message_count; ++i) {
        application_qc_message *message = &engine->messages[i];
        if (message->reference_count > QC_ENGINE_LIMIT / 32 ||
            !add_size(&capacity, message->size + message->reference_count * 32 + 64, error)) return false;
    }
    for (size_t i = 0; i < 64; ++i)
        if (!add_size(&capacity, (engine->lightstyles[i] ? strlen(engine->lightstyles[i]) : 0) + 4, error)) return false;
    size_t cvar_count = qa_cvars_count(engine->cvars);
    if (cvar_count > UINT32_MAX || engine->resource_count > UINT32_MAX || engine->message_count > UINT32_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "QuakeC engine checkpoint record count overflow");
    for (size_t i = 0; i < cvar_count; ++i) {
        const qa_cvar_view *cvar = qa_cvars_at(engine->cvars, i);
        if (!add_size(&capacity, strlen(cvar->name) + strlen(cvar->value) + strlen(cvar->reset_value) +
            (cvar->latched_value ? strlen(cvar->latched_value) : 0) + 64, error)) return false;
    }
    qa_cvar_registry_state registry;
    qa_cvar_record_state *metadata = cvar_count ? calloc(cvar_count, sizeof(*metadata)) : NULL;
    if (cvar_count && metadata == NULL) return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC cvar metadata");
    if (!qa_cvars_capture_metadata(engine->cvars, &registry, metadata, cvar_count, error)) { free(metadata); return false; }
    uint8_t *data = malloc(capacity);
    if (data == NULL) { free(metadata); return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC engine checkpoint"); }
    qa_net_writer writer; qa_net_writer_init(&writer, data, capacity, error);
    const qa_actor_registry *actors = qa_session_actors(engine->services.session);
    const qa_sha256_digest *declaration = qa_resource_digest(engine->provider->launch->declaration);
    uint8_t empty_digest[32] = {0};
    bool ok = qa_net_write_u32(&writer, 3) && qa_net_write_u32(&writer, engine->max_clients) &&
        qa_net_write_u32(&writer, engine->profile) &&
        qa_net_write_data(&writer, declaration ? declaration->bytes : empty_digest, 32) &&
        qa_net_write_u64(&writer, qa_collision_map_identity(qa_world_geometry(engine->world))) &&
        qa_net_write_u64(&writer, engine->source_time_ns) &&
        qa_net_write_f32(&writer, engine->serverflags) && qa_net_write_u8(&writer, engine->loading) &&
        qa_net_write_u8(&writer, engine->initialized) &&
        qa_net_write_u32(&writer, engine->check_slot) && qa_net_write_f32(&writer, engine->check_time) &&
        qa_net_write_i32(&writer, engine->check_cluster) &&
        qa_net_write_u8(&writer, engine->random.front) && qa_net_write_u8(&writer, engine->random.rear) &&
        qa_net_write_u64(&writer, engine->random.draws);
    for (size_t i = 0; ok && i < 31; ++i) ok = qa_net_write_u32(&writer, engine->random.words[i]);
    for (uint32_t i = 1; ok && i <= engine->max_clients; ++i) {
        application_qc_client *client = &engine->clients[i];
        ok = qa_net_write_u32(&writer, client->seat) && qa_net_write_u8(&writer, client->connected) &&
             qa_net_write_u8(&writer, client->spawned) && qa_net_write_u8(&writer, client->spectator) &&
             qa_net_write_u8(&writer, client->has_parms) && qa_net_write_u8(&writer, client->primary_character) &&
             qa_net_write_u8(&writer, client->colors) &&
             write_actor(&writer, actors, client->actor);
        for (unsigned p = 0; ok && p < 16; ++p) ok = qa_net_write_f32(&writer, client->parms[p]);
    }
    if (ok) ok = qa_net_write_u32(&writer, (uint32_t)engine->resource_count);
    for (size_t i = 0; ok && i < engine->resource_count; ++i) {
        application_qc_resource *entry = &engine->resources[i]; uint8_t empty[32] = {0};
        const qa_sha256_digest *digest = qa_resource_digest(entry->source);
        ok = qa_net_write_u32(&writer, entry->kind) && qa_net_write_u32(&writer, entry->value.index) &&
             qa_net_write_u8(&writer, entry->world_model) &&
             write_text(&writer, entry->name) && qa_net_write_data(&writer, digest ? digest->bytes : empty, 32);
    }
    if (ok) ok = qa_net_write_u64(&writer, registry.next_handle) && qa_net_write_u32(&writer, registry.modified_flags) &&
        qa_net_write_u8(&writer, registry.userinfo_modified) && qa_net_write_u8(&writer, registry.server_active) &&
        qa_net_write_u8(&writer, registry.high_characters) && qa_net_write_u8(&writer, registry.cheats) &&
        qa_net_write_u32(&writer, (uint32_t)cvar_count);
    for (size_t i = 0; ok && i < cvar_count; ++i) {
        const qa_cvar_view *cvar = qa_cvars_at(engine->cvars, i);
        ok = write_text(&writer, cvar->name) && write_text(&writer, cvar->value) &&
             write_text(&writer, cvar->reset_value) && qa_net_write_u8(&writer, cvar->latched_value != NULL) &&
             write_text(&writer, cvar->latched_value) && qa_net_write_u32(&writer, cvar->flags) &&
             qa_net_write_u64(&writer, metadata[i].handle) && qa_net_write_u64(&writer, metadata[i].owner) &&
             qa_net_write_u64(&writer, metadata[i].modification_count) &&
             qa_net_write_u8(&writer, metadata[i].modified) && qa_net_write_u8(&writer, metadata[i].console_created);
    }
    for (size_t i = 0; ok && i < 64; ++i) ok = write_text(&writer, engine->lightstyles[i]);
    if (ok) ok = qa_net_write_u32(&writer, (uint32_t)engine->message_count);
    for (size_t i = 0; ok && i < engine->message_count; ++i) {
        application_qc_message *message = &engine->messages[i];
        ok = qa_net_write_u32(&writer, message->destination) && write_actor(&writer, actors, message->recipient) &&
            qa_net_write_u32(&writer, (uint32_t)message->capacity) && qa_net_write_u32(&writer, (uint32_t)message->size) &&
            qa_net_write_u8(&writer, message->overflowed) && qa_net_write_data(&writer, message->data, message->size) &&
            qa_net_write_u32(&writer, (uint32_t)message->reference_count);
        for (size_t p = 0; ok && p < message->reference_count; ++p) {
            qa_application_protocol_reference *reference = &message->references[p];
            ok = qa_net_write_u32(&writer, (uint32_t)reference->offset) &&
                write_actor(&writer, actors, reference->actor) && qa_net_write_u8(&writer, reference->packed_sound);
        }
    }
    free(metadata);
    if (!ok) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&writer)}; return true;
}
static void dispose_candidate(struct application_qc_state *candidate)
{
    for (size_t i = 0; i < candidate->resource_count; ++i) {
        free(candidate->resources[i].name); qa_resource_release(candidate->resources[i].source);
    }
    for (size_t i = 0; i < candidate->message_count; ++i) {
        free(candidate->messages[i].data); free(candidate->messages[i].references);
    }
    for (size_t i = 0; i < 64; ++i) free(candidate->lightstyles[i]);
    qa_console_destroy(candidate->console); qa_cvars_destroy(candidate->cvars);
    free(candidate->resources); free(candidate->messages); free(candidate->clients);
}
bool application_qc_restore_engine(void *opaque, qa_bytes bytes, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (bytes.size > QC_ENGINE_LIMIT || engine->has_frame || engine->input_scope)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC engine restore requires an idle frame");
    if (!engine_console_safe(engine, error)) return false;
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    uint8_t saved_declaration[32], empty_digest[32] = {0};
    const qa_sha256_digest *declaration = qa_resource_digest(engine->provider->launch->declaration);
    if (qa_net_read_u32(&reader) != 3 || qa_net_read_u32(&reader) != engine->max_clients ||
        qa_net_read_u32(&reader) != (uint32_t)engine->profile ||
        !qa_net_read_data(&reader, saved_declaration, sizeof(saved_declaration)) ||
        memcmp(saved_declaration, declaration ? declaration->bytes : empty_digest, sizeof(saved_declaration)) ||
        qa_net_read_u64(&reader) != qa_collision_map_identity(qa_world_geometry(engine->world)))
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC engine checkpoint identity differs");
    struct application_qc_state candidate = {.provider = engine->provider, .world = engine->world,
        .services = engine->services, .profile = engine->profile, .protocol = engine->protocol,
        .max_clients = engine->max_clients, .loading = true};
    candidate.source_time_ns = qa_net_read_u64(&reader); candidate.serverflags = qa_net_read_f32(&reader);
    uint8_t loading = qa_net_read_u8(&reader);
    uint8_t initialized = qa_net_read_u8(&reader); candidate.initialized = initialized != 0;
    candidate.check_slot = qa_net_read_u32(&reader); candidate.check_time = qa_net_read_f32(&reader);
    candidate.check_cluster = qa_net_read_i32(&reader);
    candidate.random.front = qa_net_read_u8(&reader); candidate.random.rear = qa_net_read_u8(&reader);
    candidate.random.draws = qa_net_read_u64(&reader);
    for (size_t i = 0; i < 31; ++i) candidate.random.words[i] = qa_net_read_u32(&reader);
    bool ok = !reader.failed && loading <= 1 && initialized <= 1 && candidate.check_slot <= candidate.max_clients &&
        isfinite(candidate.check_time) && candidate.check_time >= 0 && candidate.check_cluster >= -1 &&
        candidate.random.front < 31 && candidate.random.rear < 31 &&
        (candidate.random.front + 31u - candidate.random.rear) % 31u == 3;
    candidate.clients = calloc((size_t)candidate.max_clients + 1, sizeof(*candidate.clients));
    if (candidate.clients == NULL) ok = application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC restored clients");
    const qa_actor_registry *actors = qa_session_actors(engine->services.session);
    for (uint32_t i = 1; ok && i <= candidate.max_clients; ++i) {
        application_qc_client *client = &candidate.clients[i];
        client->seat = qa_net_read_u32(&reader);
        uint8_t connected = qa_net_read_u8(&reader), spawned = qa_net_read_u8(&reader), spectator = qa_net_read_u8(&reader);
        uint8_t has_parms = qa_net_read_u8(&reader);
        uint8_t primary = qa_net_read_u8(&reader);
        client->colors = qa_net_read_u8(&reader);
        ok = connected <= 1 && spawned <= connected && spectator <= 1 && has_parms <= 1 && primary <= 1 &&
             read_actor(&reader, actors, connected != 0, &client->actor);
        client->connected = connected != 0; client->spawned = spawned != 0; client->spectator = spectator != 0;
        client->has_parms = has_parms != 0;
        client->primary_character = primary != 0;
        if (ok && client->connected && !client->actor.registry) ok = qa_net_reader_fail(&reader, "QuakeC connected client lacks an actor");
        for (unsigned p = 0; ok && p < 16; ++p) client->parms[p] = qa_net_read_f32(&reader);
        if (reader.failed) ok = false;
        if (ok) ok = client_binding_matches(engine, i, client, error);
        for (uint32_t earlier = 1; ok && client->connected && earlier < i; ++earlier)
            if (candidate.clients[earlier].connected &&
                (candidate.clients[earlier].seat == client->seat || qa_actor_id_equal(candidate.clients[earlier].actor, client->actor)))
                ok = qa_net_reader_fail(&reader, "Duplicate QuakeC connected client seat or actor");
    }
    uint32_t resources = ok ? qa_net_read_u32(&reader) : 0;
    if (resources > 131070u) ok = qa_net_reader_fail(&reader, "QuakeC saved precache count exceeds profile limit");
    for (uint32_t i = 0; ok && i < resources; ++i) {
        uint32_t kind = qa_net_read_u32(&reader), index = qa_net_read_u32(&reader);
        uint8_t world = qa_net_read_u8(&reader); char *name = read_text(&reader);
        uint8_t digest[32]; qa_qc_game_resource resource;
        ok = kind <= QA_QC_RESOURCE_SOUND && world <= 1 && name != NULL && *name && qa_net_read_data(&reader, digest, sizeof(digest));
        if (ok && world) {
            if (i != 0 || kind != QA_QC_RESOURCE_MODEL || index != 1) {
                ok = qa_net_reader_fail(&reader, "Invalid QuakeC world precache position");
                free(name); break;
            }
            qa_bsp_model world_model;
            const qa_bsp_view *bsp = qa_collision_bsp(qa_world_geometry(engine->world));
            ok = qa_bsp_read_model(bsp, 0, &world_model, error);
            if (ok) {
                candidate.resources = calloc(32, sizeof(*candidate.resources));
                ok = candidate.resources != NULL;
                if (ok) {
                    candidate.resource_capacity = 32; candidate.resource_count = 1;
                    candidate.resources[0] = (application_qc_resource){.kind = QA_QC_RESOURCE_MODEL, .name = name, .world_model = true,
                        .value = {.index = 1, .bounds = {world_model.bounds.min, world_model.bounds.max}}};
                    name = NULL; resource = candidate.resources[0].value;
                }
            }
        } else if (ok) ok = application_qc_resource_lookup(&candidate, (qa_qc_resource_kind)kind, name, true, &resource, error);
        ok = ok && resource.index == index && candidate.resource_count == (size_t)i + 1;
        if (ok) {
            const qa_sha256_digest *actual = qa_resource_digest(candidate.resources[i].source); uint8_t empty[32] = {0};
            if (memcmp(digest, actual ? actual->bytes : empty, sizeof(digest)))
                ok = qa_net_reader_fail(&reader, "QuakeC saved precache content changed");
        }
        free(name);
    }
    qa_cvar_options options = {.dialect = qa_cvars_dialect(engine->cvars)};
    if (ok) { candidate.cvars = qa_cvars_create(&options, error); ok = candidate.cvars != NULL; }
    qa_cvar_registry_state registry = {0};
    uint64_t next_handle = ok ? qa_net_read_u64(&reader) : 0;
    registry.modified_flags = ok ? qa_net_read_u32(&reader) : 0;
    uint8_t userinfo = ok ? qa_net_read_u8(&reader) : 0, active = ok ? qa_net_read_u8(&reader) : 0;
    uint8_t high = ok ? qa_net_read_u8(&reader) : 0, cheats = ok ? qa_net_read_u8(&reader) : 0;
    registry.next_handle = (size_t)next_handle; registry.userinfo_modified = userinfo != 0;
    registry.server_active = active != 0; registry.high_characters = high != 0; registry.cheats = cheats != 0;
    ok = ok && next_handle <= SIZE_MAX && userinfo <= 1 && active <= 1 && high <= 1 && cheats <= 1;
    uint32_t cvars = ok ? qa_net_read_u32(&reader) : 0;
    if (cvars > bytes.size / 17) ok = qa_net_reader_fail(&reader, "QuakeC saved cvar count exceeds checkpoint");
    qa_cvar_record_state *metadata = ok && cvars ? calloc(cvars, sizeof(*metadata)) : NULL;
    if (ok && cvars && metadata == NULL) ok = application_fail(error, QA_ERROR_MEMORY, "Allocating restored QuakeC cvar metadata");
    for (uint32_t i = 0; ok && i < cvars; ++i) {
        char *name = read_text(&reader), *value = read_text(&reader), *reset = read_text(&reader);
        uint8_t latched = qa_net_read_u8(&reader); char *latch = read_text(&reader); uint32_t flags = qa_net_read_u32(&reader);
        uint64_t handle = qa_net_read_u64(&reader), owner = qa_net_read_u64(&reader), modifications = qa_net_read_u64(&reader);
        uint8_t modified = qa_net_read_u8(&reader), console_created = qa_net_read_u8(&reader);
        ok = name && *name && value && reset && latch && latched <= 1 && (!latched ? !*latch : true) &&
            handle <= SIZE_MAX && modified <= 1 && console_created <= 1 &&
            qa_cvars_find(candidate.cvars, name) == NULL && qa_cvars_register(candidate.cvars, name, reset, flags, engine->provider->owner, NULL, error) &&
            qa_cvars_set(candidate.cvars, name, value, true, error);
        if (ok && latched) ok = qa_cvars_stage(candidate.cvars, name, latch, error);
        if (ok) metadata[i] = (qa_cvar_record_state){qa_cvars_find(candidate.cvars, name)->name,
            (size_t)handle, owner, modifications, modified != 0, console_created != 0};
        free(name); free(value); free(reset); free(latch);
    }
    if (ok) ok = qa_cvars_restore_metadata(candidate.cvars, &registry, metadata, cvars, error);
    free(metadata);
    for (size_t i = 0; ok && i < 64; ++i) { candidate.lightstyles[i] = read_text(&reader); ok = candidate.lightstyles[i] != NULL; }
    uint32_t messages = ok ? qa_net_read_u32(&reader) : 0;
    if ((uint64_t)messages > (uint64_t)candidate.max_clients + 4) ok = qa_net_reader_fail(&reader, "QuakeC message route count exceeds source destinations");
    if (ok && messages) {
        candidate.messages = calloc(messages, sizeof(*candidate.messages));
        ok = candidate.messages != NULL;
        candidate.message_capacity = messages;
    }
    for (uint32_t i = 0; ok && i < messages; ++i) {
        application_qc_message *message = &candidate.messages[candidate.message_count++];
        message->destination = qa_net_read_u32(&reader);
        ok = message->destination <= 4 && (candidate.profile == QA_QC_QUAKEWORLD || message->destination != 4) &&
             read_actor(&reader, actors, false, &message->recipient);
        message->capacity = qa_net_read_u32(&reader); message->size = qa_net_read_u32(&reader);
        uint8_t overflowed = qa_net_read_u8(&reader); message->overflowed = overflowed != 0;
        size_t maximum = candidate.profile == QA_QC_QUAKEWORLD ?
            message->destination == 1 ? 1450u * 5u : message->destination == 0 || message->destination == 3 ? 1024u : 1450u :
            message->destination == 0 || message->destination == 2 ? 1024u : 8000u;
        ok = ok && message->capacity == maximum && message->size <= maximum && overflowed <= 1 &&
             (message->destination == 1 ? message->recipient.registry != 0 : message->recipient.registry == 0);
        if (ok) { message->data = malloc(maximum); ok = message->data && qa_net_read_data(&reader, message->data, message->size); }
        uint32_t references = ok ? qa_net_read_u32(&reader) : 0;
        if (references > message->size * 2) ok = qa_net_reader_fail(&reader, "QuakeC protocol references exceed payload");
        if (ok && references) {
            message->references = calloc(references, sizeof(*message->references));
            ok = message->references != NULL; message->reference_capacity = references;
        }
        for (uint32_t p = 0; ok && p < references; ++p) {
            qa_application_protocol_reference *reference = &message->references[message->reference_count++];
            reference->offset = qa_net_read_u32(&reader);
            ok = read_actor(&reader, actors, false, &reference->actor);
            uint8_t packed = qa_net_read_u8(&reader); reference->packed_sound = packed != 0;
            ok = ok && packed <= 1 && (candidate.profile == QA_QC_QUAKEWORLD || !packed) &&
                 reference->offset < message->size && message->size - reference->offset >= 2;
        }
        for (uint32_t earlier = 0; ok && earlier < i; ++earlier)
            if (candidate.messages[earlier].destination == message->destination &&
                qa_actor_id_equal(candidate.messages[earlier].recipient, message->recipient))
                ok = qa_net_reader_fail(&reader, "Duplicate QuakeC saved message destination");
    }
    if (ok) ok = qa_net_reader_finish(&reader);
    static const char *required[] = {"skill", "deathmatch", "coop", "teamplay", "sv_gravity", "sv_aim", "sv_maxspeed",
        "maxclients", "registered", "developer", "sv_cheats", "samelevel", "timelimit", "fraglimit", "gamecfg"};
    for (size_t i = 0; ok && i < sizeof(required) / sizeof(required[0]); ++i)
        if (qa_cvars_find(candidate.cvars, required[i]) == NULL) ok = qa_net_reader_fail(&reader, "Missing QuakeC engine cvar");
    if (ok && engine->profile == QA_QC_QUAKEWORLD && qa_cvars_find(candidate.cvars, "sv_phs") == NULL)
        ok = qa_net_reader_fail(&reader, "Missing QuakeWorld engine PHS cvar");
    if (ok) {
        candidate.console = application_qc_create_console(engine, candidate.cvars, error);
        ok = candidate.console != NULL && qa_qc_game_rebind_console(engine->provider->state.qc.game,
                    candidate.cvars, candidate.console, error);
    }
    if (ok) {
        qa_console_destroy(engine->console); qa_cvars_destroy(engine->cvars);
        engine->console = candidate.console; engine->cvars = candidate.cvars;
        candidate.console = NULL; candidate.cvars = NULL;
        for (size_t i = 0; i < engine->resource_count; ++i) { free(engine->resources[i].name); qa_resource_release(engine->resources[i].source); }
        for (size_t i = 0; i < engine->message_count; ++i) { free(engine->messages[i].data); free(engine->messages[i].references); }
        for (size_t i = 0; i < 64; ++i) { free(engine->lightstyles[i]); engine->lightstyles[i] = candidate.lightstyles[i]; candidate.lightstyles[i] = NULL; }
        free(engine->resources); free(engine->messages); free(engine->clients);
        engine->resources = candidate.resources; engine->resource_count = candidate.resource_count; engine->resource_capacity = candidate.resource_capacity;
        candidate.resources = NULL; candidate.resource_count = 0;
        engine->messages = candidate.messages; engine->message_count = candidate.message_count; engine->message_capacity = candidate.message_capacity;
        candidate.messages = NULL; candidate.message_count = 0;
        engine->clients = candidate.clients; candidate.clients = NULL;
        engine->source_time_ns = candidate.source_time_ns; engine->serverflags = candidate.serverflags;
        engine->random = candidate.random;
        engine->check_slot = candidate.check_slot; engine->check_time = candidate.check_time; engine->check_cluster = candidate.check_cluster;
        engine->loading = loading != 0;
        engine->initialized = candidate.initialized;
    }
    dispose_candidate(&candidate);
    if (!ok && error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Invalid QuakeC engine checkpoint");
    return ok;
}

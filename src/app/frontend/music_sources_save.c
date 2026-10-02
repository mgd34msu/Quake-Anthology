#include "music_sources_private.h"
#include "save_private.h"

static bool blob(qa_source_save_io *io, qa_bytes *bytes) {
    size_t size = bytes->size;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_WRITE) return qa_source_save_bytes(io, (void *)bytes->data, size);
    if (size > io->input.size - io->offset) return false;
    *bytes = (qa_bytes){io->input.data + io->offset, size}; io->offset += size; return true;
}
static bool world_fields(qa_source_save_io *io, qa_application_content_graph *graph, frontend_music_sources *owner) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ; frontend_music_world *world = &owner->world;
    uint64_t pool = 0, resource = 0;
    qa_sha256_digest identity = {0};
    if (!reading) {
        const qa_launch_instance *instance = qa_launch_instance_lease_view(world->metadata);
        if (!instance || !frontend_music_world_current(owner) ||
            !qa_application_content_resource_id(graph, world->map, &pool, &resource)) return false;
        identity = instance->identity;
    }
    bool ok = frontend_save_text(io, &world->instance) && world->instance && *world->instance &&
        frontend_save_provider(io, owner->application, &world->provider) &&
        qa_source_save_u64(io, &world->map_revision) && qa_source_save_u64(io, &pool) && pool &&
        qa_source_save_u64(io, &resource) && resource && qa_source_save_bytes(io, &identity, sizeof(identity));
    if (reading && ok) {
        const qa_launch_snapshot *snapshot = qa_application_launch(owner->application);
        const qa_launch_instance *instance = qa_launch_snapshot_find(snapshot, world->instance);
        const char *provider = qa_application_provider_instance(owner->application, world->provider);
        const qa_resource *map = qa_application_content_resource(graph, pool, resource);
        ok = instance && provider && !strcmp(provider, world->instance) && map &&
            qa_sha256_equal(&instance->identity, &identity) &&
            qa_launch_instance_retain_metadata(instance, &world->metadata, io->error);
        if (ok) { world->map = (qa_resource *)map; qa_resource_retain(world->map); }
    }
    return ok;
}
static bool origin_fields(qa_source_save_io *io, frontend_music_sources *owner) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    frontend_music_origin *origin = &owner->origin;
    uint32_t kind = origin->kind;
    qa_strings *strings = qa_session_strings(qa_application_session(owner->application));
    const qa_launch_instance *descriptor = !reading ? qa_launch_instance_lease_view(owner->origin_metadata) : NULL;
    const qa_product *product = descriptor ? qa_catalog_product(origin->catalog, origin->product) : NULL;
    char *receiver = reading ? NULL : (char *)qa_strings_cstr(strings, origin->receiver);
    char *instance = reading ? NULL : descriptor ? (char *)descriptor->selection.instance : NULL;
    char *key = reading ? NULL : product ? (char *)product->key : NULL;
    qa_sha256_digest identity = reading ? (qa_sha256_digest){0} : descriptor ? descriptor->identity : (qa_sha256_digest){0};
    bool ok = (reading || (owner->origin_bound && origin->current(origin->context, origin))) &&
        qa_source_save_u32(io, &kind) && kind <= FRONTEND_MUSIC_MODULE &&
        qa_source_save_u32(io, &origin->physical_seat) && origin->physical_seat < owner->frontend->options.seats &&
        frontend_save_text(io, &receiver) && receiver && *receiver &&
        frontend_save_text(io, &instance) && instance && *instance &&
        frontend_save_text(io, &key) && key && *key && qa_source_save_bytes(io, &identity, sizeof(identity));
    if (reading) {
        if (ok) {
            origin->kind = (frontend_music_origin_kind)kind;
            origin->receiver = qa_strings_find(strings, (qa_bytes){(const uint8_t *)receiver, strlen(receiver)});
            ok = origin->receiver != 0;
        }
        free(receiver);
        owner->origin_instance = instance; owner->origin_product = key; owner->origin_identity = identity;
    }
    return ok;
}
static bool bus_fields(qa_source_save_io *io, const qa_audio_checkpoint_refs *refs, uint64_t *bus) {
    qa_buffer encoded = {0}; qa_bytes bytes = {0}; bool reading = io->direction == QA_SOURCE_SAVE_READ;
    bool ok = true;
    if (!reading) {
        ok = refs && refs->encode && refs->encode(refs->context, QA_AUDIO_REFERENCE_BUS, *bus, &encoded, io->error);
        bytes = (qa_bytes){encoded.data, encoded.size};
    }
    if (ok) ok = blob(io, &bytes) && bytes.size;
    if (ok && reading) ok = refs && refs->decode && refs->decode(refs->context, QA_AUDIO_REFERENCE_BUS, bytes, bus, io->error) && *bus;
    qa_buffer_free(&encoded); return ok;
}
static bool command_fields(qa_source_save_io *io, frontend_music_command *command, uint64_t registry) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    qa_command_context *context = &command->context;
    uint32_t dialect = context->dialect, origin = context->origin;
    bool ok = qa_source_save_u64(io, &context->session) && qa_source_save_u64(io, &context->owner) &&
        qa_source_save_u64(io, &context->client) && qa_source_save_u32(io, &context->seat) &&
        qa_source_save_u32(io, &dialect) && dialect <= QA_CONSOLE_Q3 &&
        qa_source_save_u32(io, &origin) && origin <= QA_COMMAND_REMOTE &&
        qa_source_save_bool(io, &context->direct) && qa_source_save_bool(io, &context->console_text) &&
        qa_source_save_u64(io, &context->registry) && qa_source_save_u64(io, &context->generation) &&
        qa_source_save_actor(io, &context->actor) && frontend_save_text(io, &command->script) &&
        qa_source_save_count(io, &command->argc, 1024) && command->argc;
    if (reading && ok) {
        context->dialect = (qa_console_dialect)dialect; context->origin = (qa_command_origin)origin;
        context->script = command->script;
        if (context->registry == registry) context->registry = qa_actors_identity(qa_session_actors(io->session));
        else { context->registry = 0; if (!context->generation) context->generation = UINT64_MAX; }
        command->argv = calloc(command->argc, sizeof(*command->argv));
        if (!command->argv) { command->argc = 0; ok = frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring queued music arguments"); }
    }
    size_t size = 0;
    for (size_t i = 0; ok && i < command->argc; ++i) {
        ok = frontend_save_text(io, command->argv + i) && command->argv[i];
        if (ok) { size_t n = strlen(command->argv[i]); ok = n < 9216 - size; if (ok) size += n + 1; }
    }
    return ok && (!strcmp(command->argv[0], "music") || !strcmp(command->argv[0], "cd"));
}
static bool fields(qa_source_save_io *io, qa_application_content_graph *graph, const qa_audio_checkpoint_refs *refs,
    frontend_music_sources *owner) {
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint8_t magic[4] = {'Q','F','M','S'}; uint32_t version = 2;
    bool ok = qa_source_save_bytes(io, magic, 4) && !memcmp(magic, "QFMS", 4) &&
        qa_source_save_u32(io, &version) && version == 2 && qa_source_save_u64(io, &owner->seed);
    uint64_t catalog = reading ? 0 : qa_application_content_catalog_id(graph, owner->menu_catalog);
    const qa_product *selected = !reading && owner->menu_product ? qa_catalog_product(owner->menu_catalog, owner->menu_product) : NULL;
    char *key = selected ? (char *)selected->key : NULL;
    if (ok) ok = qa_source_save_u64(io, &catalog) && catalog && frontend_save_text(io, &key);
    if (reading && ok) {
        ok = qa_application_content_retain_catalog(graph, catalog, &owner->menu_catalog, io->error) &&
            owner->menu_catalog == qa_application_catalog(owner->application);
        const qa_product *row = ok && key ? qa_catalog_find(owner->menu_catalog, key) : NULL;
        if (key) ok = ok && owner->engine && row && row->availability == QA_CONTENT_INSTALLED;
        if (ok) owner->menu_product = row ? row->id : 0;
        if (ok && owner->engine && !row) for (size_t i = 0; i < qa_catalog_count(owner->menu_catalog); ++i)
            if (qa_catalog_at(owner->menu_catalog, i)->availability == QA_CONTENT_INSTALLED) { ok = false; break; }
    }
    if (reading) free(key);
    qa_buffer encoded_controls = {0}; qa_bytes controls = {0};
    if (ok && !reading) { ok = qa_audio_music_controls_checkpoint(owner->controls, &encoded_controls, io->error);
        controls = (qa_bytes){encoded_controls.data, encoded_controls.size}; }
    if (ok) ok = blob(io, &controls) && controls.size;
    if (reading && ok) ok = qa_audio_music_controls_restore(controls, &owner->controls, io->error);
    qa_buffer_free(&encoded_controls);
    if (ok) ok = qa_source_save_bool(io, &owner->has_origin);
    if (ok && owner->has_origin) ok = origin_fields(io, owner);
    /* Automatic slots use real frontend issuance. Explicit slots use their
     * actual SOURCE/native/module bus namespace and its typed audio receipt. */
    for (size_t i = 0; ok && i < 2; ++i) {
        bool present = owner->buses[i] != 0;
        if (ok) ok = qa_source_save_bool(io, &present);
        if (ok && present) ok = i == FRONTEND_MUSIC_WORLD && owner->has_origin ? bus_fields(io, refs, owner->buses + i) :
            qa_source_save_u64(io, owner->buses + i) && owner->buses[i] > QA_FRONTEND_COMMAND_OWNER &&
            owner->buses[i] - QA_FRONTEND_COMMAND_OWNER <= owner->frontend->next_source_id;
        if (ok && i && owner->buses[i]) ok = owner->buses[i] != owner->buses[0];
    }
    if (ok) ok = (!!owner->buses[FRONTEND_MUSIC_MENU] == (owner->engine && owner->menu_product));
    if (ok && owner->has_origin) ok = owner->buses[FRONTEND_MUSIC_WORLD] != 0;
    bool world = owner->world.metadata != NULL;
    if (ok) ok = qa_source_save_bool(io, &world);
    if (ok && owner->buses[FRONTEND_MUSIC_WORLD] && !owner->has_origin) ok = world;
    if (ok && world) ok = world_fields(io, graph, owner);
    for (size_t i = 0; ok && i < 2; ++i) {
        qa_buffer encoded = {0}; qa_bytes saved = {0};
        if (!reading && owner->buses[i]) {
            ok = owner->policies[i] && frontend_music_policy_checkpoint(owner->policies[i], graph, refs, &encoded, io->error);
            saved = (qa_bytes){encoded.data, encoded.size};
        }
        if (ok) ok = blob(io, &saved) && (!!saved.size == !!owner->buses[i]);
        if (reading && ok && saved.size) ok = frontend_music_policy_restore_prepare(owner->frontend, graph, refs,
            saved, owner->policies + i, io->error) &&
            frontend_music_policy_binding_is(owner->policies[i], owner->frontend, owner->engine, owner->buses[i], i == FRONTEND_MUSIC_MENU);
        qa_buffer_free(&encoded);
    }
    uint32_t output = owner->output;
    if (ok) ok = qa_source_save_u32(io, &output) && output <= FRONTEND_MUSIC_WORLD;
    if (reading && ok) owner->output = (frontend_music_slot)output;
    uint64_t registry = qa_actors_identity(qa_session_actors(io->session)); size_t count = 0;
    if (!reading) for (const frontend_music_command *command = owner->commands; command; command = command->next) ++count;
    if (ok) ok = qa_source_save_u64(io, &registry) && registry &&
        qa_source_save_count(io, &count, reading ? io->input.size - io->offset : SIZE_MAX);
    frontend_music_command *command = owner->commands;
    for (size_t i = 0; ok && i < count; ++i) {
        if (reading) {
            command = calloc(1, sizeof(*command));
            if (!command) { ok = frontend_fail(io->error, QA_ERROR_MEMORY, "Restoring pending menu music"); break; }
            if (owner->commands_tail) owner->commands_tail->next = command; else owner->commands = command;
            owner->commands_tail = command;
        }
        ok = command_fields(io, command, registry);
        if (!reading) command = command->next;
    }
    return ok;
}
bool frontend_music_sources_checkpoint(const frontend_music_sources *owner, const qa_application_content_graph *graph,
    const qa_audio_checkpoint_refs *refs, qa_buffer *out, qa_error *e) {
    if (!owner || !frontend_music_sources_idle(owner) || owner->restoring || !owner->frontend->capture || !graph ||
        graph != qa_application_content_graph_read(owner->application) || !out || out->data || out->size)
        return frontend_fail(e, QA_ERROR_ARGUMENT, "Music source capture requires its actual immutable/audio owner lease");
    qa_source_save_io io = {0};
    bool ok = qa_source_save_writer(&io, qa_application_session(owner->application), e) &&
        fields(&io, (qa_application_content_graph *)graph, refs, (frontend_music_sources *)owner) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io);
    return ok || frontend_fail(e, QA_ERROR_FORMAT, "Music source continuation leaves its genuine content/audio topology");
}
bool frontend_music_sources_restore_prepare(qa_frontend *f, qa_application_content_graph *graph,
    const qa_audio_checkpoint_refs *refs, qa_bytes bytes, frontend_music_sources **out, qa_error *e) {
    if (!f || !f->application || !f->source_restoring || f->capture || !graph ||
        graph != qa_application_content_graph_read(f->application) || !out || *out)
        return frontend_fail(e, QA_ERROR_ARGUMENT, "Music source import requires its actual empty restored frontend");
    frontend_music_sources *owner = calloc(1, sizeof(*owner));
    if (!owner) return frontend_fail(e, QA_ERROR_MEMORY, "Retaining restored music sources");
    owner->frontend = f; owner->application = f->application; owner->engine = f->audio; owner->slot = out;
    owner->restoring = true; *out = owner;
    qa_source_save_io io = {0};
    bool ok = qa_source_save_reader(&io, qa_application_session(f->application), bytes, e) &&
        fields(&io, graph, refs, owner) && qa_source_save_finish(&io, NULL);
    qa_source_save_dispose(&io);
    /* Failed import deliberately retains genuine claimed graph references;
     * the isolated candidate's checked retirement owns their cleanup. */
    return ok || frontend_fail(e, QA_ERROR_FORMAT, "Invalid saved application music source continuation");
}
bool frontend_music_sources_restore_finish(frontend_music_sources *owner, qa_error *e) {
    if (!frontend_music_sources_current(owner) || !owner->restoring || owner->busy || !owner->frontend->source_restoring)
        return frontend_fail(e, QA_ERROR_ARGUMENT, "Music source import lost its real retained candidate");
    if (owner->has_origin ? !owner->origin_bound || !owner->origin.current(owner->origin.context, &owner->origin) :
        owner->policies[FRONTEND_MUSIC_WORLD] && !frontend_music_world_current(owner))
        return frontend_fail(e, QA_ERROR_FORMAT, "Saved WORLD music does not own the actual restored entity source/map");
    if (!owner->has_origin && owner->policies[FRONTEND_MUSIC_WORLD]) {
        const qa_launch_instance *world = qa_launch_instance_lease_view(owner->world.metadata);
        qa_catalog *catalog = world ? qa_launch_instance_catalog(world) : NULL;
        if (!world || !frontend_music_policy_world_is(owner->policies[FRONTEND_MUSIC_WORLD], catalog,
                world->selection.product, world->content, frontend_music_world_fallback(owner)))
            return frontend_fail(e, QA_ERROR_FORMAT, "Saved WORLD playlist leaves its actual restored source mounts");
    }
    for (size_t i = 0; i < 2; ++i) if (owner->policies[i]) {
        qa_audio_music *music = frontend_music_policy_player(owner->policies[i]);
        if (frontend_music_policy_idle(owner->policies[i]) && qa_audio_music_controls_is(music, owner->controls)) continue;
        if (!qa_audio_music_controls_bind(music, owner->controls, e) || !frontend_music_policy_restore_finish(owner->policies[i], e)) return false;
    }
    owner->restoring = false;
    if (frontend_music_sources_parent_is(owner, owner->frontend, owner->engine)) return true;
    owner->restoring = true;
    return frontend_fail(e, QA_ERROR_FORMAT, "Saved music output does not own its actual imported menu/WORLD bus route");
}

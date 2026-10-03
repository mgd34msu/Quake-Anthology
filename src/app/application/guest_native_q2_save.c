#include "guest_native_q2_private.h"
#include "guest_native_q2_attack.h"
#include "guest_native_q2_combat.h"
#include "guest_q2_control.h"
#include "qa/network.h"
#include "qa/native_observe.h"
#include "native_q2_publication.h"
#include "native_q2_wire_engine.h"
#include "native_q2_visibility.h"
#include "native_q2_callbacks.h"
#include "native_q2_client_stages.h"
#include "native_q2_client_outputs.h"
#include "native_q2_source_actors.h"
#include "native_q2_inventory_scanner.h"
#include "native_q2_inventory_rows.h"
#include <limits.h>

static bool write_actor(qa_net_writer *writer, const qa_actor_registry *actors,
    qa_actor_id actor, qa_error *error)
{
    qa_saved_actor_id saved = {0};
    if (actor.registry && !qa_actors_save_reference(actors, actor, &saved, error)) return false;
    return qa_net_write_u8(writer, actor.registry ? 1 : 0) &&
        qa_net_write_u64(writer, saved.generation) && qa_net_write_u32(writer, saved.slot);
}

static bool read_actor(qa_net_reader *reader, const qa_actor_registry *actors,
    qa_actor_id *out, qa_error *error)
{
    uint8_t present = qa_net_read_u8(reader);
    qa_saved_actor_id saved;
    saved.generation = qa_net_read_u64(reader);
    saved.slot = qa_net_read_u32(reader);
    *out = (qa_actor_id){0};
    if (reader->failed) return false;
    if (present > 1)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 continuation has an invalid actor presence");
    if (!present)
        return (!saved.slot && !saved.generation) || application_fail(error, QA_ERROR_FORMAT, "Native Q2 continuation has an invalid empty actor reference");
    const qa_actor_record *actor = qa_actors_resolve_saved(actors, saved);
    if (!actor) return application_fail(error, QA_ERROR_NOT_FOUND, "Native Q2 continuation actor is not restored");
    *out = actor->id;
    return true;
}

static bool write_text(qa_net_writer *writer, const char *text)
{
    size_t bytes = strlen(text ? text : "");
    return bytes < UINT32_MAX && qa_net_write_u32(writer, (uint32_t)bytes) &&
        qa_net_write_data(writer, text ? text : "", bytes);
}

static bool read_text(qa_net_reader *reader, char **out, qa_error *error)
{
    uint32_t length = qa_net_read_u32(reader);
    qa_bytes bytes;
    if (reader->failed || length > 64u * 1024u * 1024u ||
        !qa_net_read_bytes(reader, length, &bytes)) return false;
    if (memchr(bytes.data, 0, bytes.size))
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 continuation text contains an embedded terminator");
    char *copy = malloc((size_t)length + 1);
    if (!copy) return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q2 continuation text");
    if (length) memcpy(copy, bytes.data, length);
    copy[length] = 0; *out = copy;
    return true;
}

bool application_native_q2_prepare_restore(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 private restore requires an idle detached candidate owner");
    if (!application_q2_control_suspend(engine, error)) return false;
    if (!application_native_q2_inventory_scanner_suspend(engine->inventory_scanner,error)) return false;
    if (!application_native_q2_callbacks_suspend(engine,error)) return false;
    if (!application_native_q2_source_actors_suspend(engine,error)) return false;
    if (!application_native_q2_stages_close(engine,error)) return false;
    if (!application_native_q2_combat_suspend(engine, error)) return false;
    for (uint32_t i = 1; i < 257; ++i)
        if (!application_native_q2_inventory_detach(engine, i, error)) return false;
    return application_native_q2_attack_suspend(engine, error);
}

bool application_native_q2_restore_finish(application_provider *provider, qa_error *error)
{
    struct application_native_q2 *engine = provider ? provider->state.native.q2_engine : NULL;
    if (!engine || !application_native_q2_idle(provider))
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 private restore finish requires an idle source owner");
    if (engine->profile != QA_NATIVE_Q2_CGAME_API2023 && engine->map_ready &&
        !qa_native_host_world_actor_bind(provider->state.native.host, engine->world_actor, error)) return false;
    if (!application_native_q2_attack_activate(engine, error)) return false;
    if (!application_native_q2_combat_activate(engine, error)) return false;
    if (engine->initialized && engine->map_ready &&
        !application_q2_control_activate(engine, error)) return false;
    if (!application_native_q2_combat_finish(provider, error)) return false;
    if (!application_native_q2_visibility_validate(engine, error)) return false;
    if(!application_native_q2_callbacks_finish_restore(engine,error)) return false;
    if(engine->inventory_rows&&engine->map_ready&&
        (!application_native_q2_inventory_rows_prepare(engine->inventory_rows,error)||
         !application_native_q2_inventory_scanner_finish_restore(engine->inventory_scanner,error)||
         !application_native_q2_inventory_scanner_activate(engine->inventory_scanner,error))) return false;
    bool ok=application_native_q2_inventory_finish(provider, error) &&
        (!engine->map_ready || (application_native_q2_callbacks_validate(engine,error) &&
        application_native_q2_stages_prepare(engine,error) &&
        application_native_q2_client_outputs_finish_restore(engine,error) &&
        application_native_q2_callbacks_register(engine,error)));
    if(ok)ok=qa_native_observers_restore_ready(qa_native_host_instance(provider->state.native.host),error);
    if(ok)engine->restore_record=(qa_bytes){0};
    return ok;
}

bool application_native_q2_capture_engine(void *opaque, qa_buffer *out, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    if (!engine || !out || engine->current_client || engine->disconnect_client || engine->arguments.count || engine->shutting_down)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 engine continuation requires drained client and command calls");
    if(engine->callbacks&&!application_native_q2_callbacks_current(engine->callbacks))
        return application_fail(error,QA_ERROR_ARGUMENT,"Native callback continuation retains source projections or lacks validation");
    qa_application *app = engine->provider->application;
    const char *map = engine->map_name ? qa_strings_cstr(qa_session_strings(app->session), engine->map_name) : "";
    const char *spawn = engine->spawn_point ? qa_strings_cstr(qa_session_strings(app->session), engine->spawn_point) : "";
    const char *entities = engine->entity_text ? engine->entity_text : "";
    size_t size = 124u + 257u * 3680u + engine->configstring_count * 4u;
    const char *texts[] = {map, spawn, entities};
    for (size_t i = 0; i < 3; ++i) {
        if (!texts[i] || strlen(texts[i]) > 64u * 1024u * 1024u - size)
            return application_fail(error, QA_ERROR_MEMORY, "Native Q2 continuation text exceeds its storage budget");
        size += strlen(texts[i]);
    }
    for (uint32_t i = 0; i < engine->configstring_count; ++i) {
        size_t bytes = engine->configstrings[i] ? strlen(engine->configstrings[i]) : 0;
        if (bytes > 64u * 1024u * 1024u - size)
            return application_fail(error, QA_ERROR_MEMORY, "Native Q2 configstring continuation exceeds its storage budget");
        size += bytes;
    }
    qa_buffer attack = {0}, combat = {0};
    if (!application_native_q2_attack_capture(engine, &attack, error)) return false;
    if (!application_native_q2_combat_capture(engine, &combat, error)) { qa_buffer_free(&attack); return false; }
    if (size > 64u * 1024u * 1024u - 4u || attack.size > 64u * 1024u * 1024u - size - 4u) {
        qa_buffer_free(&attack); qa_buffer_free(&combat);
        return application_fail(error, QA_ERROR_MEMORY, "Native source attack continuation exceeds the engine budget");
    }
    size += 4u + attack.size;
    if (size > 64u * 1024u * 1024u - 4u || combat.size > 64u * 1024u * 1024u - size - 4u) {
        qa_buffer_free(&attack); qa_buffer_free(&combat);
        return application_fail(error, QA_ERROR_MEMORY, "Native deferred damage continuation exceeds the engine budget");
    }
    size += 4u + combat.size;
    qa_buffer publication = {0};
    if (!application_native_q2_publication_capture(engine, &publication, error)) {
        qa_buffer_free(&attack); qa_buffer_free(&combat); return false;
    }
    if (size > 64u * 1024u * 1024u - 4u || publication.size > 64u * 1024u * 1024u - size - 4u) {
        qa_buffer_free(&attack); qa_buffer_free(&combat); qa_buffer_free(&publication);
        return application_fail(error, QA_ERROR_MEMORY, "Native publication continuation exceeds the engine budget");
    }
    size += 4u + publication.size;
    qa_buffer wire = {0};
    if (!application_native_q2_wire_capture(engine, &wire, error)) {
        qa_buffer_free(&attack); qa_buffer_free(&combat); qa_buffer_free(&publication); return false;
    }
    if (size > 64u * 1024u * 1024u - 4u || wire.size > 64u * 1024u * 1024u - size - 4u) {
        qa_buffer_free(&attack); qa_buffer_free(&combat); qa_buffer_free(&publication); qa_buffer_free(&wire);
        return application_fail(error, QA_ERROR_MEMORY, "Original Q2 Engine namespace exceeds the cold budget");
    }
    size += 4u + wire.size;
    qa_buffer callbacks={0},scanner={0},stages={0},visibility={0};
    bool children=application_native_q2_callbacks_capture(engine,&callbacks,error)&&
        (!engine->inventory_scanner||application_native_q2_inventory_scanner_capture(engine->inventory_scanner,&scanner,error))&&
        application_native_q2_stages_capture(engine,&stages,error)&&
        application_native_q2_visibility_capture(engine,&visibility,error);
    qa_buffer *additional[]={&callbacks,&scanner,&stages,&visibility};
    for(size_t i=0;children&&i<4;++i) {
        if(size>64u*1024u*1024u-4u||additional[i]->size>64u*1024u*1024u-size-4u)
            children=application_fail(error,QA_ERROR_MEMORY,"Native callback/scanner continuation exceeds its engine budget");
        else size+=4u+additional[i]->size;
    }
    if(!children) {
        qa_buffer_free(&attack); qa_buffer_free(&combat); qa_buffer_free(&publication); qa_buffer_free(&wire);
        qa_buffer_free(&callbacks); qa_buffer_free(&scanner); qa_buffer_free(&stages); qa_buffer_free(&visibility); return false;
    }
    qa_buffer buffer = {.data = malloc(size), .size = size};
    if (!buffer.data) { qa_buffer_free(&attack); qa_buffer_free(&combat); qa_buffer_free(&publication); qa_buffer_free(&wire); qa_buffer_free(&callbacks); qa_buffer_free(&scanner); qa_buffer_free(&stages); qa_buffer_free(&visibility); return application_fail(error, QA_ERROR_MEMORY, "Retaining native Q2 engine continuation"); }
    qa_net_writer writer; qa_net_writer_init(&writer, buffer.data, buffer.size, error);
    const qa_actor_registry *actors = qa_session_actors(app->session);
    bool ok = qa_net_write_u32(&writer, UINT32_C(0x4532514e)) &&
        qa_net_write_u32(&writer, (uint32_t)engine->profile) && qa_net_write_u32(&writer, engine->configstring_count) &&
        qa_net_write_u8(&writer, engine->initialized) && qa_net_write_u8(&writer, engine->map_ready) &&
        write_actor(&writer, actors, engine->world_actor, error) &&
        qa_net_write_u64(&writer, engine->frame.number) && qa_net_write_u64(&writer, engine->frame.start_ns) &&
        qa_net_write_u64(&writer, engine->frame.elapsed_ns) && qa_net_write_u64(&writer, engine->frame.time_ns) &&
        qa_net_write_u32(&writer, (uint32_t)engine->frame.phase) &&
        write_text(&writer, map) && write_text(&writer, spawn) && write_text(&writer, entities);
    for (uint32_t i = 0; ok && i < engine->configstring_count; ++i)
        ok = write_text(&writer, engine->configstrings[i]);
    for (uint32_t i = 0; ok && i < 257; ++i) {
        const application_native_q2_client *client = &engine->clients[i];
        uint8_t flags = (uint8_t)(client->reserved | client->connected << 1 | client->begun << 2 |
            client->bot << 3 | client->disconnect_started << 4 | client->userinfo_present << 5);
        size_t userinfo_limit = engine->callbacks ? sizeof(client->userinfo) : engine->profile == QA_NATIVE_Q2_GAME_API2023 ? 2048u : 512u;
        if (!memchr(client->userinfo, 0, userinfo_limit) ||
            (engine->callbacks&&!application_native_q2_callbacks_userinfo_validate(engine,client->userinfo,error)) ||
            (!client->userinfo_present && client->userinfo[0]) ||
            (client->connected && !client->userinfo_present) ||
            !qa_actor_id_equal(client->protocol_fog_actor, client->actor) ||
            client->protocol_fog.bits || client->protocol_fog.time) {
            ok = application_fail(error, QA_ERROR_FORMAT, "Native Q2 client lost its retained Source observer identity");
            break;
        }
        ok = write_actor(&writer, actors, client->actor, error) && qa_net_write_u32(&writer, client->seat) &&
            qa_net_write_u8(&writer, flags) &&
            write_text(&writer, client->userinfo) &&
            qa_net_write_data(&writer, client->layout, sizeof(client->layout));
        for (size_t item = 0; ok && item < 256; ++item)
            ok = qa_net_write_u16(&writer, (uint16_t)client->inventory[item]);
        qa_q2_wire_fog fog = client->protocol_fog;
        fog.bits = UINT16_MAX;
        if (ok) ok = write_actor(&writer, actors, client->protocol_fog_actor, error) &&
            qa_q2_fog_write(&writer, &fog);
    }
    if (ok) ok = qa_net_write_u32(&writer, (uint32_t)attack.size) && qa_net_write_data(&writer, attack.data, attack.size);
    if (ok) ok = qa_net_write_u32(&writer, (uint32_t)combat.size) && qa_net_write_data(&writer, combat.data, combat.size);
    if (ok) ok = qa_net_write_u32(&writer, (uint32_t)publication.size) && qa_net_write_data(&writer, publication.data, publication.size);
    if (ok) ok = qa_net_write_u32(&writer, (uint32_t)wire.size) && qa_net_write_data(&writer, wire.data, wire.size);
    for(size_t i=0;ok&&i<4;++i)
        ok=qa_net_write_u32(&writer,(uint32_t)additional[i]->size)&&qa_net_write_data(&writer,additional[i]->data,additional[i]->size);
    qa_buffer_free(&attack); qa_buffer_free(&combat);
    qa_buffer_free(&publication);
    qa_buffer_free(&wire);
    qa_buffer_free(&callbacks); qa_buffer_free(&scanner); qa_buffer_free(&stages); qa_buffer_free(&visibility);
    if (!ok) { qa_buffer_free(&buffer); return false; }
    buffer.size = qa_net_writer_size(&writer); *out = buffer;
    return true;
}

bool application_native_q2_restore_engine(void *opaque, qa_bytes bytes, qa_error *error)
{
    struct application_native_q2 *engine = opaque;
    if (!engine || engine->current_client || engine->disconnect_client || engine->arguments.count || engine->shutting_down ||
        bytes.size > 64u * 1024u * 1024u)
        return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 engine restore requires drained source calls");
    for (uint32_t i = 1; i < 257; ++i)
        if (engine->clients[i].inventory_bound)
            return application_fail(error, QA_ERROR_ARGUMENT, "Native Q2 restore requires primary inventory retirement before source replacement");
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    if (qa_net_read_u32(&reader) != UINT32_C(0x4532514e) ||
        qa_net_read_u32(&reader) != (uint32_t)engine->profile ||
        qa_net_read_u32(&reader) != engine->configstring_count)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 continuation profile differs from its admitted owner");
    uint8_t initialized = qa_net_read_u8(&reader), map_ready = qa_net_read_u8(&reader);
    qa_actor_id world;
    const qa_actor_registry *actors = qa_session_actors(engine->provider->application->session);
    if (initialized > 1 || map_ready > 1 || (map_ready && !initialized))
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 continuation lifecycle is invalid");
    if (!read_actor(&reader, actors, &world, error)) return false;
    if (world.registry) {
        const qa_actor_record *record = qa_actors_get(actors, world);
        if (!record || record->owner != engine->provider->owner || !record->has_source || record->source_slot)
            return application_fail(error, QA_ERROR_FORMAT, "Native Q2 continuation world actor differs from its source owner");
    }
    if (map_ready && engine->profile != QA_NATIVE_Q2_CGAME_API2023 && !world.registry)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 ready continuation has no world actor");
    qa_source_frame frame = {.provider = engine->provider->owner,
        .kind = engine->provider->component.clock.kind};
    frame.number = qa_net_read_u64(&reader); frame.start_ns = qa_net_read_u64(&reader);
    frame.elapsed_ns = qa_net_read_u64(&reader); frame.time_ns = qa_net_read_u64(&reader);
    frame.phase = (qa_frame_phase)qa_net_read_u32(&reader);
    if (reader.failed || (unsigned)frame.phase > QA_FRAME_EXIT || frame.time_ns < frame.start_ns)
        return application_fail(error, QA_ERROR_FORMAT, "Native Q2 continuation source clock is invalid");
    char *map = NULL, *spawn = NULL, *entities = NULL;
    char **config = calloc(engine->configstring_count, sizeof(*config));
    application_native_q2_client *clients = calloc(257, sizeof(*clients));
    bool ok = config && clients;
    if (!ok) application_fail(error, QA_ERROR_MEMORY, "Preparing native Q2 continuation restore");
    if (ok) ok = read_text(&reader, &map, error) && read_text(&reader, &spawn, error) && read_text(&reader, &entities, error);
    for (uint32_t i = 0; ok && i < engine->configstring_count; ++i)
        ok = read_text(&reader, &config[i], error);
    for (uint32_t i = 0; ok && i < 257; ++i) {
        application_native_q2_client *client = &clients[i]; qa_bytes layout;
        ok = read_actor(&reader, actors, &client->actor, error);
        client->seat = qa_net_read_u32(&reader); uint8_t flags = qa_net_read_u8(&reader);
        client->reserved = flags & 1; client->connected = flags & 2; client->begun = flags & 4;
        client->bot = flags & 8; client->disconnect_started = flags & 16;
        client->userinfo_present = flags & 32;
        char *userinfo = NULL;
        if (ok) ok = read_text(&reader, &userinfo, error);
        size_t userinfo_limit = engine->callbacks ? sizeof(client->userinfo) : engine->profile == QA_NATIVE_Q2_GAME_API2023 ? 2048u : 512u;
        if (ok) {
            ok = strlen(userinfo) < userinfo_limit &&
                (!engine->callbacks||application_native_q2_callbacks_userinfo_validate(engine,userinfo,error))&&
                (client->userinfo_present || !*userinfo) &&
                (!client->connected || client->userinfo_present);
            if (ok) memcpy(client->userinfo, userinfo, strlen(userinfo) + 1);
        }
        free(userinfo);
        ok = ok && !(flags & ~63u) && (!client->begun || client->connected) &&
            (i || (!client->actor.registry && !flags)) &&
            (!client->connected || client->actor.registry) &&
            (!client->disconnect_started || !client->connected) &&
            client->reserved == engine->clients[i].reserved &&
            (!client->reserved || client->seat == engine->clients[i].seat) &&
            qa_net_read_bytes(&reader, 1024, &layout);
        if (ok) { memcpy(client->layout, layout.data, 1024); ok = memchr(client->layout, 0, 1024) != NULL; }
        for (size_t item = 0; ok && item < 256; ++item)
            client->inventory[item] = (int16_t)qa_net_read_u16(&reader);
        if (ok) ok = read_actor(&reader, actors, &client->protocol_fog_actor, error) &&
            qa_q2_fog_read(&reader, &client->protocol_fog) &&
            client->protocol_fog.bits == UINT16_MAX && !client->protocol_fog.time &&
            qa_actor_id_equal(client->protocol_fog_actor, client->actor);
        client->protocol_fog.bits = 0;
        for (uint32_t prior = 1; ok && client->actor.registry && prior < i; ++prior)
            if (qa_actor_id_equal(clients[prior].actor, client->actor)) ok = false;
        if (!ok && !reader.failed) application_fail(error, QA_ERROR_FORMAT, "Native Q2 continuation client record is invalid");
    }
    struct application_native_q2_attack_restore *attack = NULL;
    if (ok) {
        uint32_t extent = qa_net_read_u32(&reader); qa_bytes state;
        ok = !reader.failed && qa_net_read_bytes(&reader, extent, &state) &&
            application_native_q2_attack_restore_prepare(engine, state, &attack, error);
    }
    struct application_native_q2_combat_restore *combat = NULL; qa_bytes combat_state = {0};
    if (ok) {
        uint32_t extent = qa_net_read_u32(&reader);
        ok = !reader.failed && qa_net_read_bytes(&reader, extent, &combat_state);
    }
    application_native_q2_publication_restore *publication = NULL;
    if (ok) {
        uint32_t extent = qa_net_read_u32(&reader); qa_bytes state;
        ok = !reader.failed && qa_net_read_bytes(&reader, extent, &state) &&
            application_native_q2_publication_restore_prepare(engine, state, map_ready != 0, &publication, error);
    }
    application_native_q2_wire_engine *wire = NULL;
    if (ok) {
        uint32_t extent = qa_net_read_u32(&reader); qa_bytes state;
        ok = !reader.failed && qa_net_read_bytes(&reader, extent, &state) &&
            application_native_q2_wire_restore(engine, state, &wire, error);
        if (ok && ((map_ready && engine->profile != QA_NATIVE_Q2_CGAME_API2023) != (wire != NULL)))
            ok = application_fail(error, QA_ERROR_FORMAT, "Original Q2 Engine namespace differs from its real map lifecycle");
    }
    qa_bytes callbacks_state={0},scanner_state={0},stages_state={0};
    application_native_q2_visibility *visibility = NULL;
    if(ok) {
        uint32_t extent=qa_net_read_u32(&reader);
        ok=!reader.failed&&qa_net_read_bytes(&reader,extent,&callbacks_state)&&
            ((callbacks_state.size!=0)==(engine->callbacks!=NULL));
        if(ok) { extent=qa_net_read_u32(&reader); ok=!reader.failed&&qa_net_read_bytes(&reader,extent,&scanner_state)&&
            ((scanner_state.size!=0)==(engine->inventory_scanner!=NULL)); }
        if(ok) { extent=qa_net_read_u32(&reader); ok=!reader.failed&&qa_net_read_bytes(&reader,extent,&stages_state)&&
            ((stages_state.size!=0)==(engine->callbacks!=NULL)); }
        if(!ok&&!reader.failed) application_fail(error,QA_ERROR_FORMAT,"Native saved callback/scanner owner differs from its acquired declaration");
    }
    if (ok) {
        uint32_t extent = qa_net_read_u32(&reader); qa_bytes state;
        ok = !reader.failed && qa_net_read_bytes(&reader, extent, &state) &&
            application_native_q2_visibility_restore(engine, state, &frame, clients,
                map_ready != 0, &visibility, error);
    }
    qa_string_id map_id = 0, spawn_id = 0;
    if (ok) ok = qa_net_reader_finish(&reader);
    if (ok) {
        const qa_strings *strings=qa_session_strings(engine->provider->application->session);
        if (*map) map_id=qa_strings_find(strings,(qa_bytes){(const uint8_t *)map,strlen(map)});
        if (*spawn) spawn_id=qa_strings_find(strings,(qa_bytes){(const uint8_t *)spawn,strlen(spawn)});
        if ((*map && !map_id) || (*spawn && !spawn_id))
            ok=application_fail(error,QA_ERROR_FORMAT,"Native saved map or spawn leaves its restored string identities");
    }
    /* Deferred preparation writes only the isolated source candidate, after
     * every engine field and owned allocation has been validated. */
    if (ok) ok = application_native_q2_combat_restore_prepare(engine, combat_state, &combat, error);
    if(ok) ok=application_native_q2_callbacks_restore(engine,callbacks_state,error)&&
        (!engine->inventory_scanner||application_native_q2_inventory_scanner_restore(engine->inventory_scanner,scanner_state,error))&&
        application_native_q2_stages_restore(engine,stages_state,error);
    if (ok) {
        application_native_q2_attack_restore_commit(engine, attack); attack = NULL;
        application_native_q2_combat_restore_commit(engine, combat); combat = NULL;
        application_native_q2_publication_restore_commit(publication); publication = NULL;
        application_native_q2_wire_destroy(&engine->wire_engine);
        engine->wire_engine = wire; wire = NULL;
        application_native_q2_visibility_destroy(&engine->visibility);
        engine->visibility = visibility; visibility = NULL;
        for (uint32_t i = 0; i < engine->configstring_count; ++i) free(engine->configstrings[i]);
        free(engine->configstrings); engine->configstrings = config; config = NULL;
        memcpy(engine->clients, clients, sizeof(engine->clients));
        free(engine->entity_text); engine->entity_text = entities; entities = NULL;
        engine->world_actor = world; engine->frame = frame; engine->map_name = map_id; engine->spawn_point = spawn_id;
        engine->initialized = initialized != 0; engine->map_ready = engine->provider->map_bound = map_ready != 0;
        qa_cvars_set_server_active(engine->cvars, engine->map_ready);
        ++engine->config_revision;
        engine->hud_source_owner = 0;
    }
    if (config) { for (uint32_t i = 0; i < engine->configstring_count; ++i) free(config[i]); free(config); }
    free(clients); free(map); free(spawn); free(entities);
    application_native_q2_attack_restore_abort(attack);
    application_native_q2_combat_restore_abort(combat);
    application_native_q2_publication_restore_abort(publication);
    application_native_q2_wire_destroy(&wire);
    application_native_q2_visibility_destroy(&visibility);
    return ok;
}

#include "guest_qc_profile.h"
#include "qa/vfs_view_save.h"
#include "qa/source_save.h"
#include "qa/cvars_save.h"
#include "guest_qc_rerelease.h"
#include "control_frame.h"
#include "startup_flow.h"

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
static bool resource_path_matches(const application_qc_resource *entry)
{
    const char *path=entry->acquisition.path;
    if (!path) return false;
    if (entry->kind==QA_QC_RESOURCE_SOUND) {
        if (strncmp(path,"sound/",6)) return false;
        path+=6;
    }
    const char *name=entry->name;
    while (*name && *path) {
        if ((*name=='\\'?'/':*name)!=*path) return false;
        ++name; ++path;
    }
    return !*name && !*path;
}
static bool resource_ready(struct application_qc_state *engine,const application_qc_resource *entry,
    size_t ordinal,qa_error *error)
{
    qa_bounds b=entry->value.bounds;
    if (!entry->name || !*entry->name || entry->kind>QA_QC_RESOURCE_SOUND || !entry->value.index ||
        entry->value.index>=(engine->profile==QA_QC_RERELEASE?65536u:256u) ||
        !isfinite(b.mins.x) || !isfinite(b.mins.y) || !isfinite(b.mins.z) ||
        !isfinite(b.maxs.x) || !isfinite(b.maxs.y) || !isfinite(b.maxs.z) ||
        b.mins.x>b.maxs.x || b.mins.y>b.maxs.y || b.mins.z>b.maxs.z)
        return application_fail(error,QA_ERROR_FORMAT,"Invalid source precache metadata");
    if (entry->source) {
        qa_vfs *files=engine->provider->launch->content;
        if (entry->world_model || entry->has_inline_model || (entry->kind==QA_QC_RESOURCE_MODEL && *entry->name=='*') ||
            qa_resource_pool_find(qa_vfs_resources(files),entry->acquisition.resource_id)!=entry->source ||
            entry->acquisition.resource_id!=qa_resource_id(entry->source) || !resource_path_matches(entry))
            return application_fail(error,QA_ERROR_FORMAT,"Source precache differs from its immutable resource receipt");
        return qa_vfs_acquisition_retained(files,&entry->acquisition,error);
    }
    if (entry->kind!=QA_QC_RESOURCE_MODEL || !entry->has_inline_model || entry->acquisition.mount || entry->acquisition.resource_id ||
        entry->acquisition.path || entry->acquisition.lookup_path || entry->acquisition.link_source || entry->acquisition.link_target)
        return application_fail(error,QA_ERROR_FORMAT,"Source precache lacks its actual immutable resource");
    uint32_t model=entry->inline_model;
    if (entry->world_model) {
        const qa_application *app=engine->provider->application;
        const qa_launch_snapshot *snapshot=app->routing_snapshot?app->routing_snapshot:qa_application_launch(app);
        const qa_launch_choices *choices=snapshot?qa_launch_snapshot_choices(snapshot):NULL;
        if (model || ordinal || entry->value.index!=1 || !choices || !choices->world.map || strcmp(entry->name,choices->world.map))
            return application_fail(error,QA_ERROR_FORMAT,"World precache differs from its actual requested map");
    } else {
        if (*entry->name!='*')
            return application_fail(error,QA_ERROR_FORMAT,"Inline precache has no genuine physical model");
    }
    qa_bounds actual;
    bool ok;
    if (entry->world_model) {
        qa_bsp_model world;
        ok=qa_bsp_read_model(qa_collision_bsp(qa_world_geometry(engine->world)),0,&world,error);
        if (ok) actual=(qa_bounds){world.bounds.min,world.bounds.max};
    } else ok=qa_collision_model_bounds(qa_world_geometry(engine->world),model,&actual,error);
    return ok &&
        ((b.mins.x==actual.mins.x && b.mins.y==actual.mins.y && b.mins.z==actual.mins.z &&
          b.maxs.x==actual.maxs.x && b.maxs.y==actual.maxs.y && b.maxs.z==actual.maxs.z) ||
         application_fail(error,QA_ERROR_FORMAT,"Saved source bounds differ from their retained physical model"));
}
static void openings_free(qa_buffer *openings,size_t count)
{
    for (size_t i=0;openings && i<count;++i) qa_buffer_free(openings+i);
    free(openings);
}
static bool opening_capture(struct application_qc_state *engine,
    const application_qc_resource *entry,qa_buffer *out,qa_error *error)
{
    if (!entry->source) return true;
    qa_vfs_acquisition receipt=entry->acquisition;
    qa_source_save_io io={0};
    bool ok=qa_source_save_writer(&io,engine->services.session,error) &&
        qa_vfs_acquisition_opening_codec(&io,engine->provider->launch->content,&receipt) &&
        receipt.opening_present && qa_source_save_finish(&io,out);
    qa_source_save_dispose(&io);
    if (!ok && (!error || error->code==QA_OK))
        application_fail(error,QA_ERROR_FORMAT,"Source precache lacks its retained acquisition opening");
    return ok;
}
static bool opening_restore(qa_net_reader *reader,struct application_qc_state *engine,
    application_qc_resource *entry,qa_error *error)
{
    uint32_t size=qa_net_read_u32(reader);
    if (reader->failed || reader->bit%8 || size>qa_net_reader_remaining(reader) ||
        (entry->source ? !size : size!=0))
        return qa_net_reader_fail(reader,"Source acquisition opening exceeds its resource envelope");
    if (!size) return true;
    qa_bytes bytes={reader->bytes.data+reader->bit/8,size};
    qa_source_save_io io={0};
    bool ok=qa_source_save_reader(&io,engine->services.session,bytes,error) &&
        qa_vfs_acquisition_opening_codec(&io,engine->provider->launch->content,&entry->acquisition) &&
        entry->acquisition.opening_present && qa_source_save_finish(&io,NULL);
    qa_source_save_dispose(&io);
    if (ok) reader->bit+=(size_t)size*8;
    return ok;
}
static bool write_resource(qa_net_writer *writer,const application_qc_resource *entry,
    const qa_buffer *opening)
{
    const qa_sha256_digest *digest=qa_resource_digest(entry->source); uint8_t empty[32]={0};
    const qa_vfs_acquisition *a=&entry->acquisition;
    qa_bounds b=entry->value.bounds;
    return qa_net_write_u32(writer,entry->kind) && qa_net_write_u32(writer,entry->value.index) &&
        qa_net_write_u8(writer,entry->world_model) && write_text(writer,entry->name) &&
        qa_net_write_u64(writer,a->resource_id) && qa_net_write_u64(writer,a->mount) &&
        write_text(writer,a->path) && write_text(writer,a->lookup_path) &&
        qa_net_write_u8(writer,a->link_source!=NULL) && write_text(writer,a->link_source) &&
        qa_net_write_u8(writer,a->link_target!=NULL) && write_text(writer,a->link_target) &&
        qa_net_write_data(writer,digest?digest->bytes:empty,sizeof(empty)) &&
        qa_net_write_f32(writer,b.mins.x) && qa_net_write_f32(writer,b.mins.y) && qa_net_write_f32(writer,b.mins.z) &&
        qa_net_write_f32(writer,b.maxs.x) && qa_net_write_f32(writer,b.maxs.y) && qa_net_write_f32(writer,b.maxs.z) &&
        qa_net_write_u32(writer,(uint32_t)opening->size) && qa_net_write_data(writer,opening->data,opening->size);
}
static bool read_resource(qa_net_reader *reader,struct application_qc_state *engine,
    application_qc_resource *entry,size_t ordinal,qa_error *error)
{
    uint32_t kind=qa_net_read_u32(reader); entry->kind=(qa_qc_resource_kind)kind;
    entry->value.index=qa_net_read_u32(reader); uint8_t world=qa_net_read_u8(reader);
    entry->world_model=world!=0; entry->name=read_text(reader);
    qa_vfs_acquisition *a=&entry->acquisition;
    a->resource_id=qa_net_read_u64(reader); a->mount=qa_net_read_u64(reader);
    a->path=read_text(reader); a->lookup_path=read_text(reader);
    uint8_t source=qa_net_read_u8(reader); a->link_source=read_text(reader);
    uint8_t target=qa_net_read_u8(reader); a->link_target=read_text(reader);
    uint8_t digest[32],empty[32]={0};
    bool ok=kind<=QA_QC_RESOURCE_SOUND && world<=1 && source<=1 && target<=1 &&
        entry->name && a->path && a->lookup_path && a->link_source && a->link_target &&
        (source || !*a->link_source) && (target || !*a->link_target) &&
        qa_net_read_data(reader,digest,sizeof(digest));
    entry->value.bounds.mins.x=qa_net_read_f32(reader); entry->value.bounds.mins.y=qa_net_read_f32(reader);
    entry->value.bounds.mins.z=qa_net_read_f32(reader); entry->value.bounds.maxs.x=qa_net_read_f32(reader);
    entry->value.bounds.maxs.y=qa_net_read_f32(reader); entry->value.bounds.maxs.z=qa_net_read_f32(reader);
    if (!source) { free(a->link_source); a->link_source=NULL; }
    if (!target) { free(a->link_target); a->link_target=NULL; }
    if (!a->resource_id) {
        ok=ok && !a->mount && a->path && !*a->path && a->lookup_path && !*a->lookup_path;
        free(a->path); a->path=NULL; free(a->lookup_path); a->lookup_path=NULL;
    } else if (ok) {
        qa_resource_pool *pool=qa_vfs_resources(engine->provider->launch->content);
        entry->source=(qa_resource *)qa_resource_pool_find(pool,a->resource_id);
        if (entry->source) qa_resource_retain(entry->source); else ok=false;
    }
    const qa_sha256_digest *actual=qa_resource_digest(entry->source);
    return ok && !reader->failed && !memcmp(digest,actual?actual->bytes:empty,sizeof(digest)) &&
        opening_restore(reader,engine,entry,error) &&
        application_qc_resource_resolve_inline(entry,error) &&
        resource_ready(engine,entry,ordinal,error);
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
    bool qw = engine->profile == QA_QC_QUAKEWORLD && !engine->provider->state.qc.qualified && engine->max_clients == 32;
    if ((client->colors >> 4) > 13 || (client->colors & 15u) > 13 ||
        (client->receipt_seen && (engine->profile != QA_QC_RERELEASE || !client->connected || !client->spawned)) ||
        (!client->receipt_seen && (client->receipt_sequence || client->receipt_ordinal)) ||
        (client->prepared && (!qw || !client->connected || !client->has_parms)) ||
        (qw && client->spawned && !client->prepared) ||
        !application_qc_pending_weapon_ready(engine,client,error) ||
        !qa_qc_slot(engine->provider->state.qc.instance, slot, &binding))
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC client metadata differs from its reserved guest binding");
    const qa_actor_record *record = qa_actors_get(qa_session_actors(engine->services.session), binding.actor);
    bool owned = qw && binding.kind == QA_QC_SLOT_OWNED && record && record->owner == engine->provider->owner &&
        record->has_source && record->source_slot == slot && binding.owner == record->owner && binding.source_slot == slot;
    bool borrowed = binding.kind == QA_QC_SLOT_BORROWED && record && binding.owner == record->owner &&
        binding.source_slot == (record->has_source ? record->source_slot : 0);
    if (client->connected ? (!owned && !borrowed) || !qa_actor_id_equal(binding.actor, client->actor) :
        client->actor.registry != 0 || (qw ? !owned : binding.kind != QA_QC_SLOT_FREE))
        return application_fail(error, QA_ERROR_FORMAT, "QuakeC client differs from its full reserved source ownership");
    return true;
}
bool application_qc_capture_engine(void *opaque, qa_buffer *out, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (out == NULL || engine->has_frame || application_qc_has_source_admission(engine) ||
        engine->input_scope || engine->parked_inputs || engine->client_think_time)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC engine checkpoint requires an idle frame");
    if (!engine_console_safe(engine, error)) return false;
    if (!application_qc_capture_client_outputs(engine, error) ||
        !application_qc_callbacks_ready(engine, error)) return false;
    for (uint32_t i = 1; i <= engine->max_clients; ++i) {
        const application_qc_client *client = &engine->clients[i];
        if (!client_binding_matches(engine, i, client, error)) return false;
        bool seen = false; uint64_t sequence = 0;
        if (client->receipt_seen && (!application_control_frames_sequence(engine->provider->application,
            client->actor, &seen, &sequence) || !seen || client->receipt_sequence > sequence))
            return application_fail(error, QA_ERROR_FORMAT, "QC receipt leaves its actual admitted control sequence");
    }
    size_t capacity = 256;
    if (engine->original_extension.size>UINT32_MAX ||
        (engine->original_extension.size && (!engine->original_extension.data ||
         memchr(engine->original_extension.data,0,engine->original_extension.size))) ||
        !add_size(&capacity,engine->original_extension.size+4,error))
        return application_fail(error,QA_ERROR_FORMAT,"Invalid original source extension owner");
    if (!add_size(&capacity, ((size_t)engine->max_clients + 1) * 128, error)) return false;
    for(uint32_t i=1;i<=engine->max_clients;++i) {
        const char *item=engine->clients[i].pending_weapon?
            qa_strings_cstr(qa_session_strings(engine->services.session),engine->clients[i].pending_weapon):"";
        if(!item || !add_size(&capacity,strlen(item)+5,error)) return false;
    }
    uint32_t indices[2]={0};
    for (size_t i = 0; i < engine->resource_count; ++i) {
        application_qc_resource *entry=engine->resources+i;
        if (!resource_ready(engine,entry,i,error)) return false;
        if (entry->value.index!=++indices[entry->kind])
            return application_fail(error,QA_ERROR_FORMAT,"Source precache indices differ from physical table order");
        for (size_t j=0;j<i;++j)
            if (entry->kind==engine->resources[j].kind && !strcmp(entry->name,engine->resources[j].name))
                return application_fail(error,QA_ERROR_FORMAT,"Duplicate source precache name");
        const qa_vfs_acquisition *a=&entry->acquisition;
        if (!add_size(&capacity,strlen(entry->name)+112,error) ||
            !add_size(&capacity,a->path?strlen(a->path):0,error) ||
            !add_size(&capacity,a->lookup_path?strlen(a->lookup_path):0,error) ||
            !add_size(&capacity,a->link_source?strlen(a->link_source):0,error) ||
            !add_size(&capacity,a->link_target?strlen(a->link_target):0,error)) return false;
    }
    for (size_t i = 0; i < engine->message_count; ++i) {
        application_qc_message *message = &engine->messages[i];
        if (message->reference_count > QC_ENGINE_LIMIT / 32 ||
            !add_size(&capacity, message->size + message->reference_count * 32 + 64, error)) return false;
    }
    for (size_t i = 0; i < 64; ++i)
        if (!add_size(&capacity, (engine->lightstyles[i] ? strlen(engine->lightstyles[i]) : 0) + 4, error)) return false;
    if (engine->resource_count > UINT32_MAX || engine->message_count > UINT32_MAX)
        return application_fail(error, QA_ERROR_MEMORY, "QuakeC engine checkpoint record count overflow");
    qa_buffer rerelease={0};
    if (!application_qc_rerelease_checkpoint(engine,&rerelease,error)) return false;
    if (rerelease.size>UINT32_MAX || !add_size(&capacity,rerelease.size+4,error)) {
        qa_buffer_free(&rerelease); return false;
    }
    qa_buffer *openings=engine->resource_count?calloc(engine->resource_count,sizeof(*openings)):NULL;
    if (engine->resource_count && !openings) {
        qa_buffer_free(&rerelease);
        return application_fail(error,QA_ERROR_MEMORY,"Retaining Source acquisition opening capsules");
    }
    for (size_t i=0;i<engine->resource_count;++i)
        if (!opening_capture(engine,engine->resources+i,openings+i,error) ||
            openings[i].size>UINT32_MAX || !add_size(&capacity,openings[i].size+4,error)) {
            openings_free(openings,engine->resource_count); qa_buffer_free(&rerelease); return false;
        }
    qa_buffer registry = {0};
    bool registry_ok = qa_cvars_save_capture(engine->cvars, &registry, error);
    if (registry_ok && registry.size > QC_ENGINE_LIMIT - 4)
        registry_ok = application_fail(error, QA_ERROR_MEMORY, "QuakeC cvar registry exceeds its checkpoint limit");
    if (registry_ok) registry_ok = add_size(&capacity, registry.size + 4, error);
    if (!registry_ok) {
        openings_free(openings,engine->resource_count); qa_buffer_free(&rerelease);
        qa_buffer_free(&registry); return false;
    }
    uint8_t *data = malloc(capacity);
    if (data == NULL) { openings_free(openings,engine->resource_count); qa_buffer_free(&rerelease); qa_buffer_free(&registry); return application_fail(error, QA_ERROR_MEMORY, "Allocating QuakeC engine checkpoint"); }
    qa_net_writer writer; qa_net_writer_init(&writer, data, capacity, error);
    const qa_actor_registry *actors = qa_session_actors(engine->services.session);
    const qa_sha256_digest *declaration = qa_resource_digest(engine->provider->launch->declaration);
    uint8_t empty_digest[32] = {0};
    bool ok = qa_net_write_u32(&writer, engine->max_clients) &&
        qa_net_write_u32(&writer, engine->profile) &&
        qa_net_write_data(&writer, declaration ? declaration->bytes : empty_digest, 32) &&
        qa_net_write_u64(&writer, qa_collision_map_identity(qa_world_geometry(engine->world))) &&
        qa_net_write_u64(&writer, engine->source_time_ns) &&
        qa_net_write_f32(&writer, engine->serverflags) && qa_net_write_u8(&writer, engine->loading) &&
        qa_net_write_u8(&writer, engine->initialized) &&
        qa_net_write_u8(&writer, engine->callbacks_active) &&
        qa_net_write_u8(&writer, engine->output_channels) &&
        qa_net_write_u32(&writer, engine->check_slot) && qa_net_write_f64(&writer, engine->check_time) &&
        qa_net_write_i32(&writer, engine->check_cluster) &&
        qa_net_write_u8(&writer, engine->random.front) && qa_net_write_u8(&writer, engine->random.rear) &&
        qa_net_write_u64(&writer, engine->random.draws);
    for (size_t i = 0; ok && i < 31; ++i) ok = qa_net_write_u32(&writer, engine->random.words[i]);
    for (uint32_t i = 1; ok && i <= engine->max_clients; ++i) {
        application_qc_client *client = &engine->clients[i];
        ok = qa_net_write_u32(&writer, client->seat) && qa_net_write_u8(&writer, client->connected) &&
             qa_net_write_u8(&writer, client->spawned) && qa_net_write_u8(&writer, client->spectator) &&
             qa_net_write_u8(&writer, client->prepared) &&
             qa_net_write_u8(&writer, client->has_parms) && qa_net_write_u8(&writer, client->primary_character) &&
             qa_net_write_u8(&writer, client->colors) &&
             qa_net_write_u8(&writer, client->output_published) &&
             qa_net_write_u8(&writer, client->receipt_seen) && qa_net_write_u64(&writer, client->receipt_sequence) &&
             qa_net_write_u64(&writer, client->receipt_ordinal) &&
             write_actor(&writer, actors, client->actor) &&
             write_text(&writer,client->pending_weapon?qa_strings_cstr(qa_session_strings(engine->services.session),client->pending_weapon):"") &&
             qa_net_write_u8(&writer,client->pending_weapon_following);
        for (unsigned p = 0; ok && p < 16; ++p) ok = qa_net_write_f32(&writer, client->parms[p]);
    }
    if (ok) ok = qa_net_write_u32(&writer, (uint32_t)engine->resource_count);
    for (size_t i = 0; ok && i < engine->resource_count; ++i) ok=write_resource(&writer,engine->resources+i,openings+i);
    if (ok) ok = qa_net_write_u32(&writer, (uint32_t)registry.size) &&
        qa_net_write_data(&writer, registry.data, registry.size);
    for (size_t i = 0; ok && i < 64; ++i) ok = write_text(&writer, engine->lightstyles[i]);
    if (ok) ok=qa_net_write_u32(&writer,(uint32_t)engine->original_extension.size) &&
        qa_net_write_data(&writer,engine->original_extension.data,engine->original_extension.size);
    if (ok) ok=qa_net_write_u32(&writer,(uint32_t)rerelease.size) && qa_net_write_data(&writer,rerelease.data,rerelease.size);
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
    qa_buffer_free(&registry); qa_buffer_free(&rerelease); openings_free(openings,engine->resource_count);
    if (!ok) { free(data); return false; }
    *out = (qa_buffer){data, qa_net_writer_size(&writer)}; return true;
}
static void dispose_candidate(struct application_qc_state *candidate)
{
    for (size_t i = 0; i < candidate->resource_count; ++i) {
        qa_vfs_acquisition_dispose(&candidate->resources[i].acquisition);
        free(candidate->resources[i].name); qa_resource_release(candidate->resources[i].source);
    }
    for (size_t i = 0; i < candidate->message_count; ++i) {
        free(candidate->messages[i].data); free(candidate->messages[i].references);
    }
    for (size_t i = 0; i < 64; ++i) free(candidate->lightstyles[i]);
    qa_buffer_free(&candidate->original_extension);
    application_qc_rerelease_destroy(candidate);
    free(candidate->resources); free(candidate->messages); free(candidate->clients);
}
bool application_qc_restore_engine(void *opaque, qa_bytes bytes, qa_error *error)
{
    struct application_qc_state *engine = opaque;
    if (bytes.size > QC_ENGINE_LIMIT || engine->has_frame || application_qc_has_source_admission(engine) ||
        engine->input_scope || engine->parked_inputs || engine->client_think_time)
        return application_fail(error, QA_ERROR_ARGUMENT, "QuakeC engine restore requires an idle frame");
    if (!engine_console_safe(engine, error) || !application_qc_callbacks_ready(engine, error)) return false;
    qa_net_reader reader; qa_net_reader_init(&reader, bytes, error);
    uint8_t saved_declaration[32], empty_digest[32] = {0};
    const qa_sha256_digest *declaration = qa_resource_digest(engine->provider->launch->declaration);
    if (qa_net_read_u32(&reader) != engine->max_clients ||
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
    uint8_t callbacks_active = qa_net_read_u8(&reader);
    candidate.output_channels = qa_net_read_u8(&reader);
    candidate.check_slot = qa_net_read_u32(&reader); candidate.check_time = qa_net_read_f64(&reader);
    candidate.check_cluster = qa_net_read_i32(&reader);
    candidate.random.front = qa_net_read_u8(&reader); candidate.random.rear = qa_net_read_u8(&reader);
    candidate.random.draws = qa_net_read_u64(&reader);
    for (size_t i = 0; i < 31; ++i) candidate.random.words[i] = qa_net_read_u32(&reader);
    const struct application_qc_profile *profile = engine->provider->state.qc.qualified;
    bool ok = !reader.failed && loading <= 1 && initialized <= 1 && callbacks_active <= 1 &&
        (!callbacks_active || (initialized && !loading && profile && profile->callback_count)) &&
        candidate.check_slot <= candidate.max_clients &&
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
        uint8_t prepared = qa_net_read_u8(&reader);
        uint8_t has_parms = qa_net_read_u8(&reader);
        uint8_t primary = qa_net_read_u8(&reader);
        client->colors = qa_net_read_u8(&reader);
        uint8_t output_published = qa_net_read_u8(&reader);
        uint8_t receipt_seen = qa_net_read_u8(&reader); client->receipt_sequence = qa_net_read_u64(&reader);
        client->receipt_ordinal = qa_net_read_u64(&reader);
        ok = connected <= 1 && spawned <= connected && spectator <= 1 && prepared <= connected &&
             has_parms <= 1 && primary <= 1 && output_published <= spawned && receipt_seen <= 1 &&
             read_actor(&reader, actors, connected != 0, &client->actor);
        client->connected = connected != 0; client->spawned = spawned != 0; client->spectator = spectator != 0;
        client->prepared = prepared != 0;
        client->has_parms = has_parms != 0;
        client->primary_character = primary != 0;
        client->output_published = output_published != 0;
        client->receipt_seen = receipt_seen != 0;
        char *pending=ok?read_text(&reader):NULL;
        uint8_t following=ok?qa_net_read_u8(&reader):0;
        if(ok) {
            ok=pending && following<=1;
            if(ok && *pending) {
                client->pending_weapon=qa_strings_find(qa_session_strings(engine->services.session),
                    (qa_bytes){(const uint8_t *)pending,strlen(pending)});
                ok=client->pending_weapon!=0;
            }
            client->pending_weapon_following=following!=0;
        }
        free(pending);
        if (ok && client->connected && !client->actor.registry) ok = qa_net_reader_fail(&reader, "QuakeC connected client lacks an actor");
        for (unsigned p = 0; ok && p < 16; ++p) client->parms[p] = qa_net_read_f32(&reader);
        if (reader.failed) ok = false;
        if (ok) ok = client_binding_matches(engine, i, client, error);
        for (uint32_t earlier = 1; ok && client->connected && earlier < i; ++earlier)
            if (candidate.clients[earlier].connected &&
                (candidate.clients[earlier].seat == client->seat || qa_actor_id_equal(candidate.clients[earlier].actor, client->actor)))
                ok = qa_net_reader_fail(&reader, "Duplicate QuakeC connected client seat or actor");
    }
    if (ok) ok = application_qc_restore_client_outputs(&candidate, error);
    uint32_t resources = ok ? qa_net_read_u32(&reader) : 0;
    if (resources > (candidate.profile==QA_QC_RERELEASE?131070u:510u) || resources>qa_net_reader_remaining(&reader)/104)
        ok = qa_net_reader_fail(&reader, "QuakeC saved precache count exceeds profile limit");
    if (ok && resources) {
        candidate.resource_capacity=32;
        while (candidate.resource_capacity<resources) candidate.resource_capacity*=2;
        candidate.resources=calloc(candidate.resource_capacity,sizeof(*candidate.resources));
        if (!candidate.resources) ok=application_fail(error,QA_ERROR_MEMORY,"Retaining immutable source precache owners");
    }
    uint32_t indices[2]={0};
    for (uint32_t i = 0; ok && i < resources; ++i) {
        application_qc_resource *entry=candidate.resources+candidate.resource_count++;
        ok=read_resource(&reader,&candidate,entry,i,error) && entry->value.index==++indices[entry->kind];
        for (uint32_t j=0;ok && j<i;++j)
            if (entry->kind==candidate.resources[j].kind && !strcmp(entry->name,candidate.resources[j].name))
                ok=qa_net_reader_fail(&reader,"Duplicate saved source precache name");
    }
    qa_bytes registry = {0};
    uint32_t registry_size = ok ? qa_net_read_u32(&reader) : 0;
    if (ok) ok = registry_size && qa_net_read_bytes(&reader, registry_size, &registry);
    for (size_t i = 0; ok && i < 64; ++i) { candidate.lightstyles[i] = read_text(&reader); ok = candidate.lightstyles[i] != NULL; }
    uint32_t extension=ok?qa_net_read_u32(&reader):0;
    if (ok && (reader.failed || extension>qa_net_reader_remaining(&reader)))
        ok=qa_net_reader_fail(&reader,"Original source extension exceeds its checkpoint");
    if (ok && extension) {
        candidate.original_extension.data=malloc(extension);
        if (!candidate.original_extension.data)
            ok=application_fail(error,QA_ERROR_MEMORY,"Retaining original source extension");
        else {
            candidate.original_extension.size=extension;
            ok=qa_net_read_data(&reader,candidate.original_extension.data,extension) &&
                !memchr(candidate.original_extension.data,0,extension);
        }
    }
    uint32_t rerelease=ok?qa_net_read_u32(&reader):0;
    if (ok && (reader.failed || !rerelease || rerelease>qa_net_reader_remaining(&reader)))
        ok=qa_net_reader_fail(&reader,"Rerelease source continuation exceeds its checkpoint");
    if (ok) {
        qa_bytes state={reader.bytes.data+reader.bit/8,rerelease};
        ok=!(reader.bit%8) && application_qc_rerelease_restore(&candidate,state,error);
        if (ok) reader.bit+=(size_t)rerelease*8;
    }
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
    qa_cvars_restore *cvars = NULL;
    if (ok) ok = qa_cvars_save_prepare(engine->cvars, registry, &cvars, error) &&
        qa_cvars_save_validate(cvars, error);
    static const char *required[] = {"skill", "deathmatch", "coop", "teamplay", "sv_gravity", "sv_aim", "sv_maxspeed",
        "maxclients", "registered", "developer", "sv_cheats", "samelevel", "timelimit", "fraglimit", "gamecfg"};
    for (size_t i = 0; ok && i < sizeof(required) / sizeof(required[0]); ++i)
        if (qa_cvars_save_find(cvars, required[i]) == NULL) ok = qa_net_reader_fail(&reader, "Missing QuakeC engine cvar");
    if (ok && engine->profile == QA_QC_QUAKEWORLD && qa_cvars_save_find(cvars, "sv_phs") == NULL)
        ok = qa_net_reader_fail(&reader, "Missing QuakeWorld engine PHS cvar");
    if (ok) {
        bool previously_active = engine->callbacks_active;
        if (!callbacks_active) ok = application_qc_callbacks_suspend(engine->provider, error);
        if (ok) ok = qa_cvars_save_commit(cvars, error);
        if (ok) cvars = NULL;
        if (!ok && previously_active && !callbacks_active) {
            qa_error resume = {0};
            if (!application_qc_callbacks_register(engine->provider, &resume) && error) *error = resume;
        }
    }
    qa_cvars_save_abort(cvars);
    if (ok) {
        for (size_t i = 0; i < engine->resource_count; ++i) {
            qa_vfs_acquisition_dispose(&engine->resources[i].acquisition);
            free(engine->resources[i].name); qa_resource_release(engine->resources[i].source);
        }
        for (size_t i = 0; i < engine->message_count; ++i) { free(engine->messages[i].data); free(engine->messages[i].references); }
        for (size_t i = 0; i < 64; ++i) { free(engine->lightstyles[i]); engine->lightstyles[i] = candidate.lightstyles[i]; candidate.lightstyles[i] = NULL; }
        free(engine->resources); free(engine->messages); free(engine->clients);
        engine->resources = candidate.resources; engine->resource_count = candidate.resource_count; engine->resource_capacity = candidate.resource_capacity;
        candidate.resources = NULL; candidate.resource_count = 0;
        engine->messages = candidate.messages; engine->message_count = candidate.message_count; engine->message_capacity = candidate.message_capacity;
        candidate.messages = NULL; candidate.message_count = 0;
        engine->clients = candidate.clients; candidate.clients = NULL;
        engine->source_time_ns = candidate.source_time_ns; engine->serverflags = candidate.serverflags;
        qa_buffer_free(&engine->original_extension);
        engine->original_extension=candidate.original_extension;
        candidate.original_extension=(qa_buffer){0};
        application_qc_rerelease_destroy(engine);
        engine->rerelease=candidate.rerelease; candidate.rerelease=NULL;
        engine->npc_restore=candidate.npc_restore; candidate.npc_restore=(qa_buffer){0};
        engine->random = candidate.random;
        engine->check_slot = candidate.check_slot; engine->check_time = candidate.check_time; engine->check_cluster = candidate.check_cluster;
        engine->loading = loading != 0;
        engine->initialized = candidate.initialized;
        engine->output_channels = candidate.output_channels;
        if (engine->provider->application->operation == APPLICATION_PERSISTING)
            ok = application_startup_source_restore(engine->provider, engine->console, engine->cvars,
                &engine->command_context, error);
        if (ok && callbacks_active) ok = application_qc_callbacks_register(engine->provider, error);
    }
    dispose_candidate(&candidate);
    if (!ok && error && error->code == QA_OK) application_fail(error, QA_ERROR_FORMAT, "Invalid QuakeC engine checkpoint");
    return ok;
}

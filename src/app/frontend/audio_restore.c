#include "audio_restore.h"
#include "component_scene.h"
#include "ui_features.h"
#include "save_private.h"
#include "native_q2_save.h"
#include "native_q3_client.h"
#include "selected_effects.h"
#include "remote_q3_client.h"
#include "remote_q3_initial.h"
#include "network_initial_graph.h"
#include "music_sources.h"
#include "remote_q1_restore.h"
#include "remote_q2_restore.h"
#include "unified_media_inventory.h"
#include "qa/persistence_content.h"
#include "qa/binary.h"

typedef struct bank_owner {
    uint32_t kind, variant;
    uint64_t ordinal, identity, owner, view;
} bank_owner;
typedef struct bank_inventory {
    bank_owner *rows;
    qa_audio_bank **banks;
    size_t count;
} bank_inventory;
static void dispose(bank_inventory *inventory)
{ free(inventory->rows); free(inventory->banks); *inventory = (bank_inventory){0}; }
static bool add(bank_inventory *inventory, qa_application_content_graph *graph,
    qa_audio_bank *bank, bank_owner row, qa_error *error)
{
    if (!bank) return true;
    row.view = qa_application_content_view_id(graph, qa_audio_bank_files(bank));
    if (!row.view) return frontend_fail(error, QA_ERROR_FORMAT, "Audio bank view is outside the actual content graph");
    for (size_t i = 0; i < inventory->count; ++i)
        if (inventory->banks[i] == bank) return frontend_fail(error, QA_ERROR_FORMAT, "Audio bank repeats destructor authority");
    if (inventory->count == SIZE_MAX / sizeof(*inventory->rows) || inventory->count == SIZE_MAX / sizeof(*inventory->banks))
        return frontend_fail(error, QA_ERROR_MEMORY, "Frontend audio bank inventory overflows");
    bank_owner *rows = realloc(inventory->rows, (inventory->count + 1) * sizeof(*rows));
    if (!rows) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating actual audio bank topology");
    inventory->rows = rows;
    qa_audio_bank **banks = realloc(inventory->banks, (inventory->count + 1) * sizeof(*banks));
    if (!banks) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating actual audio bank owner array");
    inventory->banks = banks; rows[inventory->count] = row; banks[inventory->count++] = bank; return true;
}
static bool collect(qa_frontend *f, bank_inventory *inventory, qa_error *error)
{
    if (!f || !f->application || f->stepping || !frontend_native_q2_callbacks_idle(f))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Audio graph requires actual idle frontend owners");
    qa_application_content_graph *graph = qa_application_content_graph_read(f->application);
    if (!graph) return frontend_fail(error, QA_ERROR_ARGUMENT, "Audio graph requires the actual content graph lease");
    bool ok = add(inventory, graph, f->sounds, (bank_owner){.kind = 0}, error);
    for (size_t i = 0; ok && i < frontend_source_group_count(f); ++i) {
        frontend_source_group_view group;
        if (!frontend_source_group_read(f, i, &group)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Source audio owner is not fully constructed");
        ok = add(inventory, graph, group.sounds, (bank_owner){.kind = 1, .ordinal = i,
            .identity = group.identity, .owner = group.owner}, error);
    }
    for (size_t i = 0; ok && i < frontend_event_audio_owner_count(f); ++i) {
        frontend_event_audio_owner_view event;
        if (!frontend_event_audio_owner_read(f, i, &event)) return frontend_fail(error, QA_ERROR_ARGUMENT, "Event audio owner is not admitted");
        ok = add(inventory, graph, event.sounds, (bank_owner){.kind = 2, .ordinal = i,
            .owner = event.owner, .variant = event.family}, error);
    }
    for (size_t i = 0; ok && i < frontend_native_q2_owner_count(f); ++i) {
        frontend_native_q2_owner_view native;
        if (!frontend_native_q2_owner_read(f, i, &native) || native.prepared)
            return frontend_fail(error, QA_ERROR_ARGUMENT, "Native audio owner is not fully constructed");
        ok = add(inventory, graph, native.sounds, (bank_owner){.kind = 3, .ordinal = i,
            .identity = native.identity, .owner = native.owner, .variant = native.profile}, error);
    }
    for (size_t i=0;ok && i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view owner;
        ok=frontend_native_q3_read(f,i,&owner,error) && owner.sounds &&
            add(inventory,graph,owner.sounds,(bank_owner){.kind=4,.ordinal=i,
                .identity=owner.identity,.owner=owner.receiver},error);
    }
    for (size_t i=0;ok && i<frontend_selected_effects_count(f);++i) {
        frontend_selected_effects_view owner;
        ok=frontend_selected_effects_at(f,i,&owner,error) && owner.sounds &&
            add(inventory,graph,owner.sounds,(bank_owner){.kind=5,.ordinal=i,
                .identity=owner.identity,.owner=owner.provider},error);
    }
    for(size_t i=0;ok && i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources owner;
        ok=frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&owner,error) && owner.sounds &&
            add(inventory,graph,owner.sounds,(bank_owner){.kind=6,.ordinal=i,
                .identity=owner.identity,.owner=owner.domain.source.receiver.receiver},error);
    }
    frontend_network_initial_graph_view initial;
    if(ok) ok=frontend_network_initial_graph_read(f,&initial,error);
    if(ok && initial.present) {
        frontend_remote_q3_initial_view owner;
        ok=initial.parent && frontend_remote_q3_initial_read(initial.parent,&owner,error) && owner.sounds &&
            add(inventory,graph,owner.sounds,(bank_owner){.kind=7,
                .identity=owner.identity,.owner=owner.attempt.source.receiver.receiver},error);
    }
    for(size_t i=0;ok && i<frontend_music_sources_bank_count(f->music_sources);++i) {
        qa_audio_bank *bank=frontend_music_sources_bank_at(f->music_sources,i);
        ok=bank && add(inventory,graph,bank,(bank_owner){.kind=8,.ordinal=i},error);
    }
    for(size_t i=0;ok && i<frontend_remote_q1_count(f);++i) {
        frontend_remote_q1_view owner; frontend_remote_q1_media media; frontend_remote_q1 *row=frontend_remote_q1_at(f,i);
        ok=(f->source_restoring?frontend_remote_q1_import_read(row,&owner,error):frontend_remote_q1_metadata_read(row,&owner,error)) &&
            frontend_remote_q1_media_read(row,&media,error) && add(inventory,graph,media.sounds,
                (bank_owner){.kind=9,.ordinal=i,.identity=owner.map_generation,.owner=owner.domain.actor_owner},error);
    }
    for(size_t i=0;ok && i<frontend_remote_q2_count(f);++i) {
        frontend_remote_q2_view owner; frontend_remote_q2 *row=frontend_remote_q2_at(f,i);
        ok=(f->source_restoring?frontend_remote_q2_import_read(row,&owner,error):frontend_remote_q2_metadata_read(row,&owner,error)) &&
            add(inventory,graph,owner.sounds,(bank_owner){.kind=10,.ordinal=i,.identity=owner.identity},error);
    }
    size_t unified_count=0;
    if(ok) ok=frontend_unified_media_inventory_count(f,&unified_count,error);
    for(size_t i=0;ok && i<unified_count;++i) {
        frontend_unified_media *media=NULL;
        ok=frontend_unified_media_inventory_at(f,i,&media,error);
        for(size_t j=0;ok && media && j<frontend_unified_media_bank_count(media);++j) {
            frontend_unified_bank_view bank; uint64_t key;
            ok=frontend_unified_media_bank_read(media,j,&bank) && frontend_unified_media_bank_key(i,j,&key) &&
                add(inventory,graph,bank.sounds,(bank_owner){.kind=11,.ordinal=key},error);
        }
    }
    for(size_t i=0;ok && i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        ok=frontend_component_scene_metadata_read(f,i,&row,error) &&
            add(inventory,graph,row.sounds,(bank_owner){.kind=12,.ordinal=i,
                .identity=row.identity,.owner=row.receiver},error);
    }
    return ok;
}
static bool append(qa_audio_asset ***all, size_t *count, qa_audio_asset **part, size_t size, qa_error *error)
{
    if (!size) return true;
    if (size > SIZE_MAX / sizeof(**all) - *count) return frontend_fail(error, QA_ERROR_MEMORY, "Audio holder inventory overflows");
    qa_audio_asset **grown = realloc(*all, (*count + size) * sizeof(*grown));
    if (!grown) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating actual audio holders");
    memcpy(grown + *count, part, size * sizeof(*part)); *all = grown; *count += size; return true;
}
static bool holders(qa_frontend *f, qa_audio_asset ***out, size_t *count, qa_error *error)
{
    qa_audio_asset **part = NULL; size_t size = 0;
    bool ok = !f->audio || qa_audio_engine_assets_read(f->audio, &part, &size, error);
    ok = ok && append(out, count, part, size, error); free(part); part = NULL;
    for (size_t i = 0; ok && i < frontend_source_group_count(f); ++i) {
        frontend_source_group_view group;
        ok = frontend_source_group_read(f, i, &group);
        if (ok && group.assets) {
            ok = qa_q3_presentation_audio_assets_read(group.assets, &part, &size, error) && append(out, count, part, size, error);
            free(part); part = NULL;
        }
    }
    for (size_t i=0;ok && i<frontend_native_q3_count(f);++i) {
        frontend_native_q3_view owner;
        ok=frontend_native_q3_read(f,i,&owner,error) && owner.assets &&
            qa_q3_presentation_audio_assets_read(owner.assets,&part,&size,error) &&
            append(out,count,part,size,error);
        free(part); part=NULL; size=0;
    }
    for (size_t i=0;ok && i<frontend_selected_effects_count(f);++i) {
        frontend_selected_effects_view owner;
        ok=frontend_selected_effects_at(f,i,&owner,error) && owner.assets &&
            qa_q3_presentation_audio_assets_read(owner.assets,&part,&size,error) &&
            append(out,count,part,size,error);
        free(part); part=NULL; size=0;
    }
    for(size_t i=0;ok && i<frontend_remote_q3_count(f);++i) {
        frontend_remote_q3_resources owner;
        ok=frontend_remote_q3_resources_read(frontend_remote_q3_at(f,i),&owner,error) && owner.assets &&
            qa_q3_presentation_audio_assets_read(owner.assets,&part,&size,error) && append(out,count,part,size,error);
        free(part); part=NULL; size=0;
    }
    frontend_network_initial_graph_view initial;
    if(ok) ok=frontend_network_initial_graph_read(f,&initial,error);
    if(ok && initial.present) {
        frontend_remote_q3_initial_view owner;
        ok=initial.parent && frontend_remote_q3_initial_read(initial.parent,&owner,error) && owner.assets &&
            qa_q3_presentation_audio_assets_read(owner.assets,&part,&size,error) && append(out,count,part,size,error);
        free(part); part=NULL; size=0;
    }
    ok = ok && frontend_event_audio_assets_read(f, &part, &size, error) && append(out, count, part, size, error);
    free(part); part=NULL; size=0;
    for(size_t i=0;ok && i<frontend_remote_unified_count(f);++i) {
        frontend_unified_presentation_children children;
        ok=frontend_remote_unified_presentation_children_read(frontend_remote_unified_at(f,i),&children,error);
        if(ok && children.events) {
            ok=frontend_unified_events_assets_read(children.events,&part,&size,error) && append(out,count,part,size,error);
            free(part); part=NULL; size=0;
        }
        for(size_t group=0;ok && group<frontend_unified_q1_group_count(children.q1);++group)
            for(size_t n=0;ok && n<frontend_unified_q1_static_count(children.q1,group);++n) {
                uint64_t key=0; const qa_audio_asset *asset=NULL; qa_audio_mixer *mixer=NULL;
                ok=frontend_unified_q1_static_at(children.q1,group,n,&key,&asset,&mixer) && asset;
                qa_audio_asset *held=(qa_audio_asset *)asset;
                if(ok) ok=append(out,count,&held,1,error);
            }
    }
    size_t media_count=0;
    if(ok) ok=frontend_unified_media_inventory_count(f,&media_count,error);
    for(size_t i=0;ok && i<media_count;++i) {
        frontend_unified_media *media=NULL;
        ok=frontend_unified_media_inventory_at(f,i,&media,error);
        for(size_t n=0;ok && media && n<frontend_unified_media_bank_count(media);++n) {
            frontend_unified_bank_view bank;
            ok=frontend_unified_media_bank_read(media,n,&bank);
            if(ok && bank.q3_assets) {
                ok=qa_q3_presentation_audio_assets_read(bank.q3_assets,&part,&size,error) && append(out,count,part,size,error);
                free(part); part=NULL; size=0;
            }
        }
    }
    for(size_t i=0;ok && i<frontend_component_scene_count(f);++i) {
        frontend_component_scene_view row;
        ok=frontend_component_scene_metadata_read(f,i,&row,error) && row.assets &&
            qa_q3_presentation_audio_assets_read(row.assets,&part,&size,error) && append(out,count,part,size,error);
        free(part); part=NULL; size=0;
    }
    ok=ok && frontend_ui_features_assets_read(f,&part,&size,error) && append(out,count,part,size,error);
    free(part); return ok;
}
static bool header(qa_source_save_io *io, const bank_inventory *inventory)
{
    uint8_t magic[4] = {'Q','F','A','G'}; uint32_t version = 7; size_t count = inventory->count;
    if (!qa_source_save_bytes(io, magic, 4) || memcmp(magic, "QFAG", 4) ||
        !qa_source_save_u32(io, &version) || version != 7 ||
        !qa_source_save_count(io, &count, SIZE_MAX / sizeof(bank_owner)) || count != inventory->count) return false;
    for (size_t i = 0; i < count; ++i) {
        bank_owner row = inventory->rows[i];
        if (!qa_source_save_u32(io, &row.kind) || !qa_source_save_u32(io, &row.variant) ||
            !qa_source_save_u64(io, &row.ordinal) || !qa_source_save_u64(io, &row.identity) ||
            !qa_source_save_u64(io, &row.owner) || !qa_source_save_u64(io, &row.view) ||
            row.kind != inventory->rows[i].kind || row.variant != inventory->rows[i].variant ||
            row.ordinal != inventory->rows[i].ordinal || row.identity != inventory->rows[i].identity ||
            row.owner != inventory->rows[i].owner || row.view != inventory->rows[i].view) return false;
    }
    return true;
}
bool frontend_audio_banks_checkpoint(qa_frontend *f, const qa_audio_bank_checkpoint_refs *refs,
    qa_audio_asset_inventory **out_inventory, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !out_inventory || *out_inventory)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Audio graph capture requires empty outputs");
    bank_inventory banks = {0}; qa_audio_asset **assets = NULL; size_t count = 0;
    qa_audio_asset_inventory *inventory = NULL; qa_buffer saved = {0}; qa_source_save_io io = {0};
    bool ok = collect(f, &banks, error) && holders(f, &assets, &count, error) &&
        qa_audio_asset_inventory_capture(banks.banks, banks.count, assets, count, &inventory, error) &&
        qa_audio_bank_graph_checkpoint(inventory, refs, &saved, error) &&
        qa_source_save_writer(&io, qa_application_session(f->application), error) && header(&io, &banks);
    size_t size = saved.size;
    ok = ok && qa_source_save_count(&io, &size, SIZE_MAX) && qa_source_save_bytes(&io, saved.data, size) && qa_source_save_finish(&io, out);
    if (ok) { *out_inventory = inventory; inventory = NULL; }
    qa_audio_asset_inventory_destroy(inventory); qa_source_save_dispose(&io); qa_buffer_free(&saved); free(assets); dispose(&banks);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Actual audio topology is not fully qualified");
    return ok;
}
bool frontend_audio_banks_restore(qa_frontend *f, const qa_audio_bank_checkpoint_refs *refs,
    qa_bytes bytes, qa_audio_asset_inventory **out, qa_error *error)
{
    if (!out || *out) return frontend_fail(error, QA_ERROR_ARGUMENT, "Audio graph restore requires empty construction output");
    bank_inventory banks = {0}; qa_source_save_io io = {0}; size_t size = 0; qa_bytes saved = {0};
    bool ok = collect(f, &banks, error) && qa_source_save_reader(&io, qa_application_session(f->application), bytes, error) &&
        header(&io, &banks) && qa_source_save_count(&io, &size, bytes.size);
    if (ok) {
        if (size > bytes.size - io.offset) ok = false;
        else { saved = (qa_bytes){bytes.data + io.offset, size}; io.offset += size; }
    }
    ok = ok && qa_source_save_finish(&io, NULL) && qa_audio_bank_graph_restore(banks.banks, banks.count, refs, saved, out, error);
    qa_source_save_dispose(&io); dispose(&banks);
    if (!ok && error && error->code == QA_OK) frontend_fail(error, QA_ERROR_FORMAT, "Saved audio topology differs from actual prepared owners");
    return ok;
}
bool frontend_audio_asset_encode(void *context, const qa_audio_asset *asset, qa_buffer *out, qa_error *error)
{
    uint64_t index = 0;
    if (!out || out->data || out->size || !qa_audio_asset_inventory_index(context, asset, &index))
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Audio asset descriptor requires an actual inventory member and empty output");
    uint8_t *bytes = malloc(8);
    if (!bytes) return frontend_fail(error, QA_ERROR_MEMORY, "Allocating exact audio asset descriptor");
    qa_store_u64le(bytes, index); *out = (qa_buffer){bytes, 8}; return true;
}
bool frontend_audio_asset_decode(void *context, qa_bytes bytes, qa_audio_asset **out, qa_error *error)
{
    if (!out || *out || bytes.size != 8 || !bytes.data)
        return frontend_fail(error, QA_ERROR_ARGUMENT, "Audio asset descriptor requires exact bytes and empty output");
    qa_audio_asset *asset = qa_audio_asset_inventory_at(context, qa_load_u64le(bytes.data));
    if (!asset) return frontend_fail(error, QA_ERROR_FORMAT, "Audio asset ordinal is outside the actual restored graph");
    qa_audio_asset *owned = qa_audio_asset_retain(asset);
    if (!owned) return frontend_fail(error, QA_ERROR_MEMORY, "Retaining exact restored audio asset");
    *out = owned; return true;
}

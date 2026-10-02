#include "remote_q2_footsteps.h"
#include "remote_q2_private.h"
#include "remote_q2_effects_bridge.h"
#include "qa/map_sidecars.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct footstep_table { char material[16]; uint32_t count; uint64_t resources[16]; } footstep_table;
struct remote_q2_footsteps {
    frontend_q2_footstep_source source;
    unsigned calls;
    qa_map_sidecars *sidecars;
    footstep_table *tables;
    size_t count;
    char last[128];
    uint64_t last_resource;
};
static void path(const footstep_table *table, uint32_t index, char out[128])
{
    if (*table->material) snprintf(out, 128, "#sound/player/steps/%s%u.wav", table->material, index + 1);
    else snprintf(out, 128, "#sound/player/step%u.wav", index + 1);
}
static bool add(struct remote_q2_footsteps *owner, const char *material, qa_error *error)
{
    for (size_t i = 0; i < owner->count; ++i) if (!strcmp(owner->tables[i].material, material)) return true;
    if (strlen(material) >= sizeof(owner->tables[0].material) || owner->count == SIZE_MAX / sizeof(*owner->tables)) return false;
    footstep_table *tables = realloc(owner->tables, (owner->count + 1) * sizeof(*tables));
    if (!tables) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining actual Q2 material sound tables");
    owner->tables = tables; tables[owner->count] = (footstep_table){0};
    strcpy(tables[owner->count++].material, material); return true;
}
static bool table_materials(struct remote_q2_footsteps *owner, const frontend_q2_footstep_source *source, qa_error *error)
{
    if (!owner || !source || !qa_map_sidecars_current(owner->sidecars) || qa_map_sidecars_map(owner->sidecars) != source->map ||
        qa_map_sidecars_catalog(owner->sidecars) != source->catalog ||
        qa_map_sidecars_product(owner->sidecars) != source->product ||
        !qa_map_sidecars_apply_materials(owner->sidecars, source->geometry, error)) return false;
    if (!add(owner, "", error) || !add(owner, "ladder", error)) return false;
    const qa_bsp_view *bsp = qa_collision_bsp(source->geometry);
    for (size_t i = 0; i < qa_bsp_record_count(bsp, QA_BSP_TEXINFO); ++i) {
        char material_path[1040];
        if (!qa_map_sidecars_material_path(bsp, i, material_path, error)) return false;
        for (size_t j = 0; j < qa_map_sidecars_count(owner->sidecars); ++j) {
            const qa_map_sidecar *sidecar = qa_map_sidecars_at(owner->sidecars, j);
            if (!sidecar->resource || strcmp(sidecar->path, material_path)) continue;
            qa_bytes name = qa_map_sidecars_material_input(qa_resource_bytes(sidecar->resource)); char material[16] = {0};
            if (name.size) memcpy(material, name.data, name.size);
            if (*material && !add(owner, material, error)) return false;
            break;
        }
    }
    return true;
}
void remote_q2_footsteps_clear(frontend_remote_q2 *row)
{
    if (row) { qa_error error = {0}; frontend_q2_footsteps_destroy(&row->footsteps, &error); }
}
static bool source_current(void *context, const frontend_q2_footstep_source *source)
{
    frontend_remote_q2 *row = context; frontend_remote_q2_view view; qa_error error = {0};
    bool retained = row && row->importing ? frontend_remote_q2_import_read(row, &view, &error) :
        frontend_remote_q2_metadata_read(row, &view, &error);
    return retained && source && source->context == row && source->catalog == row->content.catalog &&
        source->product == row->content.selected && source->map == row->map && source->files == row->content.mounts &&
        source->geometry == row->geometry && source->sounds == row->sounds &&
        !strcmp(source->map_name, frontend_remote_q2_config(row, row->layout.models + 1)) &&
        source->current == source_current && source->trace == remote_q2_trace && source->sound == remote_q2_effect_sound;
}
static frontend_q2_footstep_source source_read(frontend_remote_q2 *row)
{
    return (frontend_q2_footstep_source){row->content.catalog, row->content.selected,
        frontend_remote_q2_config(row, row->layout.models + 1), row->map, row->content.mounts,
        row->geometry, row->sounds, row, source_current, remote_q2_trace, remote_q2_effect_sound};
}
bool frontend_q2_footsteps_destroy(frontend_q2_footsteps **owned, qa_error *error)
{
    frontend_q2_footsteps *owner = owned ? *owned : NULL;
    if (!owner) return true;
    if (owner->calls) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 footstep callback remains entered");
    qa_map_sidecars_release(owner->sidecars); free(owner->tables); free(owner); *owned = NULL; return true;
}
bool frontend_q2_footsteps_create(const frontend_q2_footstep_source *source,
    frontend_q2_footsteps **out, qa_error *error)
{
    if (!source || !out || *out || !source->catalog || !source->map_name || !source->map || !source->files ||
        !source->geometry || !source->sounds || !source->current || !source->trace || !source->sound ||
        !source->current(source->context, source)) return false;
    frontend_q2_footsteps *owner = calloc(1, sizeof(*owner));
    if (!owner) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 footstep media owner");
    *out = owner; owner->source = *source;
    if (!qa_map_sidecars_create(source->catalog, source->product, source->map_name, qa_vfs_resources(source->files),
        (qa_resource *)source->map, &owner->sidecars, error) || !table_materials(owner, source, error)) return false;
    for (size_t i = 0; i < owner->count; ++i) {
        footstep_table *table = owner->tables + i;
        for (uint32_t j = 0; j < 16; ++j) {
            char name[128]; path(table, j, name); qa_audio_asset *asset = NULL;
            if (!qa_audio_bank_register(source->sounds, name, QA_AUDIO_Q2, &asset, error)) return false;
            if (!asset) break;
            table->resources[table->count++] = qa_resource_id(qa_audio_asset_resource(asset)); qa_audio_asset_release(asset);
        }
    }
    return frontend_q2_footsteps_current(owner, source, error);
}
bool remote_q2_footsteps_prepare(frontend_remote_q2 *row, qa_error *error)
{
    if (!row || row->footsteps || !row->map || !row->geometry || !row->sounds) return false;
    if (row->layout.max_models != 8192) return true;
    frontend_q2_footstep_source source = source_read(row);
    return frontend_q2_footsteps_create(&source, &row->footsteps, error);
}
static bool surface(frontend_remote_q2 *row, const frontend_remote_q2_effects_pose *pose,
    char material[16], qa_error *error)
{
    *material = 0;
    const qa_cvar_view *setting = qa_cvars_find(row->options.domain.cvars, "cl_footsteps");
    if (row->footsteps->count <= 2 || (setting && setting->number >= 2)) return true;
    const qa_q2_entity *entity = NULL, *previous = NULL;
    for (size_t i = 0; i < row->frame.entity_count; ++i)
        if (row->frame.entities[i].number == pose->number) entity = row->frame.entities + i;
    if (!entity) return false;
    for (size_t i = 0; row->previous.valid && i < row->previous.entity_count; ++i)
        if (row->previous.entities[i].number == pose->number) previous = row->previous.entities + i;
    qa_vec3 origin = pose->origin;
    if (previous && previous->modelindex == entity->modelindex && entity->event != 6 && entity->event != 7) {
        qa_vec3 old = qa_v3(previous->origin[0], previous->origin[1], previous->origin[2]);
        qa_vec3 delta = qa_vec_sub(old, origin);
        if (fabsf(delta.x) <= 512 && fabsf(delta.y) <= 512 && fabsf(delta.z) <= 512)
            origin = qa_vec_lerp(old, origin, row->fraction);
    }
    qa_bounds box = entity->solid && entity->solid != 31 ? remote_q2_solid_bounds(row, entity->solid) :
        (qa_bounds){qa_v3(0,0,0),qa_v3(0,0,0)};
    float bottom = entity->solid && entity->solid != 31 ? box.min.z : -66;
    box.min.z = box.max.z = 0;
    qa_trace_query query = {.start = qa_vec_add(origin, qa_v3(0,0,1)),
        .shape = {QA_SHAPE_BOX, box}, .policy = {.family = QA_COLLISION_Q2, .contents_mask = 3}, .pass_actor = pose->actor};
    query.end = qa_vec_add(query.start, qa_v3(0,0,bottom - 9)); qa_trace_result trace;
    if (!remote_q2_trace(row, &query, &trace, error)) return false;
    if (trace.fraction == 1 || !trace.has_surface) return true;
    memcpy(material, trace.surface.material, 16);
    query.end = qa_vec_add(trace.end, qa_v3(0,0,1)); query.policy.contents_mask |= 56;
    if (!remote_q2_trace(row, &query, &trace, error)) return false;
    if (trace.has_surface) memcpy(material, trace.surface.material, 16);
    material[15] = 0; return true;
}
bool remote_q2_footstep(void *context, const frontend_remote_q2_effects_pose *pose, uint32_t event,
    double time, qa_builtin_random *random, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row || !pose || !random || !row->footsteps || !remote_q2_live(row, error)) return false;
    char material[16] = {0};
    if (event == 9) strcpy(material, "ladder");
    else if (!surface(row, pose, material, error)) return false;
    footstep_table *table = row->footsteps->tables;
    for (size_t i = 0; i < row->footsteps->count; ++i)
        if (!strcmp(row->footsteps->tables[i].material, material)) { table = row->footsteps->tables + i; break; }
    if (!table->count) table = row->footsteps->tables;
    if (!table->count) return true;
    uint32_t index = 0;
    if (table->count > 1) {
        uint32_t sample, threshold = (UINT32_C(0) - table->count) % table->count;
        do sample = qa_builtin_random_integer(random); while (sample < threshold);
        index = sample % table->count;
    }
    char name[128]; path(table, index, name);
    if (table->resources[index] == row->footsteps->last_resource) index = (index + 1) % table->count;
    path(table, index, name);
    qa_audio_asset *asset = qa_audio_bank_get(row->sounds, table->resources[index], QA_AUDIO_Q2);
    if (!asset) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 footstep lost its actual retained sound handle");
    if (row->frontend->audio) {
        qa_audio_play play = {.sample = qa_audio_asset_sample(asset), .asset = asset,
            .resource_id = qa_resource_id(qa_audio_asset_resource(asset)), .name = name, .family = QA_AUDIO_Q2,
            .actor = pose->number, .owner = row->identity, .audience = row->options.domain.physical_seat,
            .origin_kind = QA_AUDIO_FIXED, .origin = pose->origin, .channel = 4,
            .volume = event == 2 ? 1 : .5f, .attenuation = event == 2 ? 1 : 2,
            .server_milliseconds = time, .has_server_time = true};
        if (!qa_audio_engine_play(row->frontend->audio, &play, (int32_t)(row->sample_ns / 1000000), error)) return false;
    }
    strcpy(row->footsteps->last, name); row->footsteps->last_resource = table->resources[index]; return true;
}
bool remote_q2_footsteps_current(const frontend_remote_q2 *row, qa_error *error)
{
    if (!row || !row->footsteps) return row && (row->layout.max_models != 8192 || !row->media_ready);
    if (!qa_map_sidecars_current(row->footsteps->sidecars) || qa_map_sidecars_map(row->footsteps->sidecars) != row->map ||
        qa_map_sidecars_catalog(row->footsteps->sidecars) != row->content.catalog ||
        qa_map_sidecars_product(row->footsteps->sidecars) != row->content.selected) return false;
    for (size_t i = 0; i < row->footsteps->count; ++i) {
        const footstep_table *table = row->footsteps->tables + i;
        if (table->count > 16) return false;
        for (uint32_t j = 0; j < table->count; ++j) {
            qa_audio_asset *asset = qa_audio_bank_get(row->sounds, table->resources[j], QA_AUDIO_Q2);
            char name[128]; path(table, j, name); bool observed = false;
            if (!asset || qa_resource_pool_find(qa_vfs_resources(row->content.mounts), table->resources[j]) !=
                qa_audio_asset_resource(asset)) return false;
            for (size_t k = 0; !observed && k < qa_vfs_retained_read_count(row->content.mounts); ++k) {
                qa_vfs_read_reference receipt;
                observed = qa_vfs_retained_read_at(row->content.mounts, k, &receipt) &&
                    qa_resource_id(receipt.resource) == table->resources[j] && !strcmp(receipt.path, name + 1);
            }
            if (!observed) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 footstep sound has no actual retained path admission");
        }
        for (uint32_t j = table->count; j < 16; ++j) if (table->resources[j]) return false;
    }
    return true;
}
bool remote_q2_footsteps_visit(const frontend_remote_q2 *row, const qa_application_content_visitor *visitor, qa_error *error)
{ return !row->footsteps || qa_map_sidecars_content_visit(row->footsteps->sidecars, visitor, error); }
static bool blob(qa_source_save_io *io, qa_buffer *bytes)
{
    size_t size = bytes->size;
    if (!qa_source_save_count(io, &size, io->direction == QA_SOURCE_SAVE_READ ? io->input.size - io->offset : SIZE_MAX)) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        bytes->data = size ? malloc(size) : NULL; bytes->size = size;
        if (size && !bytes->data) return false;
    }
    return qa_source_save_bytes(io, bytes->data, size);
}
bool remote_q2_footsteps_checkpoint(const frontend_remote_q2 *row, const qa_application_content_graph *graph,
    qa_buffer *out, qa_error *error)
{
    if (!row->footsteps) return true;
    if (!remote_q2_footsteps_current(row, error)) return false;
    const struct remote_q2_footsteps *owner = row->footsteps;
    qa_buffer sidecars = {0}; qa_source_save_io io = {0}; uint32_t schema = 1; size_t count = owner->count;
    bool ok = qa_map_sidecars_checkpoint(owner->sidecars, graph, &sidecars, error) &&
        qa_source_save_writer(&io, NULL, error) && qa_source_save_u32(&io, &schema) && blob(&io, &sidecars) &&
        qa_source_save_count(&io, &count, SIZE_MAX);
    for (size_t i = 0; ok && i < count; ++i) {
        footstep_table table = owner->tables[i];
        ok = qa_source_save_bytes(&io, table.material, sizeof(table.material)) && qa_source_save_u32(&io, &table.count);
        for (size_t j = 0; ok && j < 16; ++j) ok = qa_source_save_u64(&io, table.resources + j);
    }
    char last[128]; memcpy(last, owner->last, sizeof(last));
    uint64_t last_resource = owner->last_resource;
    if (ok) ok = qa_source_save_bytes(&io, last, sizeof(last)) && qa_source_save_u64(&io, &last_resource) &&
        qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); qa_buffer_free(&sidecars); return ok;
}
bool remote_q2_footsteps_restore(frontend_remote_q2 *row, qa_application_content_graph *graph, qa_bytes bytes, qa_error *error)
{
    if (!bytes.size) return row->layout.max_models != 8192 || !row->restore_media_ready;
    if (row->footsteps || !row->importing || !row->frontend->source_restoring || !row->geometry || row->layout.max_models != 8192) return false;
    row->footsteps = calloc(1, sizeof(*row->footsteps)); if (!row->footsteps) return false;
    qa_source_save_io io = {0}; qa_buffer sidecars = {0}; uint32_t schema = 0; size_t count = 0;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && qa_source_save_u32(&io, &schema) && schema == 1 &&
        blob(&io, &sidecars) && qa_map_sidecars_create_restored(graph, (qa_bytes){sidecars.data,sidecars.size}, &row->footsteps->sidecars, error) &&
        table_materials(row, error) && qa_source_save_count(&io, &count, row->footsteps->count) && count == row->footsteps->count;
    for (size_t i = 0; ok && i < count; ++i) {
        footstep_table saved = {0};
        ok = qa_source_save_bytes(&io, saved.material, sizeof(saved.material)) && qa_source_save_u32(&io, &saved.count) &&
            saved.count <= 16 && !memcmp(saved.material, row->footsteps->tables[i].material, sizeof(saved.material));
        for (size_t j = 0; ok && j < 16; ++j) ok = qa_source_save_u64(&io, saved.resources + j) &&
            (j < saved.count ? saved.resources[j] != 0 : saved.resources[j] == 0);
        if (ok) row->footsteps->tables[i] = saved;
    }
    if (ok) ok = qa_source_save_bytes(&io, row->footsteps->last, sizeof(row->footsteps->last)) &&
        memchr(row->footsteps->last, 0, sizeof(row->footsteps->last)) &&
        qa_source_save_u64(&io, &row->footsteps->last_resource) && qa_source_save_finish(&io, NULL);
    bool found = !*row->footsteps->last && !row->footsteps->last_resource;
    for (size_t i = 0; ok && !found && i < count; ++i) for (uint32_t j = 0; j < row->footsteps->tables[i].count; ++j) {
        char name[128]; path(row->footsteps->tables + i, j, name); found = !strcmp(name, row->footsteps->last) &&
            row->footsteps->tables[i].resources[j] == row->footsteps->last_resource;
        if (found) break;
    }
    qa_source_save_dispose(&io); qa_buffer_free(&sidecars); return ok && found;
}

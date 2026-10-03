#include "remote_q2_footsteps.h"
#include "remote_q2_private.h"
#include "remote_q2_effects_bridge.h"
#include "remote_q2_restore.h"
#include "qa/map_sidecars.h"
#include "qa/source_save.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

typedef struct footstep_table { char material[16]; uint32_t count; uint64_t resources[16]; } footstep_table;
struct remote_q2_footsteps {
    frontend_q2_footstep_source source;
    char *map_name;
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
        qa_map_sidecars_product(owner->sidecars) != source->product || qa_collision_resource(source->geometry) != source->map ||
        !qa_map_sidecars_apply_materials(owner->sidecars, source->geometry, error)) return false;
    if (!add(owner, "", error) || !add(owner, "ladder", error)) return false;
    const qa_bsp_view *bsp = qa_collision_bsp(source->geometry);
    if (!bsp) return false;
    if (bsp->family != QA_BSP_Q2) return true;
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
    qa_map_sidecars_release(owner->sidecars); free(owner->tables); free(owner->map_name); free(owner); *owned = NULL; return true;
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
    owner->map_name = malloc(strlen(source->map_name) + 1);
    if (!owner->map_name) return remote_q2_fail(error, QA_ERROR_MEMORY, "Retaining Q2 footstep map identity");
    strcpy(owner->map_name, source->map_name); owner->source.map_name = owner->map_name;
    if (!qa_map_sidecars_create(source->catalog, source->product, source->map_name, qa_vfs_resources(source->files),
        source->map, &owner->sidecars, error) || !table_materials(owner, source, error)) return false;
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
    if (!remote_q2_rerelease_presentation(row)) return true;
    frontend_q2_footstep_source source = source_read(row);
    return frontend_q2_footsteps_create(&source, &row->footsteps, error);
}
static bool surface(frontend_q2_footsteps *owner, const frontend_q2_footstep_source *source,
    const frontend_q2_footstep_sample *sample,
    char material[16], qa_error *error)
{
    *material = 0;
    if (owner->count <= 2 || sample->footsteps >= 2) return true;
    qa_bounds box = sample->trace_bounds;
    box.mins.z = box.maxs.z = 0;
    qa_trace_query query = {.start = qa_vec_add(sample->pose.origin, qa_v3(0,0,1)),
        .shape = {QA_SHAPE_BOX, box}, .policy = {.family = QA_COLLISION_Q2, .contents_mask = 3}, .pass_actor = sample->pose.actor};
    query.end = qa_vec_add(query.start, qa_v3(0,0,sample->bottom - 9)); qa_trace_result trace;
    if (!source->trace(source->context, &query, &trace, error)) return false;
    if (trace.fraction == 1 || !trace.has_surface) return true;
    memcpy(material, trace.surface.material, 16);
    query.end = qa_vec_add(trace.end, qa_v3(0,0,1)); query.policy.contents_mask |= 56;
    if (!source->trace(source->context, &query, &trace, error)) return false;
    if (trace.has_surface) memcpy(material, trace.surface.material, 16);
    material[15] = 0; return true;
}
bool remote_q2_footstep(void *context, const frontend_remote_q2_effects_pose *pose, uint32_t event,
    double time, qa_builtin_random *random, qa_error *error)
{
    frontend_remote_q2 *row = context;
    if (!row || !pose || !random || !row->footsteps || !remote_q2_live(row, error)) return false;
    frontend_q2_footstep_sample sample = {.pose = *pose, .milliseconds = time, .event = event, .bottom = -66};
    const qa_cvar_view *setting = qa_cvars_find(row->options.domain.cvars, "cl_footsteps");
    sample.footsteps = setting ? (float)setting->number : 1;
    if (event == 9) {
        frontend_q2_footstep_source source = source_read(row);
        return frontend_q2_footsteps_emit(row->footsteps, &source, &sample, random, error);
    }
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
    sample.trace_bounds = box; sample.pose.origin = origin;
    if (entity->solid && entity->solid != 31) sample.bottom = box.mins.z;
    frontend_q2_footstep_source source = source_read(row);
    return frontend_q2_footsteps_emit(row->footsteps, &source, &sample, random, error);
}
bool frontend_q2_footsteps_emit(frontend_q2_footsteps *owner, const frontend_q2_footstep_source *source,
    const frontend_q2_footstep_sample *sample, qa_builtin_random *random, qa_error *error)
{
    if (!owner || !source || !sample || !random || owner->calls ||
        (sample->event != 2 && sample->event != 8 && sample->event != 9) ||
        !isfinite(sample->milliseconds) || !isfinite(sample->bottom) || !isfinite(sample->footsteps) ||
        !qa_vec_finite(sample->pose.origin) || !qa_vec_finite(sample->trace_bounds.mins) || !qa_vec_finite(sample->trace_bounds.maxs) ||
        !frontend_q2_footsteps_current(owner, source, error)) return false;
    if (!sample->footsteps) return true;
    char material[16] = {0};
    ++owner->calls;
    bool ok = true;
    if (sample->event == 9) strcpy(material, "ladder");
    else ok = surface(owner, source, sample, material, error);
    --owner->calls;
    if (!ok || !frontend_q2_footsteps_current(owner, source, error)) return false;
    footstep_table *table = owner->tables;
    for (size_t i = 0; i < owner->count; ++i)
        if (!strcmp(owner->tables[i].material, material)) { table = owner->tables + i; break; }
    if (!table->count) table = owner->tables;
    if (!table->count) return true;
    uint32_t index = 0;
    if (table->count > 1) {
        uint32_t draw, threshold = (UINT32_C(0) - table->count) % table->count;
        do draw = qa_builtin_random_integer(random); while (draw < threshold);
        index = draw % table->count;
    }
    char name[128]; path(table, index, name);
    if (table->resources[index] == owner->last_resource) index = (index + 1) % table->count;
    path(table, index, name);
    qa_audio_asset *asset = qa_audio_bank_get(source->sounds, table->resources[index], QA_AUDIO_Q2);
    if (!asset) return remote_q2_fail(error, QA_ERROR_ARGUMENT, "Q2 footstep lost its actual retained sound handle");
    ++owner->calls;
    ok = source->sound(source->context, name, sample->pose.origin, sample->pose.actor, sample->milliseconds,
        4, sample->event == 2 ? 1 : .5f, sample->event == 2 ? 1 : 2, 0, error);
    --owner->calls;
    if (!ok || !frontend_q2_footsteps_current(owner, source, error)) return false;
    strcpy(owner->last, name); owner->last_resource = table->resources[index]; return true;
}
bool frontend_q2_footsteps_current(const frontend_q2_footsteps *owner, const frontend_q2_footstep_source *source, qa_error *error)
{
    if (!owner || !source || !source->current || !source->map_name || owner->calls ||
        owner->source.catalog != source->catalog || owner->source.product != source->product ||
        owner->source.map != source->map || owner->source.files != source->files ||
        owner->source.geometry != source->geometry || owner->source.sounds != source->sounds ||
        owner->source.context != source->context || owner->source.current != source->current ||
        owner->source.trace != source->trace || owner->source.sound != source->sound ||
        !owner->map_name || strcmp(owner->map_name, source->map_name) || !source->current(source->context, source) ||
        !qa_map_sidecars_current(owner->sidecars) || qa_map_sidecars_map(owner->sidecars) != source->map ||
        qa_map_sidecars_catalog(owner->sidecars) != source->catalog ||
        qa_map_sidecars_product(owner->sidecars) != source->product || qa_collision_resource(source->geometry) != source->map) return false;
    for (size_t i = 0; i < owner->count; ++i) {
        const footstep_table *table = owner->tables + i;
        if (table->count > 16) return false;
        for (uint32_t j = 0; j < table->count; ++j) {
            qa_audio_asset *asset = qa_audio_bank_get(source->sounds, table->resources[j], QA_AUDIO_Q2);
            char name[128]; path(table, j, name); bool observed = false;
            if (!asset || qa_resource_pool_find(qa_vfs_resources(source->files), table->resources[j]) !=
                qa_audio_asset_resource(asset)) return false;
            for (size_t k = 0; !observed && k < qa_vfs_retained_read_count(source->files); ++k) {
                qa_vfs_read_reference receipt;
                observed = qa_vfs_retained_read_at(source->files, k, &receipt) &&
                    qa_resource_id(receipt.resource) == table->resources[j] && !strcmp(receipt.path, name + 1);
            }
            if (!observed) return remote_q2_fail(error, QA_ERROR_FORMAT, "Q2 footstep sound has no actual retained path admission");
        }
        for (uint32_t j = table->count; j < 16; ++j) if (table->resources[j]) return false;
    }
    return true;
}
bool remote_q2_footsteps_current(const frontend_remote_q2 *row, qa_error *error)
{
    if (!row || !row->footsteps) return row && (!remote_q2_rerelease_presentation(row) || !row->media_ready);
    frontend_q2_footstep_source source = source_read((frontend_remote_q2 *)row);
    return frontend_q2_footsteps_current(row->footsteps, &source, error);
}
bool frontend_q2_footsteps_visit(const frontend_q2_footsteps *owner, const qa_application_content_visitor *visitor, qa_error *error)
{ return !owner || (!owner->calls && qa_map_sidecars_content_visit(owner->sidecars, visitor, error)); }
bool remote_q2_footsteps_visit(const frontend_remote_q2 *row, const qa_application_content_visitor *visitor, qa_error *error)
{ return row && frontend_q2_footsteps_visit(row->footsteps, visitor, error); }
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
bool frontend_q2_footsteps_checkpoint(const frontend_q2_footsteps *owner, const frontend_q2_footstep_source *source,
    const qa_application_content_graph *graph,
    qa_buffer *out, qa_error *error)
{
    if (!owner) return true;
    if (!frontend_q2_footsteps_current(owner, source, error)) return false;
    qa_buffer sidecars = {0}; qa_source_save_io io = {0}; size_t count = owner->count;
    bool ok = qa_map_sidecars_checkpoint(owner->sidecars, graph, &sidecars, error) &&
        qa_source_save_writer(&io, NULL, error) && blob(&io, &sidecars) &&
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
bool remote_q2_footsteps_checkpoint(const frontend_remote_q2 *row, const qa_application_content_graph *graph,
    qa_buffer *out, qa_error *error)
{
    if (!row) return false;
    frontend_q2_footstep_source source = source_read((frontend_remote_q2 *)row);
    return frontend_q2_footsteps_checkpoint(row->footsteps, &source, graph, out, error);
}
bool frontend_q2_footsteps_restore(const frontend_q2_footstep_source *source, qa_application_content_graph *graph,
    qa_bytes bytes, frontend_q2_footsteps **out, qa_error *error)
{
    if (!source || !out || *out || !source->map_name || !source->catalog || !source->product || !source->map ||
        !source->files || !source->geometry || !source->sounds || !source->current || !source->trace || !source->sound ||
        !source->current(source->context, source) || !bytes.size) return false;
    frontend_q2_footsteps *owner = calloc(1, sizeof(*owner)); if (!owner) return false;
    *out = owner; owner->source = *source;
    owner->map_name = malloc(strlen(source->map_name) + 1);
    if (!owner->map_name) return false;
    strcpy(owner->map_name, source->map_name); owner->source.map_name = owner->map_name;
    qa_source_save_io io = {0}; qa_buffer sidecars = {0}; size_t count = 0;
    bool ok = qa_source_save_reader(&io, NULL, bytes, error) && blob(&io, &sidecars) && qa_map_sidecars_create_restored(graph, (qa_bytes){sidecars.data,sidecars.size}, &owner->sidecars, error) &&
        table_materials(owner, source, error) && qa_source_save_count(&io, &count, owner->count) && count == owner->count;
    for (size_t i = 0; ok && i < count; ++i) {
        footstep_table saved = {0};
        ok = qa_source_save_bytes(&io, saved.material, sizeof(saved.material)) && qa_source_save_u32(&io, &saved.count) &&
            saved.count <= 16 && !memcmp(saved.material, owner->tables[i].material, sizeof(saved.material));
        for (size_t j = 0; ok && j < 16; ++j) ok = qa_source_save_u64(&io, saved.resources + j) &&
            (j < saved.count ? saved.resources[j] != 0 : saved.resources[j] == 0);
        if (ok) owner->tables[i] = saved;
    }
    if (ok) ok = qa_source_save_bytes(&io, owner->last, sizeof(owner->last)) &&
        memchr(owner->last, 0, sizeof(owner->last)) &&
        qa_source_save_u64(&io, &owner->last_resource) && qa_source_save_finish(&io, NULL);
    bool found = !*owner->last && !owner->last_resource;
    for (size_t i = 0; ok && !found && i < count; ++i) for (uint32_t j = 0; j < owner->tables[i].count; ++j) {
        char name[128]; path(owner->tables + i, j, name); found = !strcmp(name, owner->last) &&
            owner->tables[i].resources[j] == owner->last_resource;
        if (found) break;
    }
    qa_source_save_dispose(&io); qa_buffer_free(&sidecars); return ok && found;
}
bool remote_q2_footsteps_restore(frontend_remote_q2 *row, qa_application_content_graph *graph, qa_bytes bytes, qa_error *error)
{
    if (!bytes.size) return !remote_q2_rerelease_presentation(row) || !row->restore_media_ready;
    if (row->footsteps || !row->importing || !row->frontend->source_restoring || !row->geometry || !remote_q2_rerelease_presentation(row)) return false;
    frontend_q2_footstep_source source = source_read(row);
    return frontend_q2_footsteps_restore(&source, graph, bytes, &row->footsteps, error);
}

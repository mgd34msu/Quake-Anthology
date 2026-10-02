#include "controls_private.h"
#include "qa/render_save.h"

static bool color_fields(qa_source_save_io *io, qa_scene_vec4 *color)
{
    return qa_source_save_f32(io, &color->x) && qa_source_save_f32(io, &color->y) &&
        qa_source_save_f32(io, &color->z) && qa_source_save_f32(io, &color->w);
}
static bool coordinates_fields(qa_source_save_io *io, qa_scene_vec2 *uv)
{
    return qa_source_save_f32(io, &uv->x) && qa_source_save_f32(io, &uv->y);
}
static bool retained_fields(qa_source_save_io *io, qa_material_source_scratch *source,
    const qa_render_checkpoint_refs *refs)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t key = 0;
    if (!reading && source->material && (!refs || !refs->material_encode ||
        !refs->material_encode(refs->context, source->material, &key, io->error) || !key)) return false;
    if (!qa_source_save_u64(io, &key)) return false;
    if (reading && key) {
        const qa_material *material = NULL;
        if (!refs || !refs->material_decode ||
            !refs->material_decode(refs->context, key, &material, io->error) ||
            !qa_material_retain(material, io->error)) return false;
        source->material = material;
    }
    material_source_entity *entity = &source->entity;
    qa_scene_fog *fog = &source->fog;
    uint32_t kind = fog->kind, effect = fog->effect;
    if (!qa_source_save_f32(io, &source->shader_time) || !qa_source_save_u32(io, &source->fog_index) ||
        source->fog_index > 31 || !qa_source_save_u32(io, &kind) || kind > QA_FOG_Q2 ||
        !qa_source_save_u32(io, &effect) || effect > QA_FOG_NO_EFFECT) return false;
    if (reading) { fog->kind = (qa_scene_fog_kind)kind; fog->effect = (qa_scene_fog_effect)effect; }
    if (!qa_source_save_vec3(io, &fog->color) || !qa_source_save_vec3(io, &fog->height_color) ||
        !qa_source_save_vec3(io, &fog->height_end_color) || !qa_source_save_f32(io, &fog->density) ||
        !qa_source_save_f32(io, &fog->amount) || !qa_source_save_f32(io, &fog->sky_factor) ||
        !qa_source_save_f32(io, &fog->height_density) || !qa_source_save_f32(io, &fog->height_start) ||
        !qa_source_save_f32(io, &fog->height_end) || !qa_source_save_f32(io, &fog->height_falloff) ||
        !qa_source_save_f32(io, &fog->far_depth) || !qa_source_save_bool(io, &fog->sky_drawn) ||
        !qa_source_save_f32(io, &source->fog_tc_scale) || !qa_source_save_bool(io, &source->fog_has_surface) ||
        !qa_source_save_vec3(io, &source->fog_surface.normal) || !qa_source_save_f32(io, &source->fog_surface.distance) ||
        !qa_source_save_vec3(io, &source->fog_volume_color) || !color_fields(io, &entity->color) ||
        !coordinates_fields(io, &entity->texcoord) || !qa_source_save_vec3(io, &entity->ambient) ||
        !qa_source_save_vec3(io, &entity->directed) || !qa_source_save_vec3(io, &entity->light_direction) ||
        !qa_source_save_f32(io, &entity->ambient_alpha) || !qa_source_save_f32(io, &entity->time_offset) ||
        !qa_source_save_f32(io, &entity->shadow_plane) || !qa_source_save_u32(io, &entity->number) ||
        !qa_source_save_bool(io, &entity->non_normalized_axis) || !qa_source_save_bool(io, &entity->projection_shadow)) return false;
    return true;
}
static bool source_fields(qa_source_save_io *io, qa_material_source_scratch *source, uint32_t version,
    const qa_render_checkpoint_refs *refs)
{
    if (source->entered) return false;
    if (version >= 6 && !retained_fields(io, source, refs)) return false;
    if (version >= 5 && (
        !qa_source_save_vec3(io, &source->view_origin) ||
        !qa_source_save_vec3(io, &source->local_view_origin) ||
        !qa_source_save_bool(io, &source->view_mirror) ||
        !qa_source_save_bool(io, &source->projection_2d) ||
        !qa_source_save_i32(io, &source->picture_milliseconds))) return false;
    if (!qa_source_save_count(io, &source->vertex_count, QA_SOURCE_TESS_VERTICES) ||
        !qa_source_save_count(io, &source->index_count, QA_SOURCE_TESS_INDEXES)) return false;
    for (unsigned axis = 0; version >= 5 && axis < 3; ++axis)
        if (!qa_source_save_vec3(io, &source->view_axis[axis])) return false;
    /* Inactive cells are real retained BSS, including writes preceding ERR_DROP. */
    for (size_t i = 0; i < QA_SOURCE_TESS_VERTICES; ++i) {
        qa_scene_vertex *vertex = source->vertices + i;
        if (!qa_source_save_vec3(io, &vertex->position) || !qa_source_save_vec3(io, &vertex->normal) ||
            !coordinates_fields(io, &vertex->texcoord) || !coordinates_fields(io, &vertex->lightmap) ||
            !color_fields(io, &vertex->color) || !color_fields(io, source->colors + i) ||
            !coordinates_fields(io, &source->coordinates[0][i]) ||
            !coordinates_fields(io, &source->coordinates[1][i])) return false;
    }
    for (size_t i = 0; i < QA_SOURCE_TESS_INDEXES; ++i)
        if (!qa_source_save_u32(io, source->indices + i)) return false;
    return true;
}

bool qa_render_controls_saved_fields(qa_source_save_io *io, qa_render_controls *controls, uint32_t version,
    const qa_render_checkpoint_refs *refs)
{
    /* The enclosing CPU/GL codec owns the schema version and idle boundary. */
    return !controls->ticket && qa_source_save_i32(io, &controls->values.primitives) &&
        qa_source_save_bool(io, &controls->values.compiled_vertex_arrays) && source_fields(io, &controls->source, version, refs);
}

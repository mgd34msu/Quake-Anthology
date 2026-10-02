#include "controls_private.h"
#include "qa/render_save.h"
#include "save_fields.h"

static bool color_fields(qa_source_save_io *io, qa_scene_vec4 *color)
{
    return qa_source_save_f32(io, &color->x) && qa_source_save_f32(io, &color->y) &&
        qa_source_save_f32(io, &color->z) && qa_source_save_f32(io, &color->w);
}
static bool coordinates_fields(qa_source_save_io *io, qa_scene_vec2 *uv)
{
    return qa_source_save_f32(io, &uv->x) && qa_source_save_f32(io, &uv->y);
}
static bool entity_fields(qa_source_save_io *io, material_source_entity *entity)
{
    if (!color_fields(io, &entity->color) || !coordinates_fields(io, &entity->texcoord) ||
        !qa_source_save_vec3(io, &entity->ambient) || !qa_source_save_vec3(io, &entity->directed) ||
        !qa_source_save_vec3(io, &entity->light_direction) || !qa_source_save_f32(io, &entity->ambient_alpha) ||
        !qa_source_save_f32(io, &entity->time_offset) || !qa_source_save_f32(io, &entity->shadow_plane) ||
        !qa_source_save_u32(io, &entity->number) || !qa_source_save_bool(io, &entity->non_normalized_axis) ||
        !qa_source_save_bool(io, &entity->projection_shadow)) return false;
    for (size_t i = 0; i < 16; ++i)
        if (!qa_source_save_f32(io, entity->model.m + i)) return false;
    return true;
}
static bool retained_fields(qa_source_save_io *io, qa_material_source_scratch *source,
    uint32_t version, const qa_render_checkpoint_refs *refs)
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
        !qa_source_save_vec3(io, &source->fog_volume_color) || !entity_fields(io, &source->entity) ||
        !render_save_image(io, refs, &source->lightmap)) return false;
    if (reading) {
        qa_scene_resources *owner = qa_scene_image_resource_owner(source->lightmap);
        if (owner && !qa_scene_resources_retain(owner, io->error)) return false;
        source->lightmap_owner = owner;
    }
    if (!qa_source_save_u32(io, &source->entity_count) || source->entity_count > 1022 ||
        !qa_source_save_u32(io, &source->first_scene_entity) || source->first_scene_entity > source->entity_count ||
        !qa_source_save_u32(io, &source->entity_cell) || source->entity_cell >= 1023 ||
        !qa_source_save_bool(io, &source->entity_is_cell) ||
        (source->entity_is_cell && source->entity_cell >= 1022)) return false;
    /* The last selected cell survives membership rollover and can be rewritten
     * by AddRefEntity before another sorted entity is selected. */
    for (size_t i = 0; i < 1023; ++i)
        if (!entity_fields(io, source->entities + i)) return false;
    if (version>=7 && (!qa_source_save_u32(io, &source->submitted_light_count) || source->submitted_light_count > 32 ||
        !qa_source_save_u32(io, &source->first_scene_light) ||
        source->first_scene_light > source->submitted_light_count)) return false;
    if (!qa_source_save_u32(io, &source->light_mask) ||
        !qa_source_save_count(io, &source->light_count, 32)) return false;
    for (size_t i = 0; i < 32; ++i) {
        qa_scene_light *light = source->lights + i;
        uint32_t family = light->family;
        if (!qa_source_save_vec3(io, &light->origin) || !qa_source_save_vec3(io, &light->color) ||
            !qa_source_save_vec3(io, &light->direction) || !qa_source_save_f32(io, &light->radius) ||
            !qa_source_save_f32(io, &light->minimum) || !qa_source_save_f32(io, &light->scale) ||
            !qa_source_save_f32(io, &light->cos_half_angle) || !qa_source_save_bool(io, &light->additive) ||
            !qa_source_save_bool(io, &light->spot) || !qa_source_save_bool(io, &light->casts_shadow) ||
            !qa_source_save_u64(io, &light->identity) || !qa_source_save_u64(io, &light->revision) ||
            !qa_source_save_u32(io, &light->shadow_resolution) || !qa_source_save_u32(io, &family) ||
            family > QA_SCENE_Q3) return false;
        if (reading) light->family = (qa_scene_family)family;
    }
    return true;
}
static bool source_fields(qa_source_save_io *io, qa_material_source_scratch *source, uint32_t version,
    const qa_render_checkpoint_refs *refs)
{
    if (source->entered) return false;
    if (version >= 6 && !retained_fields(io, source, version, refs)) return false;
    if (version >= 7) {
        uint64_t key = 0;
        bool reading = io->direction == QA_SOURCE_SAVE_READ;
        if (!reading && source->world && (!refs || !refs->world_encode ||
            !refs->world_encode(refs->context, source->world, &key, io->error) || !key)) return false;
        if (!qa_source_save_u64(io, &key) || !qa_source_save_f32(io, &source->far_clip)) return false;
        if (reading && key) {
            const qa_scene_world *world = NULL;
            if (!refs || !refs->world_decode || !refs->world_decode(refs->context, key, &world, io->error) ||
                !qa_scene_world_retain((qa_scene_world *)world, io->error)) return false;
            source->world = (qa_scene_world *)world;
        }
    }
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
    if (version>=6) {
        uint32_t filter=controls->source_filter;
        if (!qa_source_save_u32(io,&filter) || filter>QA_SCENE_LINEAR_MIPMAP_LINEAR ||
            !qa_source_save_bool(io,&controls->source_filter_initialized)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) controls->source_filter=(qa_scene_filter)filter;
    }
    return !controls->ticket && qa_source_save_i32(io, &controls->values.primitives) &&
        qa_source_save_bool(io, &controls->values.compiled_vertex_arrays) && source_fields(io, &controls->source, version, refs);
}

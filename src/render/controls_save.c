#include "controls_private.h"
#include "qa/render_save.h"
#include "qa/q3_source_scene_bank.h"
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
bool qa_render_source_texture_saved_fields(qa_source_save_io *io,qa_render_source_texture *texture,
    uint32_t version,const qa_render_checkpoint_refs *refs)
{
    bool reading=io->direction==QA_SOURCE_SAVE_READ;
    uint32_t count=texture->count,filter=texture->filter,wrap=texture->wrap;
    if (!qa_source_save_u32(io,&count) || count>QA_SOURCE_TEXTURE_LEVELS ||
        !qa_source_save_u32(io,&filter) || filter>QA_SCENE_LINEAR_MIPMAP_LINEAR ||
        !qa_source_save_u32(io,&wrap) || wrap>QA_SCENE_CLAMP || !color_fields(io,&texture->border) ||
        !isfinite(texture->border.x) || !isfinite(texture->border.y) ||
        !isfinite(texture->border.z) || !isfinite(texture->border.w)) return false;
    texture->filter=(qa_scene_filter)filter; texture->wrap=(qa_scene_wrap)wrap;
    if (version>=19) {
        if (!qa_source_save_bool(io,&texture->magnification_linear)) return false;
    } else if (reading) {
        texture->magnification_linear=filter==QA_SCENE_LINEAR || filter==QA_SCENE_LINEAR_MIPMAP_NEAREST ||
            filter==QA_SCENE_LINEAR_MIPMAP_LINEAR;
    }
    for (uint32_t i=0;i<count;++i) {
        uint32_t kind=texture->kinds[i];
        uint32_t format=texture->formats[i];
        if (!render_save_image(io,refs,texture->images+i) || !texture->images[i]) return false;
        if (reading) texture->count=i+1;
        if (texture->images[i]->level_count<=i || !texture->images[i]->levels ||
            !qa_source_save_u32(io,&kind) || kind>QA_SCENE_DEPTH32F ||
            !qa_source_save_u32(io,&format) || format>QA_Q3_TEXTURE_RGB4_S3TC) return false;
        texture->kinds[i]=(qa_scene_image_kind)kind;
        texture->formats[i]=(qa_q3_texture_format)format;
        qa_scene_resources *owner=qa_scene_image_resource_owner(texture->images[i]);
        if (reading) {
            if (!owner || !qa_scene_resources_retain(owner,io->error)) return false;
            texture->owners[i]=owner; texture->levels[i]=texture->images[i]->levels[i];
        } else if (!owner || owner!=texture->owners[i] ||
            texture->levels[i].width!=texture->images[i]->levels[i].width ||
            texture->levels[i].height!=texture->images[i]->levels[i].height ||
            texture->levels[i].bytes!=texture->images[i]->levels[i].bytes ||
            texture->levels[i].pixels!=(texture->pixels[i]?texture->pixels[i]:texture->images[i]->levels[i].pixels)) return false;
        if (version>=18) {
            bool modified=texture->pixels[i]!=NULL;
            if (!qa_source_save_bool(io,&modified)) return false;
            if (modified) {
                if (reading) {
                    texture->pixels[i]=malloc(texture->levels[i].bytes);
                    if (!texture->pixels[i]) {
                        qa_error_set(io->error,QA_ERROR_MEMORY,i,"Restoring modified Source texture pixels"); return false;
                    }
                    texture->levels[i].pixels=texture->pixels[i];
                }
                if (!qa_source_save_bytes(io,texture->pixels[i],texture->levels[i].bytes)) return false;
            }
        } else if (!reading && texture->pixels[i]) return false;
    }
    return true;
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
    if (version >= 9) {
        bool present = source->scene_bank != NULL;
        if (!qa_source_save_bool(io, &present)) return false;
        if (present) {
            qa_q3_source_scene_bank_refs bank_refs = {.context = refs ? refs->context : NULL,
                .assets_encode = refs ? refs->assets_encode : NULL,
                .assets_decode = refs ? refs->assets_decode : NULL};
            qa_buffer bytes = {0};
            size_t size = 0;
            bool reading = io->direction == QA_SOURCE_SAVE_READ;
            bool ok = reading || qa_q3_source_scene_bank_checkpoint(source->scene_bank, &bank_refs, &bytes, io->error);
            if (!reading) size = bytes.size;
            if (ok) ok = qa_source_save_count(io, &size, SIZE_MAX);
            if (ok && reading) {
                ok = io->offset <= io->input.size && size <= io->input.size - io->offset;
                if (ok) {
                    qa_bytes input = {io->input.data + io->offset, size};
                    ok = qa_q3_source_scene_bank_restore(input, &bank_refs, &source->scene_bank, io->error);
                    if (ok) io->offset += size;
                } else qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "Source scene bank exceeds its enclosing renderer capsule");
            } else if (ok) ok = qa_source_save_bytes(io, bytes.data, size);
            qa_buffer_free(&bytes);
            if (!ok) return false;
        }
    }
    if (version >= 6 && !retained_fields(io, source, version, refs)) return false;
    if (version >= 12 && !qa_source_save_f32(io, &source->identity_light)) return false;
    for (size_t i = 0; version >= 12 && i < 8; ++i)
        if (!qa_source_save_bytes(io, source->texts[i], sizeof(source->texts[i])) || source->texts[i][32] != 0) return false;
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
    if (version>=17 && !qa_render_source_texture_saved_fields(io,&controls->zero_texture,version,refs)) return false;
    /* The enclosing CPU/GL codec owns the schema version and idle boundary. */
    qa_render_source_attributes *attributes=&controls->attributes;
    if (version>=10) {
        if (!color_fields(io,&attributes->color) || !qa_source_save_bool(io,&attributes->color_known) ||
            !qa_source_save_bool(io,&attributes->color_array) ||
            !qa_source_save_u32(io,&attributes->texture_unit) || attributes->texture_unit>1) return false;
        for (size_t unit=0;unit<2;++unit) {
            uint32_t environment=attributes->environment[unit];
            uint32_t kind=attributes->coordinate_kind[unit];
            if (!coordinates_fields(io,attributes->coordinates+unit) ||
                !qa_source_save_bool(io,attributes->coordinates_known+unit) ||
                !qa_source_save_bool(io,attributes->coordinate_array+unit) ||
                !qa_source_save_bool(io,attributes->texture_enabled+unit) ||
                !qa_source_save_u32(io,&environment) || environment>QA_TEXTURE_REPLACE ||
                !qa_source_save_u32(io,&kind) || kind>MATERIAL_SOURCE_COORDINATES_DRAW ||
                !qa_source_save_u32(io,attributes->coordinate_bank+unit) || attributes->coordinate_bank[unit]>1) return false;
            if (io->direction==QA_SOURCE_SAVE_READ) {
                attributes->environment[unit]=(qa_scene_texture_environment)environment;
                attributes->coordinate_kind[unit]=(material_source_coordinate_kind)kind;
            }
            if (version>=12 && !qa_source_save_bool(io,attributes->actual_empty+unit)) return false;
        }
    } else if (io->direction==QA_SOURCE_SAVE_READ) qa_render_source_attributes_init(attributes);
    if (version>=16 && !color_fields(io,&attributes->zero_border)) return false;
    if (version>=20) {
        uint32_t cull=controls->source_cull_type;
        if (!qa_source_save_u32(io,&cull) || cull>QA_CULL_BACK ||
            !qa_source_save_bool(io,&controls->source_cull_valid)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) controls->source_cull_type=(qa_scene_cull)cull;
    } else if (io->direction==QA_SOURCE_SAVE_READ) controls->source_cull_valid=false;
    if (version>=6) {
        uint32_t filter=controls->source_filter;
        if (!qa_source_save_u32(io,&filter) || filter>QA_SCENE_LINEAR_MIPMAP_LINEAR ||
            !qa_source_save_bool(io,&controls->source_filter_initialized)) return false;
        if (io->direction==QA_SOURCE_SAVE_READ) controls->source_filter=(qa_scene_filter)filter;
    }
    if (version>=8 && (!qa_source_save_bool(io,&controls->source_limits_initialized) ||
        !qa_source_save_u32(io,&controls->source_max_polys) ||
        !qa_source_save_u32(io,&controls->source_max_polyverts) ||
        (controls->source_limits_initialized ? controls->source_max_polys<600 || controls->source_max_polyverts<3000 ||
            controls->source_max_polys>INT32_MAX || controls->source_max_polyverts>INT32_MAX :
            controls->source_max_polys!=0 || controls->source_max_polyverts!=0))) return false;
    if (controls->ticket || controls->image_ticket || !qa_source_save_i32(io, &controls->values.primitives) ||
        !qa_source_save_bool(io, &controls->values.compiled_vertex_arrays) ||
        !source_fields(io, &controls->source, version, refs)) return false;
    if (controls->source.scene_bank) {
        qa_q3_source_scene_membership membership;
        if (!controls->source_limits_initialized ||
            !qa_q3_source_scene_bank_membership(controls->source.scene_bank, &membership) ||
            membership.max_polygons != controls->source_max_polys || membership.max_vertices != controls->source_max_polyverts) {
            qa_error_set(io->error, QA_ERROR_FORMAT, io->offset, "Source bank differs from its actual renderer allocation limits");
            return false;
        }
    }
    return true;
}

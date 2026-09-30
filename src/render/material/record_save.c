#include "library_save_private.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

#define ENUM(field, type, maximum) do { uint32_t v = (field); \
    if (!qa_source_save_u32(io, &v) || v > (maximum)) return false; \
    if (io->direction == QA_SOURCE_SAVE_READ) (field) = (type)v; } while (0)
#define FLOAT(field) do { if (!qa_source_save_f32(io, &(field)) || !isfinite(field)) return false; } while (0)
#define BOOL(field) do { if (!qa_source_save_bool(io, &(field))) return false; } while (0)
#define VECTOR(field) do { if (!qa_source_save_vec3(io, &(field)) || !qa_vec_finite(field)) return false; } while (0)

bool qa_material_saved_text(qa_source_save_io *io, char **value)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, present = *value != NULL;
    if (!qa_source_save_bool(io, &present) || !present) return !io->failed;
    size_t size = reading ? 0 : strlen(*value);
    if (!qa_source_save_count(io, &size, reading ? io->input.size - io->offset : SIZE_MAX) || size == SIZE_MAX) return false;
    char *text = reading ? malloc(size + 1) : *value;
    if (!text) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating retained material text"); return false; }
    if (reading) *value = text;
    if (!qa_source_save_bytes(io, text, size) || memchr(text, 0, size)) return false;
    if (reading) text[size] = 0;
    return true;
}
bool qa_material_saved_image(qa_source_save_io *io, const qa_material_library_checkpoint_refs *refs,
    const qa_scene_image **image)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, present = *image != NULL;
    if (!qa_source_save_bool(io, &present) || !present) return !io->failed;
    uint64_t key = 0;
    if (!reading && (!refs->image_encode || !refs->image_encode(refs->context, *image, &key, io->error))) return false;
    if (!qa_source_save_u64(io, &key)) return false;
    if (reading) {
        const qa_scene_image *decoded = NULL;
        if (!refs->image_decode || !refs->image_decode(refs->context, key, &decoded, io->error) || !decoded) return false;
        qa_scene_image_retain(decoded); *image = decoded;
    }
    return true;
}
bool qa_material_saved_identity(qa_source_save_io *io, const qa_material_library_checkpoint_refs *refs,
    uint64_t *identity, bool world)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, present = *identity != 0;
    if (!qa_source_save_bool(io, &present) || !present) return !io->failed;
    uint64_t key = 0;
    bool (*encode)(void *, uint64_t, uint64_t *, qa_error *) = world ? refs->world_encode : refs->image_identity_encode;
    bool (*decode)(void *, uint64_t, uint64_t *, qa_error *) = world ? refs->world_decode : refs->image_identity_decode;
    if (!reading && (!encode || !encode(refs->context, *identity, &key, io->error))) return false;
    if (!qa_source_save_u64(io, &key)) return false;
    return !reading || (decode && decode(refs->context, key, identity, io->error) && *identity);
}
static bool profile(qa_source_save_io *io, qa_material_profile *p)
{
    BOOL(p->detail_textures); BOOL(p->vertex_lighting); BOOL(p->ui_fullscreen); BOOL(p->permedia2);
    BOOL(p->multitexture); BOOL(p->texture_env_add); BOOL(p->ignore_fast_path); return true;
}
static bool wave(qa_source_save_io *io, qa_material_wave *w)
{
    ENUM(w->kind, qa_material_wave_kind, QA_WAVE_NONE);
    FLOAT(w->base); FLOAT(w->amplitude); FLOAT(w->phase); FLOAT(w->frequency); return true;
}
static bool state(qa_source_save_io *io, qa_scene_state *s)
{
    ENUM(s->blend_source, qa_scene_blend, QA_BLEND_SRC_ALPHA_SATURATE);
    ENUM(s->blend_destination, qa_scene_blend, QA_BLEND_SRC_ALPHA_SATURATE);
    ENUM(s->depth_test, qa_scene_depth, QA_DEPTH_LESS); ENUM(s->alpha_test, qa_scene_alpha, QA_ALPHA_GE128);
    ENUM(s->cull, qa_scene_cull, QA_CULL_BACK);
    BOOL(s->depth_write); BOOL(s->color_write); BOOL(s->polygon_offset); BOOL(s->wireframe);
    FLOAT(s->depth_near); FLOAT(s->depth_far); FLOAT(s->offset_factor); FLOAT(s->offset_units); FLOAT(s->line_width);
    BOOL(s->stencil_enabled); ENUM(s->stencil_test, qa_scene_stencil_test, QA_STENCIL_NOTEQUAL);
    if (!qa_source_save_u32(io, &s->stencil_reference) || !qa_source_save_u32(io, &s->stencil_compare_mask) ||
        !qa_source_save_u32(io, &s->stencil_write_mask)) return false;
    ENUM(s->stencil_fail, qa_scene_stencil_op, QA_STENCIL_INVERT);
    ENUM(s->stencil_depth_fail, qa_scene_stencil_op, QA_STENCIL_INVERT);
    ENUM(s->stencil_depth_pass, qa_scene_stencil_op, QA_STENCIL_INVERT); return true;
}
static bool fog(qa_source_save_io *io, qa_scene_fog *f)
{
    ENUM(f->kind, qa_scene_fog_kind, QA_FOG_Q2); ENUM(f->effect, qa_scene_fog_effect, QA_FOG_NO_EFFECT);
    VECTOR(f->color); VECTOR(f->height_color); VECTOR(f->height_end_color);
    FLOAT(f->density); FLOAT(f->amount); FLOAT(f->sky_factor); FLOAT(f->height_density);
    FLOAT(f->height_start); FLOAT(f->height_end); FLOAT(f->height_falloff); FLOAT(f->far_depth);
    BOOL(f->sky_drawn); return true;
}
static bool stage(qa_source_save_io *io, const qa_material_library_checkpoint_refs *refs, qa_material_stage *s)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ, bundle = s->images != NULL;
    if (!state(io, &s->state) || !qa_source_save_count(io, &s->image_count, QA_MATERIAL_MAX_ANIMATION) ||
        !qa_source_save_bool(io, &bundle) || (!bundle && s->image_count)) return false;
    if (reading && bundle) {
        qa_scene_image **images = calloc(QA_MATERIAL_MAX_ANIMATION, sizeof(*images));
        char **names = calloc(QA_MATERIAL_MAX_ANIMATION, sizeof(*names));
        if (!images || !names) { free(images); free(names); qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating material image bundle"); return false; }
        s->images = images; s->image_names = names;
    }
    if (bundle) {
        if (!s->image_names) return false;
        for (unsigned i = 0; i < QA_MATERIAL_MAX_ANIMATION; ++i) {
            const qa_scene_image *image = s->images[i];
            bool ok = qa_material_saved_image(io, refs, &image);
            if (reading) s->images[i] = (qa_scene_image *)image;
            if (!ok || !qa_material_saved_text(io, &s->image_names[i]) ||
                (i >= s->image_count && (s->images[i] || s->image_names[i]))) return false;
        }
    }
    FLOAT(s->animation_frequency);
    if (!qa_material_saved_text(io, &s->video_name) || !qa_material_saved_identity(io, refs, &s->video_identity, false)) return false;
    BOOL(s->lightmap); BOOL(s->is_lightmap); BOOL(s->clamp); BOOL(s->detail); BOOL(s->video); BOOL(s->retain_texture); BOOL(s->invalid_blend);
    ENUM(s->fog_adjustment, qa_scene_fog_effect, QA_FOG_NO_EFFECT);
    ENUM(s->rgb, qa_material_color_kind, QA_COLOR_BAD); ENUM(s->alpha, qa_material_color_kind, QA_COLOR_BAD);
    FLOAT(s->constant.x); FLOAT(s->constant.y); FLOAT(s->constant.z); FLOAT(s->constant.w);
    if (!wave(io, &s->rgb_wave) || !wave(io, &s->alpha_wave)) return false;
    FLOAT(s->portal_range); ENUM(s->tcgen, qa_material_tcgen, QA_TC_BAD);
    VECTOR(s->tc_vectors[0]); VECTOR(s->tc_vectors[1]);
    size_t count = s->tcmod_count;
    if (!qa_source_save_count(io, &count, QA_MATERIAL_MAX_TCMODS)) return false;
    if (reading && count) {
        s->tcmods = calloc(count, sizeof(*s->tcmods));
        if (!s->tcmods) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating material tcmods"); return false; }
    }
    if (reading) s->tcmod_count = count;
    for (size_t i = 0; i < count; ++i) {
        qa_material_tcmod *mod = &s->tcmods[i]; ENUM(mod->kind, qa_material_tcmod_kind, QA_TCMOD_NONE);
        for (unsigned j = 0; j < 6; ++j) FLOAT(mod->values[j]);
        if (!wave(io, &mod->wave)) return false;
    }
    return true;
}
static bool options(qa_source_save_io *io, qa_material_record *record)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; qa_scene_image_options *o = &record->options;
    ENUM(o->family, qa_scene_family, QA_SCENE_Q3); ENUM(o->wrap, qa_scene_wrap, QA_SCENE_CLAMP);
    ENUM(o->filter, qa_scene_filter, QA_SCENE_LINEAR_MIPMAP_LINEAR); ENUM(o->usage, qa_scene_image_usage, QA_IMAGE_USAGE_SKY);
    BOOL(o->mipmap); BOOL(o->transparent); BOOL(o->fullbright_only);
    int32_t index = o->transparent_index;
    if (!qa_source_save_i32(io, &index)) return false;
    o->transparent_index = index;
    size_t palette = o->palette_rgb.size, translation = o->translation.size;
    if (!qa_source_save_count(io, &palette, 768) || (palette && palette != 768) ||
        !qa_source_save_count(io, &translation, 256) || (translation && translation != 256)) return false;
    if (reading) {
        record->palette = palette ? malloc(palette) : NULL; record->translation = translation ? malloc(translation) : NULL;
        if ((palette && !record->palette) || (translation && !record->translation)) {
            qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating material palette identity"); return false;
        }
        o->palette_rgb = (qa_bytes){record->palette, palette}; o->translation = (qa_bytes){record->translation, translation};
    }
    return (!palette || o->palette_rgb.data) && (!translation || o->translation.data) &&
        qa_source_save_bytes(io, (void *)o->palette_rgb.data, palette) && qa_source_save_bytes(io, (void *)o->translation.data, translation);
}
bool qa_material_saved_record(qa_source_save_io *io, const qa_material_library_checkpoint_refs *refs, qa_material_record *record)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ; qa_material *m = &record->material;
    if (!qa_material_saved_text(io, &m->name) || !m->name || !*m->name ||
        !qa_source_save_u64(io, &m->revision) || !m->revision ||
        !qa_source_save_u32(io, &m->registration) || !qa_source_save_u32(io, &m->sorted_index) ||
        !qa_source_save_i32(io, &m->lightmap_index)) return false;
    ENUM(m->family, qa_scene_family, QA_SCENE_Q3); BOOL(m->default_shader);
    if (!profile(io, &m->profile)) return false;
    FLOAT(m->sort); FLOAT(m->clamp_time); FLOAT(m->portal_range); ENUM(m->cull, qa_scene_cull, QA_CULL_BACK);
    BOOL(m->polygon_offset); BOOL(m->sky); BOOL(m->no_mipmaps); BOOL(m->no_picmip); BOOL(m->entity_mergable);
    if (!qa_source_save_u32(io, &m->surface_flags) || !qa_source_save_u32(io, &m->content_flags) || !fog(io, &m->fog)) return false;
    FLOAT(m->sky_height);
    if (!qa_material_saved_text(io, &m->sky_outer) || !qa_material_saved_text(io, &m->sky_inner)) return false;
    for (unsigned i = 0; i < 6; ++i)
        if (!qa_material_saved_image(io, refs, &m->sky_outer_images[i]) || !qa_material_saved_image(io, refs, &m->sky_inner_images[i])) return false;
    VECTOR(m->sun_light); VECTOR(m->sun_direction); BOOL(m->has_sun);
    size_t count = m->stage_count;
    if (!qa_source_save_count(io, &count, QA_MATERIAL_MAX_STAGES)) return false;
    if (reading && count) {
        m->stages = calloc(count, sizeof(*m->stages));
        if (!m->stages) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating retained material stages"); return false; }
    }
    if (reading) m->stage_count = count;
    for (size_t i = 0; i < count; ++i) if (!stage(io, refs, &m->stages[i])) return false;
    count = m->deform_count;
    if (!qa_source_save_count(io, &count, QA_MATERIAL_MAX_DEFORMS)) return false;
    if (reading && count) {
        m->deforms = calloc(count, sizeof(*m->deforms));
        if (!m->deforms) { qa_error_set(io->error, QA_ERROR_MEMORY, io->offset, "allocating retained material deforms"); return false; }
    }
    if (reading) m->deform_count = count;
    for (size_t i = 0; i < count; ++i) {
        qa_material_deform *d = &m->deforms[i]; ENUM(d->kind, qa_material_deform_kind, QA_DEFORM_NONE);
        if (!wave(io, &d->wave)) return false;
        VECTOR(d->vector); FLOAT(d->spread); FLOAT(d->width); FLOAT(d->height); FLOAT(d->speed);
        if (!qa_source_save_u32(io, &d->text_index)) return false;
    }
    FLOAT(m->remap_time_offset);
    if (!options(io, record)) return false;
    ENUM(record->kind, qa_material_registration_kind, QA_MATERIAL_STENCIL_SHADOW);
    return qa_material_saved_identity(io, refs, &record->world_identity, true) &&
        qa_source_save_i32(io, &record->lightmap_index) && qa_material_saved_text(io, &record->base_name) &&
        qa_material_saved_image(io, refs, &record->base_image);
}

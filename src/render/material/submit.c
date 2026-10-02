#include "internal.h"
#include "source_scratch_private.h"
#include "qa/scene_effects.h"
#include "qa/q3_source_scene_bank.h"
#include "qa/material_library_save.h"
#include "qa/render_controls.h"
#include <math.h>
#include <stdalign.h>
#include <string.h>
static bool source_issue(qa_material_source_scratch *, qa_scene_frame *, bool, qa_error *);
static bool source_set_2d_state(qa_material_source_scratch *source, qa_error *error)
{
    qa_scene_state state;
    qa_scene_state_default(&state);
    state.depth_test = QA_DEPTH_ALWAYS; state.depth_write = false;
    state.blend_source = QA_BLEND_SRC_ALPHA;
    state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
    return material_source_stage_state(source, &state, error) && material_source_cull_disable(source, error);
}
bool qa_material_source_no_bind_image(qa_material_source_scratch *source,
    const qa_scene_image *requested, const qa_scene_image **out, qa_error *error)
{
    const qa_scene_image *dlight;
    if (!source || !out || !qa_render_controls_source_dlight_read(source->owner, &dlight, error)) return false;
    *out = dlight ? dlight : requested; return true;
}
static void source_sort_fix_rows(material_source_submission *row, uint32_t inserted)
{
    for (; row; row = row->next) {
        uint32_t packed = row->packed_sort, shader = (packed >> 17) & 16383u;
        if (shader >= inserted) row->packed_sort = ((shader + 1) << 17) |
            ((packed >> 7) & 1023u) | (((packed >> 2) & 31u) << 2) | (packed & 3u);
    }
}
void material_source_sort_inserted(qa_material_source_scratch *source, uint32_t inserted)
{
    for (material_source_operation *operation = source->operations; operation; operation = operation->next)
        if (!operation->pictures) source_sort_fix_rows(operation->head, inserted);
}
static bool source_sort_capture(material_source_submission *row, qa_scene_frame *frame, qa_error *error)
{
    uint32_t rank;
    if (!qa_material_order_prepare(frame->material_order, error) ||
        !qa_material_order_rank(frame->material_order, row->original, &rank, error)) return false;
    row->packed_sort = (rank << 17) | (row->context.entity << 7) |
        (row->context.fog_index << 2) | (row->context.source_dlighted ? 1u : 0u);
    return true;
}
static bool stencil_material(const qa_material *material)
{
    qa_material_registration_kind kind;
    return qa_material_registration_read(material, &kind) && kind == QA_MATERIAL_STENCIL_SHADOW;
}

static bool blend_enabled(const qa_material_stage *stage)
{
    return stage->state.blend_source != QA_BLEND_ONE || stage->state.blend_destination != QA_BLEND_ZERO;
}
static bool active_stage(const qa_material_stage *stage)
{
    return stage->video || stage->lightmap || stage->retain_texture ||
           (stage->image_count != 0 && stage->images != NULL && stage->images[0] != NULL);
}
static qa_scene_vec4 attenuate_fog(qa_scene_vec4 color, qa_scene_fog_effect effect, qa_scene_vec2 uv)
{
    float attenuation = 1.0f - qa_material_fog_factor(uv.x, uv.y);
    if (effect == QA_FOG_RGB || effect == QA_FOG_RGBA) {
        color.x = truncf(roundf(color.x * 255.0f) * attenuation) / 255.0f;
        color.y = truncf(roundf(color.y * 255.0f) * attenuation) / 255.0f;
        color.z = truncf(roundf(color.z * 255.0f) * attenuation) / 255.0f;
    }
    if (effect == QA_FOG_ALPHA || effect == QA_FOG_RGBA)
        color.w = truncf(roundf(color.w * 255.0f) * attenuation) / 255.0f;
    return color;
}
static void *frame_array(qa_scene_frame *frame, size_t count, size_t size, size_t alignment, qa_error *error)
{
    if (count > SIZE_MAX / size) {
        qa_error_set(error, QA_ERROR_MEMORY, count, "Material frame allocation overflows");
        return NULL;
    }
    if (count == 0) return NULL;
    return qa_arena_alloc(&frame->storage, count * size, alignment, error);
}
static bool original_texture(const qa_material_stage *stage, const qa_material_context *context,
                    float time, const qa_scene_image **out, qa_error *error)
{
    if (stage->retain_texture) { *out = NULL; return true; }
    if (stage->video && context->video_frame != NULL && stage->video_identity != 0) {
        qa_error video_error = {0};
        *out = context->video_frame(context->video_context, stage->video_identity, context->seconds, &video_error);
        if (video_error.code != QA_OK) { if (error) *error = video_error; return false; }
        return *out != NULL || context->source_scratch != NULL;
    }
    if (stage->video && context->video_image != NULL && stage->video_name != NULL) {
        qa_error video_error = {0};
        *out = context->video_image(context->video_context, stage->video_name, context->seconds, &video_error);
        if (video_error.code != QA_OK) { if (error) *error = video_error; return false; }
        return *out != NULL || context->source_scratch != NULL;
    }
    if (stage->image_count == 0 || stage->images == NULL) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, 0, "Material stage has no registered image");
        return false;
    }
    size_t index = 0;
    if (stage->image_count > 1) {
        float value = (time * stage->animation_frequency) * 1024.0f;
        if (!isfinite(value) || (double)value < -2147483648.0 || (double)value >= 2147483648.0) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Material animation index is outside source integer range");
            return false;
        }
        int32_t integer = (int32_t)value;
        if (integer > 0) index = ((size_t)(uint32_t)integer >> 10) % stage->image_count;
    }
    if (index == 0 && stage->lightmap && context->lightmap != NULL) { *out = context->lightmap; return true; }
    *out = qa_scene_image_at_time(stage->images[index], context->seconds);
    if (*out == NULL) {
        qa_error_set(error, QA_ERROR_NOT_FOUND, index, "Material animation contains an unloaded image");
        return false;
    }
    return true;
}
static bool texture(const qa_material *material, const qa_material_stage *stage, const qa_material_context *context,
                    float time, const qa_scene_image **out, qa_error *error)
{
    if (!original_texture(stage, context, time, out, error)) return false;
    if (!*out || !context->source_recipient_image) return true;
    const qa_scene_image *image = NULL;
    bool mipmap = !material->no_mipmaps && !stage->video && !stage->is_lightmap;
    if (!context->source_recipient_image(context->source_recipient_context, *out,
        !material->no_picmip && mipmap, mipmap, &image, error)) return false;
    if (!image) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source recipient lost its reached image version");
        return false;
    }
    *out = image;
    return true;
}
static bool same_wave(qa_material_wave a, qa_material_wave b)
{
    return a.kind == b.kind && a.base == b.base && a.amplitude == b.amplitude &&
           a.phase == b.phase && a.frequency == b.frequency;
}
static bool collapse_stages(const qa_material_stage *a, const qa_material_stage *b,
                            qa_scene_texture_environment *environment, qa_scene_state *state)
{
    if (a->invalid_blend || b->invalid_blend ||
        a->state.depth_test != b->state.depth_test || a->state.alpha_test != b->state.alpha_test ||
        a->rgb != b->rgb || a->alpha != b->alpha ||
        (a->rgb == QA_COLOR_WAVE && !same_wave(a->rgb_wave, b->rgb_wave)) ||
        (a->alpha == QA_COLOR_PORTAL && !same_wave(a->alpha_wave, b->alpha_wave))) return false;
    bool a_filter = (a->state.blend_source == QA_BLEND_DST_COLOR && a->state.blend_destination == QA_BLEND_ZERO) ||
                    (a->state.blend_source == QA_BLEND_ZERO && a->state.blend_destination == QA_BLEND_SRC_COLOR);
    bool b_filter = (b->state.blend_source == QA_BLEND_DST_COLOR && b->state.blend_destination == QA_BLEND_ZERO) ||
                    (b->state.blend_source == QA_BLEND_ZERO && b->state.blend_destination == QA_BLEND_SRC_COLOR);
    bool a_add = a->state.blend_source == QA_BLEND_ONE && a->state.blend_destination == QA_BLEND_ONE;
    bool b_add = b->state.blend_source == QA_BLEND_ONE && b->state.blend_destination == QA_BLEND_ONE;
    *state = a->state;
    if (b_filter && (!blend_enabled(a) || a_filter)) {
        *environment = QA_TEXTURE_MODULATE;
        if (a_filter) { state->blend_source = QA_BLEND_DST_COLOR; state->blend_destination = QA_BLEND_ZERO; }
        return true;
    }
    if (b_add && a->rgb == QA_COLOR_IDENTITY && (!blend_enabled(a) || a_add)) {
        *environment = QA_TEXTURE_ADD;
        return true;
    }
    return false;
}
static qa_scene_cull material_cull(const qa_material *material, const qa_material_context *context)
{
    if (material->cull == QA_CULL_NONE) return QA_CULL_NONE;
    if (context->source_scratch)
        return context->mirror ? (material->cull == QA_CULL_FRONT ? QA_CULL_BACK : QA_CULL_FRONT) : material->cull;
    qa_vec3 x = qa_v3(context->model.m[0], context->model.m[1], context->model.m[2]);
    qa_vec3 y = qa_v3(context->model.m[4], context->model.m[5], context->model.m[6]);
    qa_vec3 z = qa_v3(context->model.m[8], context->model.m[9], context->model.m[10]);
    bool reversed = qa_vec_dot(qa_vec_cross(x, y), z) < 0.0f;
    if (context->mirror != reversed) return material->cull == QA_CULL_FRONT ? QA_CULL_BACK : QA_CULL_FRONT;
    return material->cull;
}
static qa_scene_draw initial_draw(const qa_material *material, const qa_material *original,
                                  qa_scene_mesh mesh, const qa_material_context *context)
{
    qa_scene_draw draw = {0};
    draw.mesh = mesh;
    draw.model = context->model;
    qa_scene_matrix view_projection = qa_scene_matrix_multiply(context->view.projection, qa_scene_view_matrix(&context->view));
    draw.mvp = qa_scene_matrix_multiply(view_projection, context->model);
    if (context->source_picture) draw.mvp = context->source_picture_projection;
    draw.environment = QA_TEXTURE_MODULATE;
    draw.lighting = QA_LIGHT_VERTEX;
    draw.shade_scale = 1.0f;
    draw.entity = context->entity;
    draw.fog_index = context->fog_index;
    draw.light_mask = context->light_mask;
    draw.source_primitives = context->source_primitives;
    draw.source_retain_depth_range = context->source_scratch != NULL;
    draw.source_retain_polygon_offset = context->source_scratch != NULL;
    draw.source_stage_state = context->source_scratch != NULL;
    draw.sort_key = ((uint64_t)original->sorted_index << 17) | ((uint64_t)context->entity << 7) |
                    ((uint64_t)context->fog_index << 2) |
                    ((context->source_scratch ? context->source_dlighted : context->light_mask != 0) ? 1u : 0u);
    qa_scene_state_default(&draw.state);
    draw.state.cull = material_cull(material, context);
    draw.state.polygon_offset = material->polygon_offset;
    draw.state.offset_factor = -1.0f;
    draw.state.offset_units = -2.0f;
    if (context->source_scratch) {
        draw.state.offset_factor = context->source_diagnostics.polygon_offset_factor;
        draw.state.offset_units = context->source_diagnostics.polygon_offset_units;
    }
    if (context->source_depth_hack) { draw.state.depth_near = 0; draw.state.depth_far = .3f; }
    if (context->source_sky_depth) draw.state.depth_near = draw.state.depth_far = context->source_diagnostics.show_sky ? 0 : 1;
    return draw;
}
static bool source_wave(qa_material_wave wave, bool noise, qa_error *error)
{
    if (wave.kind == QA_WAVE_NONE || (!noise && wave.kind == QA_WAVE_NOISE)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Source waveform has no evaluation table"); return false;
    }
    return true;
}
static bool source_colors(const qa_material_stage *stage, const qa_scene_mesh *geometry,
    const qa_material_context *context, float time, bool vertex_lit, qa_error *error)
{
    qa_material_source_scratch *source = context->source_scratch;
    qa_material_stage rgb = *stage;
    rgb.alpha = stage->rgb == QA_COLOR_CONSTANT ? QA_COLOR_CONSTANT : QA_COLOR_SKIP;
    if (rgb.rgb == QA_COLOR_WAVE && !source_wave(rgb.rgb_wave, true, error)) return false;
    for (size_t i = 0; i < geometry->vertex_count; ++i) {
        qa_scene_vec4 color;
        if (!qa_material_stage_color(&rgb, geometry->vertices + i, context, time, source->colors[i], &color, error)) return false;
        source->colors[i] = color;
    }
    if (!vertex_lit && stage->alpha != QA_COLOR_SKIP) {
        if (stage->alpha == QA_COLOR_WAVE && !source_wave(stage->alpha_wave, false, error)) return false;
        qa_material_stage alpha = *stage; alpha.rgb = QA_COLOR_SKIP;
        /* Identity's optimization tests the original RGB generator. */
        if ((stage->alpha == QA_COLOR_IDENTITY || stage->alpha == QA_COLOR_IDENTITY_LIGHTING) &&
            (stage->rgb == QA_COLOR_IDENTITY || (stage->rgb == QA_COLOR_VERTEX && context->identity_light == 1)))
            alpha.alpha = QA_COLOR_SKIP;
        for (size_t i = 0; i < geometry->vertex_count; ++i) {
            qa_scene_vec4 color;
            if (!qa_material_stage_color(&alpha, geometry->vertices + i, context, time, source->colors[i], &color, error)) return false;
            source->colors[i] = color;
        }
    }
    if (!vertex_lit && context->fog_tc_scale > 0)
        for (size_t i = 0; i < geometry->vertex_count; ++i)
            source->colors[i] = attenuate_fog(source->colors[i], stage->fog_adjustment,
                qa_material_fog_coordinates(context, geometry->vertices[i].position));
    return true;
}
static bool source_coordinates(const qa_material_stage *stage, const qa_scene_mesh *geometry,
    const qa_material_context *context, float time, unsigned bundle, qa_error *error)
{
    qa_material_source_scratch *source = context->source_scratch;
    qa_material_stage generator = *stage; generator.tcmod_count = 0;
    for (size_t i = 0; i < geometry->vertex_count; ++i) {
        qa_scene_vec2 uv;
        if (!qa_material_stage_texcoord(&generator, geometry->vertices + i, context, time, &uv, error)) return false;
        source->coordinates[bundle][i] = uv;
    }
    for (size_t mod = 0; mod < stage->tcmod_count; ++mod) {
        if (stage->tcmods[mod].kind == QA_TCMOD_NONE) break;
        if (stage->tcmods[mod].kind == QA_TCMOD_STRETCH &&
            !source_wave(stage->tcmods[mod].wave, false, error)) return false;
        qa_material_stage modifier = *stage;
        modifier.tcgen = QA_TC_TEXTURE; modifier.tcmods = stage->tcmods + mod; modifier.tcmod_count = 1;
        for (size_t i = 0; i < geometry->vertex_count; ++i) {
            qa_scene_vertex vertex = geometry->vertices[i];
            vertex.texcoord = source->coordinates[bundle][i];
            qa_scene_vec2 uv;
            if (!qa_material_stage_texcoord(&modifier, &vertex, context, time, &uv, error)) return false;
            source->coordinates[bundle][i] = uv;
        }
    }
    return true;
}
static bool emit_stage(const qa_material *material, const qa_material *original,
                        const qa_material_stage *stage, const qa_material_stage *second,
                        qa_scene_texture_environment environment, qa_scene_state state,
                        const qa_scene_mesh *geometry, const qa_material_context *context,
                        float time, qa_scene_vec4 *previous_colors, qa_material_iterator iterator,
                        qa_scene_frame *frame, qa_error *error)
{
    qa_material_source_scratch *source = context->source_scratch;
    if (source) {
        bool lightmapped = iterator == QA_MATERIAL_LIGHTMAPPED, vertex_lit = iterator == QA_MATERIAL_VERTEX_LIT;
        if (!lightmapped && (!vertex_lit || !source->issuing) &&
            !source_colors(stage, geometry, context, time, vertex_lit, error)) return false;
        if (!lightmapped && !vertex_lit && stage->tcgen != QA_TC_BAD) {
            if (!source_coordinates(stage, geometry, context, time, 0, error)) return false;
            if (second && second->tcgen != QA_TC_BAD && !source_coordinates(second, geometry, context, time, 1, error)) return false;
        }
    }
    if ((!source || second) && (stage->invalid_blend || (second != NULL && second->invalid_blend))) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Material blend function has an uninitialized source factor");
        return false;
    }
    const qa_material_stage *first_binding = stage, *second_binding = second;
    if (second != NULL && stage->is_lightmap) { first_binding = second; second_binding = stage; }
    if (source && second_binding && context->source_diagnostics.lightmap)
        environment = QA_TEXTURE_REPLACE;
    qa_scene_draw draw = initial_draw(material, original, *geometry, context);
    draw.mesh.identity = draw.mesh.revision = 0;
    draw.environment = environment;
    qa_scene_cull cull = draw.state.cull;
    draw.state = state;
    draw.state.cull = cull;
    draw.state.polygon_offset = material->polygon_offset;
    draw.state.offset_factor = -1.0f;
    draw.state.offset_units = -2.0f;
    if (context->source_scratch) {
        draw.state.offset_factor = context->source_diagnostics.polygon_offset_factor;
        draw.state.offset_units = context->source_diagnostics.polygon_offset_units;
    }
    if (context->source_depth_hack) { draw.state.depth_near = 0; draw.state.depth_far = .3f; }
    if (context->source_sky_depth) draw.state.depth_near = draw.state.depth_far = context->source_diagnostics.show_sky ? 0 : 1;
    if (source && source->issuing) {
        if (second_binding && !material_source_stage_state(source, &state, error)) return false;
        if (second_binding && !material_source_texture_select(source, 0, error)) return false;
        if (!material_source_client_coordinate_pointer(source,
            iterator == QA_MATERIAL_GENERIC || iterator == QA_MATERIAL_SKY ? MATERIAL_SOURCE_COORDINATES_STAGE : MATERIAL_SOURCE_COORDINATES_TESS,
            first_binding == stage ? 0 : 1, error)) return false;
    }
    bool vertex_lightmap = source && iterator == QA_MATERIAL_GENERIC && !second_binding &&
        first_binding->vertex_lightmap && context->source_diagnostics.lightmap &&
        (context->source_diagnostics.vertex_lighting || material->profile.permedia2);
    if (vertex_lightmap) {
        draw.textures[0] = context->source_white;
        if (!draw.textures[0]) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source vertex lightmap lost its actual white image");
            return false;
        }
    } else if (!texture(material, first_binding, context, time, &draw.textures[0], error)) return false;
    draw.retain_texture[0] = !vertex_lightmap && (first_binding->retain_texture || (source && !draw.textures[0]));
    draw.texture_count = 1;
    if (source && source->issuing && !draw.retain_texture[0]) {
        if (context->source_diagnostics.no_bind &&
            !qa_material_source_no_bind_image(source, draw.textures[0], &draw.textures[0], error)) return false;
        if (!material_source_texture_bind(source, draw.textures[0], error)) return false;
    }
    if (second_binding != NULL) {
        if (source && source->issuing && (!material_source_texture_select(source, 1, error) ||
            !material_source_texture_enable(source, true, error) ||
            !material_source_client_arrays(source, true, true, error) ||
            !material_source_texture_environment(source, environment, error) ||
            !material_source_client_coordinate_pointer(source,
                iterator == QA_MATERIAL_LIGHTMAPPED ? MATERIAL_SOURCE_COORDINATES_TESS : MATERIAL_SOURCE_COORDINATES_STAGE,
                second_binding == stage ? 0 : 1, error))) return false;
        if (!texture(material, second_binding, context, time, &draw.textures[1], error)) return false;
        draw.retain_texture[1] = second_binding->retain_texture || (source && !draw.textures[1]);
        draw.texture_count = 2;
        if (source && source->issuing && !draw.retain_texture[1]) {
            if (context->source_diagnostics.no_bind &&
                !qa_material_source_no_bind_image(source, draw.textures[1], &draw.textures[1], error)) return false;
            if (!material_source_texture_bind(source, draw.textures[1], error)) return false;
        }
    }
    if (source && (!material_source_current(source, error) || stage->invalid_blend ||
        (second && second->invalid_blend))) {
        if (!error || error->code == QA_OK)
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Material blend function has an uninitialized source factor");
        return false;
    }
    if (source && source->issuing && !second_binding && !material_source_stage_state(source, &state, error)) return false;
    if (source && context->source_diagnostics.no_bind) {
        for (size_t i = 0; i < draw.texture_count; ++i)
            if (!draw.retain_texture[i] &&
                !qa_material_source_no_bind_image(source, draw.textures[i], &draw.textures[i], error)) return false;
    }
    qa_scene_fog_effect adjustment = stage->fog_adjustment;
    if (context->fragment_lighting && (stage->is_lightmap || stage->rgb == QA_COLOR_LIGHTING_DIFFUSE)) {
        draw.lighting = QA_LIGHT_Q2_WORLD;
        draw.light_pass = stage->is_lightmap ? QA_LIGHT_PASS_MATERIAL_LIGHTMAP : QA_LIGHT_PASS_MODEL;
        draw.lights = context->fragment_lights;
        draw.light_count = context->fragment_light_count;
        draw.shadow_atlas = context->shadow_atlas;
        draw.shadow_near = context->shadow_near;
    }
    if (context->fog.kind != QA_FOG_NONE) {
        draw.fog = context->fog;
        draw.fog.effect = adjustment;
    }
    size_t storage = source ? QA_SOURCE_TESS_VERTICES : geometry->vertex_count;
    qa_scene_vertex *vertices = frame_array(frame, storage, sizeof(*vertices), alignof(qa_scene_vertex), error);
    if (storage && vertices == NULL) return false;
    for (size_t i = 0; i < storage; ++i) {
        vertices[i] = i < geometry->vertex_count ? geometry->vertices[i] : source->vertices[i];
        if (source) {
            bool lightmapped = iterator == QA_MATERIAL_LIGHTMAPPED, vertex_lit = iterator == QA_MATERIAL_VERTEX_LIT;
            vertices[i].color = lightmapped ? (qa_scene_vec4){1, 1, 1, 1} : source->colors[i];
            vertices[i].texcoord = lightmapped || vertex_lit ? vertices[i].texcoord :
                source->coordinates[first_binding == stage ? 0 : 1][i];
            if (second_binding) vertices[i].lightmap = lightmapped ? vertices[i].lightmap :
                source->coordinates[second_binding == stage ? 0 : 1][i];
            continue;
        }
        if (!qa_material_stage_color(stage, &geometry->vertices[i], context, time, previous_colors[i], &vertices[i].color, error) ||
            !qa_material_stage_texcoord(first_binding, &geometry->vertices[i], context, time, &vertices[i].texcoord, error)) return false;
        if (second_binding != NULL &&
            !qa_material_stage_texcoord(second_binding, &geometry->vertices[i], context, time, &vertices[i].lightmap, error)) return false;
        if (context->fog_tc_scale > 0.0f)
            vertices[i].color = attenuate_fog(vertices[i].color, adjustment,
                                            qa_material_fog_coordinates(context, vertices[i].position));
        previous_colors[i] = vertices[i].color;
    }
    draw.mesh.vertices = vertices;
    draw.source_arrays = source != NULL;
    draw.source_vertex_storage = source ? QA_SOURCE_TESS_VERTICES : 0;
    if (!qa_scene_frame_draw(frame, &draw, error)) return false;
    return !source || !source->issuing || !second_binding ||
        (material_source_texture_enable(source, false, error) && material_source_texture_select(source, 0, error));
}
static float dlight_byte(float component)
{
    return (float)((uint32_t)(int32_t)component & 255u) / 255.0f;
}
static bool emit_dlights(const qa_material *material, const qa_material *original,
                          const qa_scene_mesh *geometry, const qa_material_context *context,
                          bool fast_iterator, qa_scene_frame *frame, qa_error *error)
{
    if (geometry->index_count == 0 || context->light_mask == 0 || material->sort > 3.0f ||
        (!fast_iterator && (material->surface_flags & (UINT32_C(0x20000) | 4u)) != 0)) return true;
    const qa_scene_image *dlight = material->dlight_image;
    if (context->source_scratch &&
        !qa_material_source_no_bind_image(context->source_scratch, dlight, &dlight, error)) return false;
    if (dlight == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Projected light texture is unavailable");
        return false;
    }
    for (size_t i = 0; i < context->light_count; ++i) {
        if ((context->light_mask & (UINT32_C(1) << i)) == 0) continue;
        const qa_scene_light *light = &context->lights[i];
        if (!isfinite(light->radius) || light->radius <= 0.0f || !qa_vec_finite(light->color) || !qa_vec_finite(light->origin)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, i, "Projected light must have finite color/origin and positive radius");
            return false;
        }
        qa_scene_vertex *vertices = frame_array(frame, geometry->vertex_count, sizeof(*vertices), alignof(qa_scene_vertex), error);
        uint8_t *clip = frame_array(frame, geometry->vertex_count, sizeof(*clip), alignof(uint8_t), error);
        uint32_t *indices = frame_array(frame, geometry->index_count, sizeof(*indices), alignof(uint32_t), error);
        if (vertices == NULL || clip == NULL || indices == NULL) return false;
        float scale = 1.0f / light->radius;
        for (size_t v = 0; v < geometry->vertex_count; ++v) {
            vertices[v] = geometry->vertices[v];
            qa_vec3 distance = qa_vec_sub(light->origin, vertices[v].position);
            vertices[v].texcoord = (qa_scene_vec2){0.5f + distance.x * scale, 0.5f + distance.y * scale};
            qa_scene_vec2 uv = vertices[v].texcoord;
            uint8_t mask = (uint8_t)((uv.x < 0 ? 1u : uv.x > 1 ? 2u : 0u) |
                                     (uv.y < 0 ? 4u : uv.y > 1 ? 8u : 0u));
            float modulation;
            if (distance.z > light->radius) { mask |= 16u; modulation = 0; }
            else if (distance.z < -light->radius) { mask |= 32u; modulation = 0; }
            else {
                float height = fabsf(distance.z);
                modulation = height < light->radius * 0.5f ? 1.0f : (2.0f * (light->radius - height)) * scale;
            }
            qa_vec3 color = qa_vec_scale(qa_vec_scale(light->color, 255.0f), modulation);
            if (!qa_vec_finite(color) || fabsf(color.x) >= 2147483648.0f || fabsf(color.y) >= 2147483648.0f || fabsf(color.z) >= 2147483648.0f) {
                qa_error_set(error, QA_ERROR_FORMAT, i, "Projected light color exceeds source byte-conversion range");
                return false;
            }
            vertices[v].color = (qa_scene_vec4){dlight_byte(color.x), dlight_byte(color.y), dlight_byte(color.z), 1};
            clip[v] = mask;
        }
        size_t count = 0;
        for (size_t offset = 0; offset < geometry->index_count; offset += 3) {
            uint32_t a = geometry->indices[offset], b = geometry->indices[offset + 1], c = geometry->indices[offset + 2];
            if ((clip[a] & clip[b] & clip[c]) != 0) continue;
            indices[count++] = a; indices[count++] = b; indices[count++] = c;
        }
        if (count == 0) continue;
        qa_scene_draw draw = initial_draw(material, original, *geometry, context);
        draw.mesh.identity = draw.mesh.revision = 0;
        draw.mesh.vertices = vertices;
        draw.mesh.indices = indices;
        draw.mesh.index_count = count;
        draw.textures[0] = dlight;
        draw.texture_count = 1;
        draw.state.blend_source = light->additive ? QA_BLEND_ONE : QA_BLEND_DST_COLOR;
        draw.state.blend_destination = QA_BLEND_ONE;
        draw.state.depth_test = QA_DEPTH_EQUAL;
        draw.state.depth_write = false;
        if (context->fog.kind != QA_FOG_NONE) {
            draw.fog = context->fog;
            draw.fog.effect = QA_FOG_NO_EFFECT;
        }
        if (context->source_scratch && context->source_scratch->issuing) {
            if (!material_source_client_arrays(context->source_scratch, true, true, error) ||
                !material_source_client_coordinate_pointer(context->source_scratch, MATERIAL_SOURCE_COORDINATES_DRAW, 0, error) ||
                !material_source_texture_bind(context->source_scratch, draw.textures[0], error) ||
                !material_source_stage_state(context->source_scratch, &draw.state, error)) return false;
            draw.source_arrays = true;
        }
        if (!qa_scene_frame_draw(frame, &draw, error)) return false;
    }
    return true;
}
static bool emit_fog_pass(const qa_material *material, const qa_material *original,
                      const qa_scene_mesh *geometry, const qa_material_context *context,
                      bool volume, qa_scene_frame *frame, qa_error *error)
{
    bool equal = material->sort <= 3.0f;
    qa_scene_draw draw = initial_draw(material, original, *geometry, context);
    draw.mesh.identity = draw.mesh.revision = 0;
    draw.state.blend_source = QA_BLEND_SRC_ALPHA;
    draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
    draw.state.depth_test = equal ? QA_DEPTH_EQUAL : QA_DEPTH_LEQUAL;
    draw.state.depth_write = false;
    if (volume && context->source_scratch && context->source_scratch->issuing &&
        (!material_source_client_arrays(context->source_scratch, true, true, error) ||
        !material_source_client_coordinate_pointer(context->source_scratch, MATERIAL_SOURCE_COORDINATES_STAGE, 0, error))) return false;
    size_t storage = volume && context->source_scratch && context->source_scratch->issuing ?
        QA_SOURCE_TESS_VERTICES : geometry->vertex_count;
    qa_scene_vertex *vertices = frame_array(frame, storage, sizeof(*vertices), alignof(qa_scene_vertex), error);
    if (storage && vertices == NULL) return false;
    if (volume) {
        if (material->fog_image == NULL) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Fog lookup texture is unavailable");
            return false;
        }
        draw.texture_count = 1;
    draw.textures[0] = material->fog_image;
    if (context->source_scratch && context->source_diagnostics.no_bind &&
        !qa_material_source_no_bind_image(context->source_scratch, draw.textures[0], &draw.textures[0], error)) return false;
        if (context->fog.kind != QA_FOG_NONE) {
            draw.fog = context->fog;
            draw.fog.effect = QA_FOG_NO_EFFECT;
        }
    } else {
        draw.fog = context->fog;
        draw.fog.effect = QA_FOG_OVERLAY;
    }
    for (size_t i = 0; i < geometry->vertex_count; ++i) {
        vertices[i] = geometry->vertices[i];
        vertices[i].texcoord = volume ? qa_material_fog_coordinates(context, vertices[i].position) : (qa_scene_vec2){0, 0};
        vertices[i].color = volume ? (qa_scene_vec4){context->fog_volume_color.x, context->fog_volume_color.y, context->fog_volume_color.z, 1} :
            (qa_scene_vec4){1, 1, 1, 1};
        if (volume && context->source_scratch) {
            context->source_scratch->colors[i] = vertices[i].color;
            context->source_scratch->coordinates[0][i] = vertices[i].texcoord;
        }
    }
    for (size_t i = geometry->vertex_count; i < storage; ++i) {
        vertices[i] = context->source_scratch->vertices[i];
        vertices[i].color = context->source_scratch->colors[i];
        vertices[i].texcoord = context->source_scratch->coordinates[0][i];
        vertices[i].lightmap = context->source_scratch->coordinates[1][i];
    }
    draw.mesh.vertices = vertices;
    if (volume && context->source_scratch && context->source_scratch->issuing) {
        if (!material_source_texture_bind(context->source_scratch, draw.textures[0], error) ||
            !material_source_stage_state(context->source_scratch, &draw.state, error)) return false;
        draw.source_arrays = true;
        draw.source_vertex_storage = QA_SOURCE_TESS_VERTICES;
    }
    return qa_scene_frame_draw(frame, &draw, error);
}
static bool emit_fog(const qa_material *material, const qa_material *original,
                      const qa_scene_mesh *geometry, const qa_material_context *context,
                      qa_scene_frame *frame, qa_error *error)
{
    if (material->sort > 3.0f && (material->content_flags & 64u) == 0) return true;
    if (context->fog_tc_scale > 0.0f && !emit_fog_pass(material, original, geometry, context, true, frame, error)) return false;
    if (context->fog.kind != QA_FOG_NONE && !emit_fog_pass(material, original, geometry, context, false, frame, error)) return false;
    return true;
}
static bool material_plan(const qa_material *material, bool fragment_lighting,
                           qa_scene_texture_environment *environment, qa_scene_state *state,
                           size_t *passes, qa_material_iterator *iterator)
{
    bool collapsed = material->profile.multitexture && !fragment_lighting && material->stage_count >= 2 &&
        active_stage(&material->stages[0]) && active_stage(&material->stages[1]) &&
        collapse_stages(&material->stages[0], &material->stages[1], environment, state);
    if (collapsed && *environment == QA_TEXTURE_ADD && !material->profile.texture_env_add) collapsed = false;
    size_t pass_count = material->stage_count - (collapsed ? 1u : 0u);
    *passes = pass_count; *iterator = material->sky ? QA_MATERIAL_SKY : QA_MATERIAL_GENERIC;
    if (!material->profile.ignore_fast_path && !fragment_lighting && pass_count == 1 &&
        !material->sky && !material->polygon_offset && material->deform_count == 0) {
        const qa_material_stage *first = &material->stages[0];
        if (first->rgb == QA_COLOR_LIGHTING_DIFFUSE && first->alpha == QA_COLOR_IDENTITY &&
            first->tcgen == QA_TC_TEXTURE && !collapsed) *iterator = QA_MATERIAL_VERTEX_LIT;
        else if (collapsed && first->rgb == QA_COLOR_IDENTITY && first->alpha == QA_COLOR_IDENTITY &&
             ((first->tcgen == QA_TC_TEXTURE && material->stages[1].tcgen == QA_TC_LIGHTMAP) ||
              (first->tcgen == QA_TC_LIGHTMAP && material->stages[1].tcgen == QA_TC_TEXTURE))) *iterator = QA_MATERIAL_LIGHTMAPPED;
    }
    return collapsed;
}
bool qa_material_diagnostic_plan(const qa_material *material, bool fragment_lighting,
                                  size_t *passes, qa_material_iterator *iterator, qa_error *error)
{
    if (!material || !passes || !iterator) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid material diagnostic observation"); return false;
    }
    qa_scene_state state = {0}; qa_scene_texture_environment environment = QA_TEXTURE_MODULATE;
    (void)material_plan(material, fragment_lighting, &environment, &state, passes, iterator);
    return true;
}
static bool execute_material(const qa_material *material, const qa_material *original,
                              const qa_scene_mesh *geometry, const qa_material_context *context,
                              float time, qa_scene_frame *frame, qa_error *error)
{
    qa_scene_vec4 *previous = context->source_scratch ? context->source_scratch->colors :
        frame_array(frame, geometry->vertex_count, sizeof(*previous), alignof(qa_scene_vec4), error);
    if (previous == NULL) return false;
    if (!context->source_scratch) memset(previous, 0, geometry->vertex_count * sizeof(*previous));
    qa_scene_state collapsed_state = {0}; qa_scene_texture_environment collapsed_environment = QA_TEXTURE_MODULATE;
    size_t passes; qa_material_iterator iterator;
    bool collapsed = material_plan(material, context->fragment_lighting, &collapsed_environment, &collapsed_state, &passes, &iterator);
    bool fast_iterator = iterator == QA_MATERIAL_VERTEX_LIT || iterator == QA_MATERIAL_LIGHTMAPPED;
    qa_material_source_scratch *source = context->source_scratch;
    if (source && source->issuing) {
        if (iterator == QA_MATERIAL_VERTEX_LIT && material->stage_count &&
            !source_colors(material->stages, geometry, context, time, true, error)) return false;
        if (!material_source_cull(source, material->cull, error)) return false;
        if (!fast_iterator && material->polygon_offset && !material_source_polygon_offset(source, true,
            context->source_diagnostics.polygon_offset_factor, context->source_diagnostics.polygon_offset_units, error)) return false;
        if (!fast_iterator && (passes > 1 || collapsed) && !material_source_client_arrays(source, false, false, error)) return false;
        if (!material_source_client_arrays(source, true, true, error)) return false;
        if (!fast_iterator && passes == 1 && !collapsed &&
            !material_source_client_coordinate_pointer(source, MATERIAL_SOURCE_COORDINATES_STAGE, 0, error)) return false;
        if (iterator == QA_MATERIAL_LIGHTMAPPED) {
            qa_scene_state state; qa_scene_state_default(&state);
            if (!material_source_stage_state(source, &state, error) || !material_source_texture_select(source, 0, error)) return false;
        }
    }
    for (size_t i = 0; i < material->stage_count; ++i) {
        const qa_material_stage *stage = &material->stages[i];
        if (!active_stage(stage)) { if (context->source_scratch) break; continue; }
        const qa_material_stage *second = i == 0 && collapsed ? &material->stages[1] : NULL;
        if (!emit_stage(material, original, stage, second,
                         second == NULL ? QA_TEXTURE_MODULATE : collapsed_environment,
                         second == NULL ? stage->state : collapsed_state,
                         geometry, context, time, previous, iterator, frame, error)) return false;
        if (source && context->source_diagnostics.lightmap &&
            (stage->is_lightmap || (second && second->is_lightmap) || stage->vertex_lightmap)) break;
        if (second != NULL) ++i;
    }
    return emit_dlights(material, original, geometry, context, fast_iterator, frame, error) &&
           emit_fog(material, original, geometry, context, frame, error);
}
static double shader_seconds(const qa_material *original, const qa_material *material,
                              const qa_material_context *context)
{
    if (context->source_primitives) {
        float entity_time = (float)context->seconds - context->time_offset;
        float shader_time = entity_time - material->source_time_offset;
        return shader_time;
    }
    float offset = context->time_offset;
    if (original->remapped != NULL) offset += original->remap_time_offset;
    return context->seconds - offset;
}
static void source_entity_read(qa_material_source_scratch *source, const qa_material_context *context)
{
    source->entity = (material_source_entity){.color = context->entity_color,
        .texcoord = context->entity_texcoord, .model = context->model,
        .ambient = context->ambient, .directed = context->directed,
        .light_direction = context->light_direction, .ambient_alpha = context->ambient_alpha,
        .time_offset = context->time_offset, .shadow_plane = context->shadow_plane,
        .number = context->entity, .non_normalized_axis = context->non_normalized_axis,
        .projection_shadow = context->projection_shadow};
    source->local_view_origin = context->local_view_origin;
    source->light_count = context->light_count;
    if (context->light_count) memcpy(source->lights, context->lights, context->light_count * sizeof(*context->lights));
    source->entity_is_cell = context->source_entity_cell && context->entity < 1022;
    if (source->entity_is_cell) {
        source->entity_cell = context->entity;
        material_source_entity *cell = source->entities + context->entity;
        cell->ambient = context->ambient; cell->directed = context->directed;
        cell->light_direction = context->light_direction; cell->ambient_alpha = context->ambient_alpha;
    }
}
static void source_entity_apply(const qa_material_source_scratch *source, qa_material_context *context)
{
    const material_source_entity *entity = source->entity_is_cell ?
        source->entities + source->entity_cell : &source->entity;
    context->entity_color = entity->color; context->entity_texcoord = entity->texcoord;
    context->model = source->entity.model;
    context->ambient = entity->ambient; context->directed = entity->directed;
    context->light_direction = entity->light_direction; context->ambient_alpha = entity->ambient_alpha;
    context->time_offset = entity->time_offset; context->shadow_plane = entity->shadow_plane;
    context->entity = source->entity.number; context->non_normalized_axis = entity->non_normalized_axis;
    context->projection_shadow = entity->projection_shadow;
    if (source->entity_is_cell && source->scene_bank) {
        qa_q3_source_entity_cell cell;
        if (qa_q3_source_scene_bank_entity_read(source->scene_bank, source->entity_cell, &cell)) {
            context->entity_color = (qa_scene_vec4){cell.value.color[0] / 255.f, cell.value.color[1] / 255.f,
                cell.value.color[2] / 255.f, cell.value.color[3] / 255.f};
            context->entity_texcoord = cell.value.shader_texcoord;
            context->time_offset = cell.value.shader_time;
            context->shadow_plane = cell.value.shadow_plane;
            context->non_normalized_axis = cell.value.non_normalized_axes;
            context->projection_shadow = true;
            context->ambient = cell.ambient; context->directed = cell.directed;
            context->light_direction = cell.light_direction; context->ambient_alpha = cell.ambient_alpha;
        }
    }
    context->local_view_origin = source->local_view_origin;
    context->fog_index = source->fog_index; context->fog = source->fog;
    context->fog_tc_scale = source->fog_tc_scale; context->fog_has_surface = source->fog_has_surface;
    context->fog_surface = source->fog_surface; context->fog_volume_color = source->fog_volume_color;
    context->lightmap = source->lightmap;
    context->lights = source->lights; context->light_count = source->light_count;
}
static bool source_decoded_entity(qa_material_source_scratch *source, qa_material_context *context,
    uint32_t incoming, qa_error *error)
{
    if (context->entity == incoming) return true;
    qa_scene_matrix_identity(&context->model);
    context->local_view_origin = source->view_origin;
    if (context->entity == 1022) {
        context->entity_color = (qa_scene_vec4){1, 1, 1, 1};
        context->entity_texcoord = (qa_scene_vec2){0}; context->time_offset = 0;
        context->source_entity_cell = context->source_depth_hack = false;
        context->non_normalized_axis = context->projection_shadow = false;
        return true;
    }
    qa_q3_source_entity_cell cell;
    if (!source->scene_bank || !qa_q3_source_scene_bank_entity_read(source->scene_bank, context->entity, &cell)) {
        qa_error_set(error, QA_ERROR_FORMAT, context->entity, "Source packed sort selects no physical entity cell"); return false;
    }
    const qa_q3_ref_entity *entity = &cell.value;
    context->entity_color = (qa_scene_vec4){entity->color[0] / 255.f, entity->color[1] / 255.f,
        entity->color[2] / 255.f, entity->color[3] / 255.f};
    context->entity_texcoord = entity->shader_texcoord; context->time_offset = entity->shader_time;
    context->shadow_plane = entity->shadow_plane; context->non_normalized_axis = entity->non_normalized_axes;
    context->projection_shadow = true; context->source_depth_hack = (entity->flags & 8) != 0;
    context->source_entity_cell = true;
    context->ambient = cell.ambient; context->directed = cell.directed;
    context->light_direction = cell.light_direction; context->ambient_alpha = cell.ambient_alpha;
    if (entity->kind == QA_Q3_REF_MODEL) {
        for (unsigned axis = 0; axis < 3; ++axis) {
            context->model.m[axis * 4] = entity->axis[axis].x;
            context->model.m[axis * 4 + 1] = entity->axis[axis].y;
            context->model.m[axis * 4 + 2] = entity->axis[axis].z;
        }
        context->model.m[12] = entity->origin.x; context->model.m[13] = entity->origin.y;
        context->model.m[14] = entity->origin.z;
        qa_vec3 delta = qa_vec_sub(source->view_origin, entity->origin);
        float scale = 1;
        if (entity->non_normalized_axes) { float length = qa_vec_length(entity->axis[0]); scale = length ? 1 / length : 0; }
        context->local_view_origin = qa_v3(qa_vec_dot(delta, entity->axis[0]) * scale,
            qa_vec_dot(delta, entity->axis[1]) * scale, qa_vec_dot(delta, entity->axis[2]) * scale);
    }
    return true;
}
static bool source_begin_surface(qa_material_source_scratch *source,
    const material_source_submission *row, qa_error *error)
{
    const qa_material *material = row->original->remapped ? row->original->remapped : row->original;
    if (!qa_material_retain(material, error)) return false;
    if (!material_source_lightmap_set(source, row->context.lightmap, error)) {
        qa_material_release(material); return false;
    }
    const qa_material *previous = source->material;
    source->material = material; qa_material_release(previous);
    source->vertex_count = source->index_count = 0;
    source->light_mask = 0;
    source->fog_index = row->context.fog_index; source->fog = row->context.fog;
    source->fog_tc_scale = row->context.fog_tc_scale; source->fog_has_surface = row->context.fog_has_surface;
    source->fog_surface = row->context.fog_surface; source->fog_volume_color = row->context.fog_volume_color;
    double seconds = source->pictures ? (float)row->context.seconds - material->source_time_offset :
        shader_seconds(row->original, material, &row->context);
    if (material->clamp_time && seconds >= material->clamp_time) seconds = material->clamp_time;
    source->shader_time = (float)seconds;
    source->identity_light = row->context.identity_light;
    return true;
}
static bool source_restart_surface(qa_material_source_scratch *source,
    const material_source_submission *row, qa_error *error)
{
    material_source_submission restart = *row;
    restart.original = source->material;
    return source_begin_surface(source, &restart, error);
}
static bool source_debug(const qa_material *material, const qa_material *original,
    const qa_scene_mesh *mesh, const qa_material_context *context, qa_scene_frame *frame, qa_error *error)
{
    if (!context->source_diagnostics.show_triangles && !context->source_diagnostics.show_normals) return true;
    if (!context->source_white) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source diagnostics lost their actual white image"); return false;
    }
    if (context->source_diagnostics.show_triangles) {
        if (context->source_scratch && context->source_scratch->issuing &&
            !material_source_depth_range(context->source_scratch, 0, 0, error)) return false;
        qa_scene_draw draw = initial_draw(material, original, *mesh, context);
        qa_scene_vertex *vertices = frame_array(frame, mesh->vertex_count, sizeof(*vertices), alignof(qa_scene_vertex), error);
        if (mesh->vertex_count && !vertices) return false;
        for (size_t i = 0; i < mesh->vertex_count; ++i) {
            vertices[i] = mesh->vertices[i]; vertices[i].color = (qa_scene_vec4){1, 1, 1, 1}; vertices[i].texcoord = (qa_scene_vec2){0};
        }
        draw.mesh.vertices = vertices; draw.textures[0] = context->source_white; draw.texture_count = 1;
        if (context->source_diagnostics.no_bind &&
            !qa_material_source_no_bind_image(context->source_scratch, draw.textures[0], &draw.textures[0], error)) return false;
        qa_scene_state_default(&draw.state); draw.state.depth_near = draw.state.depth_far = 0; draw.state.wireframe = true;
        if (!qa_scene_frame_draw(frame, &draw, error)) return false;
        if (context->source_scratch && context->source_scratch->issuing &&
            !material_source_depth_range(context->source_scratch, 0, 1, error)) return false;
    }
    if (context->source_diagnostics.show_normals) {
        if (context->source_scratch && context->source_scratch->issuing &&
            !material_source_depth_range(context->source_scratch, 0, 0, error)) return false;
        qa_scene_draw draw = initial_draw(material, original, *mesh, context);
        qa_scene_vertex *vertices = frame_array(frame, mesh->vertex_count * 2, sizeof(*vertices), alignof(qa_scene_vertex), error);
        uint32_t *indices = frame_array(frame, mesh->vertex_count * 2, sizeof(*indices), alignof(uint32_t), error);
        if (mesh->vertex_count && (!vertices || !indices)) return false;
        for (size_t i = 0; i < mesh->vertex_count; ++i) {
            vertices[i * 2] = (qa_scene_vertex){.position = mesh->vertices[i].position, .color = {1, 1, 1, 1}};
            vertices[i * 2 + 1] = vertices[i * 2];
            vertices[i * 2 + 1].position = qa_vec_add(mesh->vertices[i].position, qa_vec_scale(mesh->vertices[i].normal, 2));
            indices[i * 2] = (uint32_t)i * 2; indices[i * 2 + 1] = (uint32_t)i * 2 + 1;
        }
        draw.mesh.vertices = vertices; draw.mesh.indices = indices; draw.mesh.primitive = QA_SCENE_LINES;
        draw.mesh.vertex_count = draw.mesh.index_count = mesh->vertex_count * 2;
        draw.textures[0] = context->source_white; draw.texture_count = 1;
        if (context->source_diagnostics.no_bind &&
            !qa_material_source_no_bind_image(context->source_scratch, draw.textures[0], &draw.textures[0], error)) return false;
        qa_scene_state_default(&draw.state); draw.state.depth_near = draw.state.depth_far = 0;
        if (!qa_scene_frame_draw(frame, &draw, error)) return false;
        if (context->source_scratch && context->source_scratch->issuing &&
            !material_source_depth_range(context->source_scratch, 0, 1, error)) return false;
    }
    return true;
}
static bool submit_source(const qa_material *original, const qa_material *material,
    const qa_scene_mesh *mesh, const qa_material_context *context, float time,
    qa_scene_frame *frame, qa_error *error)
{
    if (frame->source_skip_backend) return true;
    qa_material_source_scratch *source = context->source_scratch;
    if (!context->source_primitives || !material_source_enter(source, error)) return false;
    /* BeginSurface resets generation counts, never the allocated cells. */
    if (!source->dispatching || context->source_writer == QA_SOURCE_WRITE_CLOUD)
        source->vertex_count = source->index_count = 0;
    bool cloud = context->source_writer == QA_SOURCE_WRITE_CLOUD;
    bool stencil = stencil_material(material);
    bool grid = source->dispatching || context->source_grid_columns != 0;
    bool ok = cloud || (grid ? mesh->vertex_count <= QA_SOURCE_TESS_VERTICES && mesh->index_count <= QA_SOURCE_TESS_INDEXES :
        mesh->vertex_count < QA_SOURCE_TESS_VERTICES && mesh->index_count < QA_SOURCE_TESS_INDEXES);
    if (!ok) qa_error_set(error, QA_ERROR_FORMAT, 0, "Source surface exceeds tess sentinel limits");
    qa_scene_mesh geometry = {0};
    bool reached = mesh->index_count != 0;
    bool skipped = false;
    if (ok) {
        if (cloud) {
            for (size_t stage = 0; ok && stage < material->stage_count; ++stage) {
                if (!active_stage(material->stages + stage)) break;
                for (size_t i = 0; i < mesh->vertex_count; ++i) {
                    qa_scene_vertex *vertex = source->vertices + source->vertex_count;
                    vertex->position = mesh->vertices[i].position; vertex->texcoord = mesh->vertices[i].texcoord;
                    if (++source->vertex_count >= QA_SOURCE_TESS_VERTICES) {
                        qa_error_set(error, QA_ERROR_FORMAT, 0, "SHADER_MAX_VERTEXES hit in FillCloudySkySide"); ok = false; break;
                    }
                }
                if (ok && stage == 0) {
                    if (mesh->index_count > QA_SOURCE_TESS_INDEXES) {
                        qa_error_set(error, QA_ERROR_FORMAT, 0, "Cloud indexes exceed tess allocation"); ok = false;
                    } else {
                        memcpy(source->indices, mesh->indices, mesh->index_count * sizeof(*mesh->indices));
                        source->index_count = mesh->index_count;
                    }
                }
            }
        } else for (size_t i = 0; i < mesh->vertex_count; ++i) {
            qa_scene_vertex previous = source->vertices[i], vertex = mesh->vertices[i];
            switch (context->source_writer) {
            case QA_SOURCE_WRITE_MODEL: /* fall through */
            case QA_SOURCE_WRITE_MODEL_MD4: vertex.color = previous.color; vertex.lightmap = previous.lightmap; break;
            case QA_SOURCE_WRITE_PICTURE: /* fall through */
            case QA_SOURCE_WRITE_POLY: vertex.normal = previous.normal; vertex.lightmap = previous.lightmap; break;
            case QA_SOURCE_WRITE_RAIL:
                vertex.normal = previous.normal; vertex.lightmap = previous.lightmap; vertex.color.w = previous.color.w; break;
            case QA_SOURCE_WRITE_BSP: vertex.normal = previous.normal; break;
            case QA_SOURCE_WRITE_BSP_NORMAL:
                if (material == context->source_default_material || stencil)
                    vertex.normal = previous.normal;
                break;
            case QA_SOURCE_WRITE_CLOUD:
                vertex.normal = previous.normal; vertex.lightmap = previous.lightmap; vertex.color = previous.color; break;
            case QA_SOURCE_WRITE_FULL: break;
            }
            source->vertices[i] = vertex;
        }
        if (!cloud && source->indices != mesh->indices)
            memcpy(source->indices, mesh->indices, mesh->index_count * sizeof(*mesh->indices));
        if (!cloud) { source->vertex_count = mesh->vertex_count; source->index_count = mesh->index_count; }
        geometry = *mesh; geometry.vertices = source->vertices; geometry.indices = source->indices;
        geometry.vertex_count = source->vertex_count; geometry.index_count = source->index_count;
        if (ok && source->index_count && (source->indices[QA_SOURCE_TESS_INDEXES - 1] != 0 ||
            source->vertices[QA_SOURCE_TESS_VERTICES - 1].position.x != 0)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Source tess sentinel was overwritten"); ok = false;
        }
        skipped = ok && context->source_diagnostics.debug_sort != 0 &&
            context->source_diagnostics.debug_sort < material->sort;
        qa_scene_mesh deformed;
        if (ok && !skipped) ok = qa_material_deform_mesh(material, &geometry, context, time, frame, &deformed, error);
        if (ok && !skipped && (grid ? deformed.vertex_count > QA_SOURCE_TESS_VERTICES || deformed.index_count > QA_SOURCE_TESS_INDEXES :
            deformed.vertex_count >= QA_SOURCE_TESS_VERTICES || deformed.index_count >= QA_SOURCE_TESS_INDEXES)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Deformed Source surface exceeds tess sentinel limits"); ok = false;
        }
        if (ok && !skipped) {
            if (deformed.vertices != source->vertices)
                memcpy(source->vertices, deformed.vertices, deformed.vertex_count * sizeof(*deformed.vertices));
            if (deformed.indices != source->indices)
                memcpy(source->indices, deformed.indices, deformed.index_count * sizeof(*deformed.indices));
            geometry = deformed; geometry.vertices = source->vertices; geometry.indices = source->indices;
            source->vertex_count = geometry.vertex_count; source->index_count = geometry.index_count;
        }
    }
    if (ok && !skipped && geometry.index_count) {
        uint32_t *indices = frame_array(frame, geometry.index_count, sizeof(*indices), alignof(uint32_t), error);
        ok = indices != NULL;
        if (ok) { memcpy(indices, geometry.indices, geometry.index_count * sizeof(*indices)); geometry.indices = indices; }
    }
    qa_scene_texture_environment plan_environment; qa_scene_state plan_state;
    size_t plan_passes; qa_material_iterator plan_iterator;
    (void)material_plan(material, context->fragment_lighting, &plan_environment, &plan_state, &plan_passes, &plan_iterator);
    bool offset = ok && !skipped && reached && material->polygon_offset &&
        plan_iterator != QA_MATERIAL_VERTEX_LIT && plan_iterator != QA_MATERIAL_LIGHTMAPPED;
    if (ok && !skipped && reached) {
        ok = execute_material(material, original, &geometry, context, time, frame, error);
        if (ok && offset && source->issuing)
            ok = material_source_polygon_offset(source, false, context->source_diagnostics.polygon_offset_factor,
                context->source_diagnostics.polygon_offset_units, error);
        ok = ok &&
            (cloud || source_debug(material, original, &geometry, context, frame, error)) &&
            material_source_current(source, error);
    }
    if (ok && !skipped && !cloud) source->index_count = 0; /* ordinary EndSurface */
    material_source_leave(source);
    return ok;
}
bool qa_material_source_scene_begin(qa_material_source_scratch *source, qa_scene_frame *frame, qa_error *error)
{
    if (source && source->collecting && source->pictures &&
        !qa_material_source_scene_end(source, source->frame, true, error)) return false;
    if (!source || !frame) return false;
    if (source->entered) {
        if (source->collecting || source->dispatching || source->submitting || source->frame != frame ||
            frame->source_pending != source || !material_source_current(source, error)) return false;
    } else {
        if (!material_source_enter(source, error)) return false;
        if (frame->source_pending) { material_source_leave(source); qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Frame already owns pending Source work"); return false; }
    }
    source->submitting = false; source->collecting = true; source->frame = frame;
    if (!material_source_order_attach(frame->material_order, source, error)) {
        source->collecting = false; material_source_leave(source); return false;
    }
    source->pictures = false; frame->source_pending = source;
    source->head = source->tail = NULL; source->submission_count = 0;
    source->sky = (qa_material_context){0};
    source->view = (material_source_view){.command_offset = frame->command_count};
    return true;
}
bool qa_material_source_scene_view(qa_material_source_scratch *source, const qa_scene_world_input *input, qa_error *error)
{
    if (!source || !input || !source->collecting || source->pictures ||
        !material_source_current(source, error)) return false;
    source->view.valid = true;
    source->view.view = input->view;
    source->view.no_world = input->no_world; source->view.hyperspace = input->source_hyperspace;
    source->view.milliseconds = input->milliseconds;
    source->view.diagnostics = input->source_diagnostics;
    source->view.read = input->source_diagnostics_read;
    source->view.context = input->source_diagnostics_context;
    const qa_scene_light *lights = input->use_projected_lights ? input->projected_lights : input->lights;
    size_t count = input->use_projected_lights ? input->projected_light_count : input->light_count;
    if (count > 32 || (count && !lights)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source view has no admitted bounded light list"); return false;
    }
    source->view.light_count = count;
    if (count) memcpy(source->view.lights, lights, count * sizeof(*lights));
    if (input->render_text_count > 8 || (input->render_text_count && !input->render_texts)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source view has no actual bounded refdef text rows"); return false;
    }
    for (size_t i = 0; i < input->render_text_count; ++i) {
        const char *text = input->render_texts[i];
        if (!text) continue;
        size_t n = 0;
        while (n < 32 && text[n]) { source->view.texts[i][n] = text[n]; ++n; }
        source->view.texts[i][n] = 0;
    }
    return true;
}
bool qa_material_source_scene_sky(qa_material_source_scratch *source, const qa_material_context *context, qa_error *error)
{
    if (!source || !context || !context->source_surface || !source->collecting || source->pictures ||
        !material_source_current(source, error)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source sky requires its actual entered scene and iterator"); return false;
    }
    if (context->source_sky_world && !qa_scene_world_retain(context->source_sky_world, error)) return false;
    qa_scene_world_release(source->view.world);
    source->view.world = context->source_sky_world;
    source->view.far_clip = context->source_sky_far_clip;
    source->sky = *context; return true;
}
bool qa_material_source_picture_begin(qa_material_source_scratch *source, qa_scene_frame *frame,
    const qa_scene_view *view, bool *first, qa_error *error)
{
    if (!view || !first) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source picture requires its actual viewport"); return false; }
    *first = false;
    if (source && source->collecting && source->pictures && source->frame == frame) {
        const qa_scene_view *previous = source->tail ? &source->tail->context.view : view;
        if (previous->seat == view->seat && previous->viewport.x == view->viewport.x && previous->viewport.y == view->viewport.y &&
            previous->viewport.width == view->viewport.width && previous->viewport.height == view->viewport.height)
            return material_source_current(source, error);
    }
    if (!qa_material_source_scene_begin(source, frame, error)) return false;
    source->pictures = true; *first = true; return true;
}
bool qa_material_source_picture_end(qa_material_source_scratch *source, qa_scene_frame *frame, qa_error *error)
{
    if (!source || !source->collecting || !source->pictures || !source->submission_count) return true;
    return qa_material_source_scene_end(source, frame, true, error);
}
bool qa_material_source_frame_end(qa_material_source_scratch *source, qa_scene_frame *frame, bool submit, qa_error *error)
{
    if (!source || !source->entered) return true;
    if (source->frame != frame || frame->source_pending != source || source->dispatching || source->issuing) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source command issue lost its actual frame owner"); return false;
    }
    if (source->collecting && !qa_material_source_scene_end(source, frame, submit, error)) {
        qa_error cleanup = {0};
        (void)source_issue(source, frame, false, &cleanup);
        return false;
    }
    if (!source->entered) return true;
    return source_issue(source, frame, submit, error);
}
bool qa_material_source_raw_submit(qa_material_source_scratch *source, qa_scene_frame *frame,
    const qa_material_context *context, const qa_scene_draw *draw, qa_error *error)
{
    if (!source || !frame || !context || !draw || context->source_scratch != source ||
        draw->source_direct != QA_SOURCE_DIRECT_RAW ||
        source->dispatching || source->issuing || !context->view.viewport.width || !context->view.viewport.height) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source raw picture requires its actual renderer and pixel viewport");
        return false;
    }
    if (source->entered && !qa_material_source_frame_end(source, frame, true, error)) return false;
    if (!material_source_enter(source, error)) return false;
    source->frame = frame; frame->source_pending = source;
    source->dispatching = source->issuing = true;
    source->issued_count = 0; source->issue_started = false;
    bool skip = frame->source_skip_backend, clear = frame->source_clear_draw_buffer;
    frame->source_backend = true; frame->source_skip_backend = false; frame->source_clear_draw_buffer = false;
    qa_scene_view view;
    bool ok = material_source_view_read(source, &view, error);
    int32_t milliseconds = (int32_t)context->milliseconds;
    if (ok && context->source_picture_clock)
        milliseconds = context->source_picture_clock(context->source_picture_clock_context);
    if (ok) ok = material_source_current(source, error);
    if (ok) {
        view.viewport = context->view.viewport; view.seat = context->view.seat;
        view.clip_enabled = false; view.projection = context->source_picture_projection;
        view.clear_color = view.clear_depth = view.clear_stencil = false;
        source->projection_2d = true; source->picture_milliseconds = milliseconds;
        qa_scene_command command = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = view};
        ok = qa_scene_frame_output_domain(frame, view.viewport, true, error) &&
            qa_scene_frame_preblend_gamma(frame, false, error) &&
            qa_scene_frame_emit(frame, &command, error) && source_set_2d_state(source, error) &&
            material_source_color(source, (qa_scene_vec4){context->identity_light, context->identity_light,
                context->identity_light, 1}, error) && qa_scene_frame_draw(frame, draw, error);
    }
    if (source->issue_started) {
        qa_error finish = {0};
        bool finished = material_source_execute_prefix(source, frame, true, ok ? error : &finish);
        if (ok && !finished) ok = false;
        frame->command_count = frame->group_count = 0;
    }
    frame->source_skip_backend = skip; frame->source_clear_draw_buffer = clear;
    source->frame = NULL; frame->source_pending = NULL;
    source->dispatching = source->issuing = false;
    source->issued_count = 0; source->issue_started = false;
    material_source_leave(source);
    return ok;
}
static bool source_collect(const qa_material *original, const qa_scene_mesh *mesh,
    const qa_material_context *context, qa_scene_frame *frame, qa_error *error)
{
    qa_material_source_scratch *source = context->source_scratch;
    if (!context->source_primitives || source->frame != frame || !material_source_current(source, error) ||
        source->submission_count == SIZE_MAX) return false;
    material_source_submission *row = frame_array(frame, 1, sizeof(*row), alignof(material_source_submission), error);
    if (!row) return false;
    *row = (material_source_submission){.original = original, .mesh = *mesh, .context = *context};
    if (!row->context.source_surface) {
        row->context.source_surface = source->sky.source_surface;
        row->context.source_surface_context = source->sky.source_surface_context;
    }
    /* Shadow and effect producers may replace their frame slices after submit. */
    qa_scene_vertex *vertices = context->source_model_pose ? NULL :
        frame_array(frame, mesh->vertex_count, sizeof(*vertices), alignof(qa_scene_vertex), error);
    uint32_t *indices = frame_array(frame, mesh->index_count, sizeof(*indices), alignof(uint32_t), error);
    if ((!context->source_model_pose && mesh->vertex_count && !vertices) || (mesh->index_count && !indices)) return false;
    if (!context->source_model_pose && mesh->vertex_count) memcpy(vertices, mesh->vertices, mesh->vertex_count * sizeof(*vertices));
    if (mesh->index_count) memcpy(indices, mesh->indices, mesh->index_count * sizeof(*indices));
    row->mesh.vertices = vertices; row->mesh.indices = indices;
    if (context->light_count) {
        qa_scene_light *lights = frame_array(frame, context->light_count, sizeof(*lights), alignof(qa_scene_light), error);
        if (!lights) return false;
        memcpy(lights, context->lights, context->light_count * sizeof(*lights)); row->context.lights = lights;
    }
    if (context->fragment_light_count) {
        qa_scene_shadow_light *lights = frame_array(frame, context->fragment_light_count, sizeof(*lights), alignof(qa_scene_shadow_light), error);
        if (!lights) return false;
        memcpy(lights, context->fragment_lights, context->fragment_light_count * sizeof(*lights)); row->context.fragment_lights = lights;
    }
    if (context->text_count) {
        const char **texts = frame_array(frame, context->text_count, sizeof(*texts), alignof(char *), error);
        if (!texts) return false;
        for (size_t i = 0; i < context->text_count; ++i) {
            texts[i] = NULL;
            if (context->texts && context->texts[i]) {
                size_t length = strlen(context->texts[i]);
                if (length == SIZE_MAX) return false;
                char *text = frame_array(frame, length + 1, 1, 1, error);
                if (!text) return false;
                memcpy(text, context->texts[i], length + 1); texts[i] = text;
            }
        }
        row->context.texts = texts;
    }
    if (!source_sort_capture(row, frame, error) || !qa_material_retain(original, error)) return false;
    if (context->source_light_world && !qa_scene_world_retain((qa_scene_world *)context->source_light_world, error)) {
        qa_material_release(original); return false;
    }
    row->held_light_world = (qa_scene_world *)context->source_light_world;
    if (context->source_model_assets && (!context->source_model_retain || !context->source_model_release ||
        !context->source_model_retain(context->source_model_context, error))) {
        qa_scene_world_release(row->held_light_world); qa_material_release(original);
        if (!error || error->code == QA_OK)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source model queue requires its actual registration owner");
        return false;
    }
    if (source->tail) source->tail->next = row; else source->head = row;
    source->tail = row; ++source->submission_count; return true;
}
static bool source_append(qa_material_source_scratch *source, const material_source_submission *row,
    qa_scene_frame *frame, qa_error *error)
{
    qa_scene_mesh mesh = row->mesh;
    bool md4 = row->context.source_writer == QA_SOURCE_WRITE_MODEL_MD4;
    size_t index_base = md4 ? source->index_count : source->vertex_count;
    if (md4) {
        for (size_t i = 0; i < row->mesh.index_count; ++i)
            source->indices[source->index_count + i] = (uint32_t)index_base + row->mesh.indices[i];
        source->index_count += row->mesh.index_count;
    }
    if (row->context.source_model_pose) {
        int32_t current = row->context.source_model_frame, previous = row->context.source_model_old_frame;
        float back = row->context.source_model_back_lerp;
        if (row->context.source_cell_geometry) {
            qa_q3_source_entity_cell cell;
            if (!source->scene_bank || !qa_q3_source_scene_bank_entity_read(source->scene_bank,
                row->context.entity, &cell)) {
                qa_error_set(error, QA_ERROR_FORMAT, row->context.entity, "Source model pose lost its selected physical entity");
                return false;
            }
            current = cell.value.frame; previous = cell.value.old_frame; back = cell.value.back_lerp;
        }
        if (!row->context.source_model_pose(row->context.source_model_context, current, previous,
            back, frame, &mesh, error) || !material_source_current(source, error)) return false;
        if (mesh.vertex_count != row->mesh.vertex_count || mesh.index_count != row->mesh.index_count ||
            (mesh.vertex_count && !mesh.vertices) || (mesh.index_count && !mesh.indices)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Source model pose changed its admitted surface topology"); return false;
        }
    }
    size_t base = source->vertex_count;
    const qa_material *material = source->material;
    bool stencil = stencil_material(material);
    for (size_t i = 0; i < mesh.vertex_count; ++i) {
        qa_scene_vertex previous = source->vertices[base + i], vertex = mesh.vertices[i];
        switch (row->context.source_writer) {
        case QA_SOURCE_WRITE_MODEL: /* fall through */
        case QA_SOURCE_WRITE_MODEL_MD4: vertex.color = previous.color; vertex.lightmap = previous.lightmap; break;
        case QA_SOURCE_WRITE_PICTURE: /* fall through */
        case QA_SOURCE_WRITE_POLY: vertex.normal = previous.normal; vertex.lightmap = previous.lightmap; break;
        case QA_SOURCE_WRITE_RAIL:
            vertex.normal = previous.normal; vertex.lightmap = previous.lightmap; vertex.color.w = previous.color.w; break;
        case QA_SOURCE_WRITE_BSP: vertex.normal = previous.normal; break;
        case QA_SOURCE_WRITE_BSP_NORMAL:
            if (material == row->context.source_default_material || stencil)
                vertex.normal = previous.normal;
            break;
        case QA_SOURCE_WRITE_CLOUD:
            vertex.normal = previous.normal; vertex.lightmap = previous.lightmap; vertex.color = previous.color; break;
        case QA_SOURCE_WRITE_FULL: break;
        }
        source->vertices[base + i] = vertex;
    }
    if (!md4) {
        for (size_t i = 0; i < mesh.index_count; ++i)
            source->indices[source->index_count + i] = (uint32_t)index_base + mesh.indices[i];
        source->index_count += mesh.index_count;
    }
    source->vertex_count += mesh.vertex_count;
    return true;
}
bool qa_material_source_commands(const qa_material *material, const qa_material_context *context,
    qa_scene_frame *frame, size_t first, qa_error *error)
{
    qa_material_source_scratch *source = context ? context->source_scratch : NULL;
    if (!material || !source || !source->collecting || source->pictures || source->frame != frame ||
        first > frame->command_count || context->light_count > 32 ||
        (context->light_count && !context->lights) || !material_source_current(source, error)) return false;
    if (source->submission_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source submission count is exhausted"); return false;
    }
    material_source_submission *row = frame_array(frame, 1, sizeof(*row), alignof(material_source_submission), error);
    if (!row) return false;
    *row = (material_source_submission){.original = material, .context = *context,
        .command_count = frame->command_count - first};
    if (row->command_count) {
        row->commands = frame_array(frame, row->command_count, sizeof(*row->commands), alignof(qa_scene_command), error);
        if (!row->commands) return false;
        memcpy(row->commands, frame->commands + first, row->command_count * sizeof(*row->commands));
    }
    if (!source_sort_capture(row, frame, error) || !qa_material_retain(material, error)) return false;
    if (context->source_light_world && !qa_scene_world_retain((qa_scene_world *)context->source_light_world, error)) {
        qa_material_release(material); return false;
    }
    row->held_light_world = (qa_scene_world *)context->source_light_world;
    frame->command_count = first;
    if (source->tail) source->tail->next = row; else source->head = row;
    source->tail = row; ++source->submission_count; return true;
}
static bool source_flush(qa_material_source_scratch *source, const material_source_submission *row,
    uint32_t light_mask, qa_scene_frame *frame, qa_error *error)
{
    if (!row || !source->index_count) return true;
    const qa_material *material = source->material;
    if (!material) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source EndSurface has no retained shader"); return false; }
    qa_material_context context = row->context; context.source_writer = QA_SOURCE_WRITE_FULL;
    const char *texts[8];
    for (size_t i = 0; i < 8; ++i) texts[i] = source->texts[i];
    context.texts = texts; context.text_count = 8;
    context.light_mask = light_mask;
    source_entity_apply(source, &context);
    float time = source->shader_time;
    if (!isfinite(time)) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source shader time is nonfinite"); return false; }
    qa_scene_mesh mesh = row->mesh;
    mesh.vertices = source->vertices; mesh.indices = source->indices;
    mesh.vertex_count = source->vertex_count; mesh.index_count = source->index_count;
    mesh.identity = mesh.revision = 0; mesh.geometry = NULL;
    size_t first = frame->command_count;
    if (stencil_material(material)) {
        if (source->indices[QA_SOURCE_TESS_INDEXES - 1] != 0 || source->vertices[QA_SOURCE_TESS_VERTICES - 1].position.x != 0) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Source tess sentinel was overwritten"); return false;
        }
        if (source->vertex_count >= QA_SOURCE_TESS_VERTICES / 2 || context.source_diagnostics.stencil_bits < 4) return true;
        for (size_t i = 0; i < source->vertex_count; ++i)
            source->vertices[i + source->vertex_count].position =
                qa_vec_add(source->vertices[i].position, qa_vec_scale(context.light_direction, -512));
        const qa_scene_image *white = context.source_white;
        if (context.source_diagnostics.no_bind &&
            !qa_material_source_no_bind_image(source, white, &white, error)) return false;
        if (!qa_scene_source_stencil_shadow(frame, &context.view, &mesh, context.model,
            context.light_direction, white,
            error) || !material_source_current(source, error)) return false;
        return source->issuing || qa_scene_frame_group(frame, first, QA_SCENE_GROUP_SEQUENCE, row->original,
            row->original->sort, context.entity, context.fog_index, 0, error);
    }
    if (material->sky && (context.source_scratch || context.source_surface)) {
        if (source->indices[QA_SOURCE_TESS_INDEXES - 1] != 0 || source->vertices[QA_SOURCE_TESS_VERTICES - 1].position.x != 0) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Source tess sentinel was overwritten"); return false;
        }
        if (context.source_diagnostics.debug_sort != 0 && context.source_diagnostics.debug_sort < material->sort) return true;
        bool depth_changed = source->issuing && !context.source_diagnostics.fast_sky;
        if (depth_changed && !material_source_depth_range(source,
            context.source_diagnostics.show_sky ? 0 : 1, context.source_diagnostics.show_sky ? 0 : 1, error)) return false;
        bool ok = (context.source_scratch ?
            qa_scene_world_source_sky_submit(source->world, row->original, material, &mesh, &context,
                source->far_clip, frame, error) :
            context.source_surface(context.source_surface_context, row->original, material,
                &mesh, &context, frame, error)) && material_source_current(source, error);
        if (!ok) return false;
        if (depth_changed && !material_source_depth_range(source, 0, 1, error)) return false;
        mesh.vertex_count = source->vertex_count; mesh.index_count = source->index_count;
        if (mesh.vertex_count && mesh.index_count &&
            !source_debug(material, row->original, &mesh, &context, frame, error)) return false;
        source->index_count = 0;
        return source->issuing || qa_scene_frame_group(frame, first, QA_SCENE_GROUP_SEQUENCE, row->original,
            row->original->sort, context.entity, context.fog_index, context.source_dlighted ? 1 : 0, error);
    }
    if (!submit_source(row->original, material, &mesh, &context, time, frame, error)) return false;
    if (source->pictures || source->issuing) return true;
    return qa_scene_frame_group(frame, first, QA_SCENE_GROUP_SEQUENCE, row->original,
        row->original->sort, context.entity, context.fog_index, context.source_dlighted ? 1 : 0, error);
}
bool material_source_deform_overflow(qa_material_source_scratch *source, const qa_material_context *context,
    qa_scene_frame *frame, qa_error *error)
{
    if (!source || !context || source->frame != frame || !source->submitting || !source->issuing ||
        !source->dispatching || !source->material || !material_source_current(source, error)) return false;
    material_source_submission row = {.original = source->material, .context = *context,
        .mesh = {.vertices = source->vertices, .indices = source->indices,
            .vertex_count = source->vertex_count, .index_count = source->index_count,
            .primitive = QA_SCENE_TRIANGLES}};
    source->submitting = false;
    bool ok = source_flush(source, &row, source->light_mask, frame, error);
    if (ok) ok = source_restart_surface(source, &row, error);
    source->submitting = true;
    return ok;
}
static bool source_flush_pending(qa_material_source_scratch *source, qa_scene_frame *frame, qa_error *error)
{
    if (source->runtime_frame_policy &&
        !source->runtime_frame_policy(source->runtime_frame_context, frame, error)) return false;
    if (frame->source_skip_backend) return true;
    if (!source->index_count) return true;
    if (!source->material || !source->runtime_diagnostics) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source pending surface has no actual runtime owner"); return false;
    }
    material_source_submission row = {.original = source->material,
        .mesh = {.primitive = QA_SCENE_TRIANGLES},
        .context = {.source_scratch = source, .source_primitives = true,
            .identity_light = source->identity_light,
            .source_white = qa_material_library_has_source_profile(source->material->library) ?
                qa_scene_source_q3_white(qa_material_library_resource_owner(source->material->library)) :
                qa_scene_white(qa_material_library_resource_owner(source->material->library)),
            .video_frame = source->runtime_video_frame, .video_context = source->runtime_video_context,
            .milliseconds = source->picture_milliseconds,
            .seconds = (float)source->picture_milliseconds * .001f}};
    if (!source->runtime_diagnostics(source->runtime_diagnostics_context, &row.context.source_diagnostics, error) ||
        !material_source_current(source, error) || !material_source_view_read(source, &row.context.view, error)) return false;
    row.context.source_picture = source->projection_2d;
    if (row.context.source_picture) {
        qa_scene_rect viewport = row.context.view.viewport;
        if (!viewport.width || !viewport.height) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source pending picture lost its actual viewport"); return false;
        }
        row.context.source_picture_projection = (qa_scene_matrix){.m = {
            2.f / (float)viewport.width, 0, 0, 0, 0, -2.f / (float)viewport.height, 0, 0,
            0, 0, -2, 0, -1.f - 2.f * (float)viewport.x / (float)viewport.width,
            1.f + 2.f * (float)viewport.y / (float)viewport.height, -1, 1}};
    }
    bool dispatching = source->dispatching;
    source->dispatching = true;
    bool ok = source_flush(source, &row, source->light_mask, frame, error);
    source->dispatching = dispatching;
    return ok;
}
static bool source_dispatch_scene(qa_material_source_scratch *source, qa_scene_frame *frame,
    bool submit, qa_error *error)
{
    if (!source || !source->entered || !source->collecting || source->frame != frame) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source scene lost its entered tess owner"); return false;
    }
    submit = submit && !frame->source_skip_backend;
    bool ok = !submit || material_source_current(source, error);
    size_t count = source->submission_count;
    material_source_submission **rows = NULL;
    material_source_submission *decoded_rows = NULL;
    size_t decoded_count = 0;
    qa_scene_group *groups = NULL, **ordered = NULL;
    if (ok && submit && count) {
        rows = frame_array(frame, count, sizeof(*rows), alignof(material_source_submission *), error);
        groups = frame_array(frame, count, sizeof(*groups), alignof(qa_scene_group), error);
        ordered = frame_array(frame, count, sizeof(*ordered), alignof(qa_scene_group *), error);
        ok = rows && groups && ordered && qa_material_order_prepare(frame->material_order, error);
        if (ok && !source->pictures) {
            decoded_rows = frame_array(frame, count, sizeof(*decoded_rows), alignof(material_source_submission), error);
            ok = decoded_rows != NULL;
        }
        material_source_submission *row = source->head;
        for (size_t i = 0; ok && i < count; ++i, row = row->next) {
            if (ok) {
                rows[i] = row; ordered[i] = groups + i;
                groups[i] = (qa_scene_group){.ordinal = i, .source_sort = row->packed_sort};
            }
        }
    }
    source->collecting = false; source->dispatching = true;
    const material_source_submission *batch = NULL;
    uint32_t key = 0, light_mask = source->pictures ? source->light_mask : 0;
    uint32_t selected_entity = UINT32_MAX;
    bool old_depth_range = false;
    for (size_t i = 0; ok && submit && i < count; ++i) {
        material_source_submission *row = rows[ordered[i]->ordinal];
        uint32_t next_key = row->packed_sort;
        if (!source->pictures) {
            material_source_submission *decoded = decoded_rows + decoded_count;
            uint32_t shader = (next_key >> 17) & 16383u;
            const qa_material *sorted_material = qa_material_order_sorted_at(frame->material_order, shader);
            if (!sorted_material) {
                qa_error_set(error, QA_ERROR_FORMAT, shader, "Source packed sort selects an absent registered shader");
                ok = false; break;
            }
            qa_scene_fog_volume fog;
            if (!qa_scene_world_source_fog_read(source->world, (next_key >> 2) & 31u, &fog, error) ||
                !qa_material_retain(sorted_material, error)) { ok = false; break; }
            *decoded = *row;
            decoded->original = sorted_material;
            decoded->context.entity = (next_key >> 7) & 1023u;
            decoded->context.fog_index = fog.index;
            decoded->context.fog = fog.fog;
            decoded->context.fog_volume_color = fog.fog.color;
            decoded->context.fog_tc_scale = fog.tc_scale;
            decoded->context.fog_has_surface = fog.has_surface;
            decoded->context.fog_surface = fog.surface;
            row = decoded; ++decoded_count;
            if (!source_decoded_entity(source, &row->context, rows[ordered[i]->ordinal]->context.entity, error)) {
                ok = false; break;
            }
        }
        bool same_entity = !batch || row->context.entity == batch->context.entity;
        bool transition = source->pictures ? row->original != source->material :
            ((key & ~UINT32_C(0x1ff80)) != (next_key & ~UINT32_C(0x1ff80)) ||
            (batch && row->original != batch->original) || (!same_entity && !row->original->entity_mergable));
        if (batch && transition) {
            ok = source_flush(source, batch, light_mask, frame, error); batch = NULL;
        }
        if (!ok) break;
        if (row->context.source_entity_cell && (row->context.entity >= 1022 ||
            (!source->scene_bank && row->context.entity >= source->entity_count))) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source surface lost its actual admitted entity cell");
            ok = false; break;
        }
        if (!batch) {
            if (!source->pictures || row->original != source->material) {
                if (source->pictures && source->material && source->index_count) {
                    material_source_submission previous = *row;
                    previous.original = source->material;
                    ok = source_flush(source, &previous, source->light_mask, frame, error);
                    if (!ok) break;
                }
                ok = source_begin_surface(source, row, error);
                if (!ok) break;
                if (source->pictures) {
                    qa_scene_matrix orientation = source->entity.model;
                    source->entity = (material_source_entity){.model = orientation};
                    source->entity_is_cell = false;
                }
            }
            light_mask = source->light_mask;
        }
        if (!source->pictures && row->context.entity != selected_entity) {
            source_entity_read(source, &row->context);
            source->shader_time = (float)shader_seconds(row->original, source->material, &row->context);
            selected_entity = row->context.entity;
            bool depth_range = row->context.entity != 1022 && row->context.source_depth_hack;
            if (source->issuing && depth_range != old_depth_range &&
                !material_source_depth_range(source, 0, depth_range ? .3f : 1, error)) {
                ok = false; break;
            }
            old_depth_range = depth_range;
        } else if (source->pictures) source_entity_apply(source, &row->context);
        if (row->context.source_light_world && !qa_scene_world_source_light_mask_read(
            row->context.source_light_world, row->context.source_light_surface, &row->context.light_mask, error)) {
            ok = false; break;
        }
        if (row->context.source_entity_surface) {
            qa_q3_source_entity_cell cell;
            const qa_q3_ref_entity *entity = NULL;
            if (row->context.source_cell_geometry) {
                if (!source->scene_bank || !qa_q3_source_scene_bank_entity_read(source->scene_bank,
                    row->context.entity, &cell)) {
                    qa_error_set(error, QA_ERROR_FORMAT, row->context.entity, "Source entity dispatch lost its actual physical cell");
                    ok = false; break;
                }
                entity = &cell.value;
            }
            bool direct = false;
            if (!row->context.source_entity_surface(row->context.source_entity_surface_context, entity,
                row->original, &row->context, frame, &row->mesh, &direct, error) ||
                !material_source_current(source, error)) { ok = false; break; }
            if (direct) { batch = row; key = next_key; continue; }
        }
        if (row->commands) {
            size_t first = frame->command_count;
            for (size_t j = 0; ok && j < row->command_count; ++j) {
                qa_scene_command command = row->commands[j];
                if (command.kind == QA_SCENE_COMMAND_DRAW && row->context.source_diagnostics.no_bind)
                    for (size_t texture = 0; texture < command.data.draw.texture_count; ++texture)
                        if (!command.data.draw.retain_texture[texture] &&
                            !qa_material_source_no_bind_image(source, command.data.draw.textures[texture],
                                &command.data.draw.textures[texture], error)) { ok = false; break; }
                if (ok) ok = qa_scene_frame_emit(frame, &command, error);
            }
            if (ok && !source->issuing) ok = qa_scene_frame_group(frame, first, QA_SCENE_GROUP_SEQUENCE, row->original,
                row->original->sort, row->context.entity, row->context.fog_index, 0, error);
            batch = row; key = next_key;
            continue;
        }
        bool rail = row->context.source_writer == QA_SOURCE_WRITE_RAIL;
        size_t columns = row->context.source_grid_columns, height = row->context.source_grid_rows;
        if (columns) {
            if (columns < 2 || height < 2 || columns > 65 || height > 65 ||
                row->mesh.vertex_count != columns * height || row->mesh.index_count != (columns - 1) * (height - 1) * 6) {
                qa_error_set(error, QA_ERROR_FORMAT, 0, "Source grid lost its selected row/column topology"); ok = false; break;
            }
            light_mask |= row->context.light_mask; source->light_mask = light_mask;
            size_t used = 0;
            bool first_slab = true;
            while (ok && used < height - 1) {
                if (!batch && !first_slab) {
                    ok = source_restart_surface(source, row, error); light_mask = source->light_mask;
                    if (!ok) break;
                }
                first_slab = false;
                size_t vrows = (QA_SOURCE_TESS_VERTICES - source->vertex_count) / columns;
                size_t irows = (QA_SOURCE_TESS_INDEXES - source->index_count) / (columns * 6);
                if (vrows < 2 || irows < 1) {
                    ok = source_flush(source, batch, light_mask, frame, error); batch = NULL; continue;
                }
                size_t slab = vrows < irows + 1 ? vrows - 1 : irows;
                if (slab > height - used) slab = height - used;
                material_source_submission part = *row;
                part.mesh.vertices = row->mesh.vertices + used * columns; part.mesh.vertex_count = slab * columns;
                part.mesh.index_count = (slab - 1) * (columns - 1) * 6;
                uint32_t *indices = part.mesh.index_count ? frame_array(frame, part.mesh.index_count,
                    sizeof(*indices), alignof(uint32_t), error) : NULL;
                if (part.mesh.index_count && !indices) { ok = false; break; }
                for (size_t j = 0; j < part.mesh.index_count; ++j)
                    indices[j] = row->mesh.indices[used * (columns - 1) * 6 + j] - (uint32_t)(used * columns);
                part.mesh.indices = indices;
                if (!source_append(source, &part, frame, error)) { ok = false; break; }
                batch = row; key = next_key;
                used += slab - 1;
            }
            continue;
        }
        if (!rail && (row->mesh.vertex_count >= QA_SOURCE_TESS_VERTICES || row->mesh.index_count >= QA_SOURCE_TESS_INDEXES)) {
            if (row->context.source_dlight_before_overflow) { light_mask |= row->context.light_mask; source->light_mask = light_mask; }
            if (batch) { ok = source_flush(source, batch, light_mask, frame, error); batch = NULL; }
            if (!ok) break;
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Source surface exceeds tess sentinel limits"); ok = false; break;
        }
        size_t pieces = rail ? row->mesh.vertex_count / 4 : 1;
        if (rail && (row->mesh.vertex_count % 4 || row->mesh.index_count != pieces * 6)) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Source rail lost its actual quad topology"); ok = false; break;
        }
        for (size_t piece = 0; ok && piece < pieces; ++piece) {
            material_source_submission part = *row; uint32_t indices[6];
            if (rail) {
                part.mesh.vertices = row->mesh.vertices + piece * 4; part.mesh.vertex_count = 4;
                part.mesh.index_count = 6; part.mesh.indices = indices;
                for (size_t j = 0; j < 6; ++j) indices[j] = row->mesh.indices[piece * 6 + j] - (uint32_t)(piece * 4);
            }
            if (row->context.source_dlight_before_overflow) { light_mask |= row->context.light_mask; source->light_mask = light_mask; }
            if ((part.mesh.vertex_count || part.mesh.index_count) &&
                (source->vertex_count + part.mesh.vertex_count >= QA_SOURCE_TESS_VERTICES ||
                source->index_count + part.mesh.index_count >= QA_SOURCE_TESS_INDEXES)) {
                ok = source_flush(source, batch, light_mask, frame, error); batch = NULL;
                if (ok) { ok = source_restart_surface(source, row, error); light_mask = source->light_mask; }
            }
            if (!ok) break;
            if (!source_append(source, &part, frame, error)) { ok = false; break; }
            batch = row; key = next_key;
            if (!row->context.source_dlight_before_overflow && part.mesh.index_count) {
                light_mask |= row->context.light_mask; source->light_mask = light_mask;
            }
        }
    }
    if (ok && submit && !source->pictures) ok = source_flush(source, batch, light_mask, frame, error);
    if (ok && submit && source->issuing && !source->pictures && old_depth_range)
        ok = material_source_depth_range(source, 0, 1, error);
    for (size_t i = 0; i < decoded_count; ++i) qa_material_release(decoded_rows[i].original);
    source->head = source->tail = NULL; source->submission_count = 0;
    source->sky = (qa_material_context){0};
    source->pictures = false;
    source->dispatching = false;
    return ok;
}
bool qa_material_source_scene_end(qa_material_source_scratch *source, qa_scene_frame *frame,
    bool submit, qa_error *error)
{
    if (!source || !source->entered || !source->collecting || source->frame != frame ||
        source->dispatching || source->issuing || !material_source_current(source, error)) {
        if (!error || error->code == QA_OK)
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source scene lost its actual collector");
        return false;
    }
    if (!submit) return source_issue(source, frame, false, error);
    if (!source->pictures && source->submission_count > 1) {
        size_t count = source->submission_count;
        material_source_submission **rows = frame_array(frame, count, sizeof(*rows), alignof(material_source_submission *), error);
        qa_scene_group *groups = frame_array(frame, count, sizeof(*groups), alignof(qa_scene_group), error);
        qa_scene_group **ordered = frame_array(frame, count, sizeof(*ordered), alignof(qa_scene_group *), error);
        if (!rows || !groups || !ordered) return false;
        material_source_submission *row = source->head;
        for (size_t i = 0; i < count; ++i, row = row->next) {
            rows[i] = row; groups[i] = (qa_scene_group){.ordinal = i, .source_sort = row->packed_sort};
            ordered[i] = groups + i;
        }
        if (!material_source_sort(ordered, count, error)) return false;
        source->head = rows[ordered[0]->ordinal]; source->tail = rows[ordered[count - 1]->ordinal];
        for (size_t i = 0; i < count; ++i)
            rows[ordered[i]->ordinal]->next = i + 1 < count ? rows[ordered[i + 1]->ordinal] : NULL;
    }
    material_source_operation *operation = frame_array(frame, 1, sizeof(*operation),
        alignof(material_source_operation), error);
    if (!operation) return false;
    *operation = (material_source_operation){.head = source->head, .tail = source->tail,
        .count = source->submission_count, .command_offset = frame->command_count,
        .pictures = source->pictures, .view = source->view};
    if (source->last_operation) source->last_operation->next = operation;
    else source->operations = operation;
    source->last_operation = operation;
    source->head = source->tail = NULL; source->submission_count = 0;
    source->collecting = source->pictures = false;
    source->sky = (qa_material_context){0};
    source->view = (material_source_view){0};
    return true;
}
bool qa_material_source_issue_emitted(qa_material_source_scratch *source, qa_scene_frame *frame, qa_error *error)
{
    if (!source || !source->issuing) return true;
    return material_source_execute_prefix(source, frame, false, error);
}
static bool source_issue(qa_material_source_scratch *source, qa_scene_frame *frame, bool submit, qa_error *error)
{
    material_source_submission *pending = source->collecting ? source->head : NULL;
    qa_scene_world *pending_world = source->view.world;
    bool requested = submit;
    bool ok = !submit || material_source_current(source, error);
    for (material_source_operation *operation = source->operations; ok && submit && operation; operation = operation->next) {
        if (operation->view.valid && operation->view.read)
            ok = operation->view.read(operation->view.context, &operation->view.diagnostics, error) &&
                material_source_current(source, error);
        for (material_source_submission *row = operation->head; ok && row; row = row->next)
            if (row->context.source_diagnostics_read)
                ok = row->context.source_diagnostics_read(row->context.source_diagnostics_context,
                    &row->context.source_diagnostics, error) && material_source_current(source, error);
    }
    submit = submit && !frame->source_skip_backend;
    size_t count = frame->command_count;
    qa_scene_command *commands = NULL;
    if (ok && submit) {
        ok = qa_scene_frame_finish(frame, NULL, NULL, error);
        count = frame->command_count;
        if (ok && count) {
            commands = frame_array(frame, count, sizeof(*commands), alignof(qa_scene_command), error);
            ok = commands != NULL;
            if (ok) memcpy(commands, frame->commands, count * sizeof(*commands));
        }
        for (material_source_operation *operation = source->operations; ok && operation; operation = operation->next)
            if (operation->view.valid) {
                material_source_view *policy = &operation->view;
                if (policy->command_offset >= count || commands[policy->command_offset].kind != QA_SCENE_COMMAND_VIEW) {
                    qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source view lost its actual reached command"); ok = false; break;
                }
                qa_scene_view *view = &commands[policy->command_offset].data.view;
                view->clear_color = policy->hyperspace || (policy->diagnostics.fast_sky && !policy->no_world);
                if (view->clear_color) {
                    float gray = policy->hyperspace ? (float)((uint32_t)policy->milliseconds & 255u) / 255 : 0;
                    view->color = (qa_scene_vec4){gray, gray, gray, 1};
                }
            }
    }
    source->collecting = false; source->issuing = true;
    source->issued_count = 0; source->issue_started = false;
    if (ok && submit) {
        frame->command_count = 0;
        size_t copied = 0;
        for (material_source_operation *operation = source->operations; ok && operation; operation = operation->next) {
            if (operation->command_offset < copied || operation->command_offset > count) {
                qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source operation lost its actual command position"); ok = false; break;
            }
            while (ok && copied < operation->command_offset) {
                if (commands[copied].kind == QA_SCENE_COMMAND_SWAP && source->runtime_frame_policy) {
                    frame->source_backend = true;
                    ok = source->runtime_frame_policy(source->runtime_frame_context, frame, error) &&
                        material_source_current(source, error);
                }
                if (ok && !frame->source_skip_backend && (commands[copied].kind == QA_SCENE_COMMAND_SWAP ||
                    (operation->view.valid && copied == operation->view.command_offset)))
                    ok = source_flush_pending(source, frame, error);
                size_t reached = copied;
                if (ok) ok = qa_scene_frame_emit(frame, commands + copied++, error);
                if (ok && operation->view.valid && !operation->view.hyperspace &&
                    reached == operation->view.command_offset)
                    ok = material_source_cull_invalidate(source, error);
            }
            if (!ok) break;
            if (operation->view.valid) {
                if (operation->view.world && !qa_scene_world_retain(operation->view.world, error)) { ok = false; break; }
                qa_scene_world_release(source->world);
                source->world = operation->view.world;
                source->far_clip = operation->view.far_clip;
                memcpy(source->texts, operation->view.texts, sizeof(source->texts));
                source->projection_2d = false;
                source->view_origin = operation->view.view.origin;
                memcpy(source->view_axis, operation->view.view.axis, sizeof(source->view_axis));
                source->view_mirror = operation->view.view.mirror;
                qa_scene_matrix orientation = source->entity.model;
                source->entity = (material_source_entity){.model = orientation, .number = 1022};
                source->entity_is_cell = false;
                source->light_count = operation->view.light_count;
                if (source->light_count) memcpy(source->lights, operation->view.lights,
                    source->light_count * sizeof(*source->lights));
            }
            source->head = operation->head; source->tail = operation->tail;
            source->submission_count = operation->count; source->pictures = operation->pictures;
            if (operation->pictures && operation->head) {
                if (!source->projection_2d) {
                    const qa_material_context *context = &operation->head->context;
                    source->picture_milliseconds = context->source_picture_clock ?
                        context->source_picture_clock(context->source_picture_clock_context) : (int32_t)context->milliseconds;
                    if (!material_source_current(source, error)) { ok = false; break; }
                    if (!source_set_2d_state(source, error)) { ok = false; break; }
                }
                for (material_source_submission *row = operation->head; row; row = row->next) {
                    row->context.milliseconds = source->picture_milliseconds;
                    row->context.seconds = (float)source->picture_milliseconds * .001f;
                    row->context.local_view_origin = source->local_view_origin;
                    row->context.view.origin = source->view_origin;
                    memcpy(row->context.view.axis, source->view_axis, sizeof(source->view_axis));
                    row->context.view.mirror = row->context.mirror = source->view_mirror;
                }
            }
            if (operation->pictures) source->projection_2d = true;
            source->collecting = true;
            ok = source_dispatch_scene(source, frame, true, error);
        }
        while (ok && copied < count) {
            if (commands[copied].kind == QA_SCENE_COMMAND_SWAP) {
                frame->source_backend = true;
                if (source->runtime_frame_policy)
                    ok = source->runtime_frame_policy(source->runtime_frame_context, frame, error) &&
                        material_source_current(source, error);
                if (ok && !frame->source_skip_backend) ok = source_flush_pending(source, frame, error);
            }
            if (ok) ok = qa_scene_frame_emit(frame, commands + copied++, error);
        }
    }
    if (source->issue_started) {
        qa_error finish = {0};
        bool completed = material_source_execute_prefix(source, frame, true, ok ? error : &finish);
        if (ok && !completed) ok = false;
        /* These commands have reached the actual renderer and must not replay. */
        frame->command_count = frame->group_count = 0;
    }
    if (requested && frame->source_skip_backend) frame->command_count = frame->group_count = 0;
    for (material_source_submission *row = pending; row; row = row->next) {
        if (row->context.source_model_assets) row->context.source_model_release(row->context.source_model_context);
        qa_scene_world_release(row->held_light_world); qa_material_release(row->original);
    }
    qa_scene_world_release(pending_world);
    for (material_source_operation *operation = source->operations; operation; operation = operation->next) {
        for (material_source_submission *row = operation->head; row; row = row->next) {
            if (row->context.source_model_assets) row->context.source_model_release(row->context.source_model_context);
            qa_scene_world_release(row->held_light_world); qa_material_release(row->original);
        }
        qa_scene_world_release(operation->view.world);
    }
    source->head = source->tail = NULL; source->submission_count = 0;
    source->operations = source->last_operation = NULL;
    source->sky = (qa_material_context){0}; source->frame = NULL;
    source->view = (material_source_view){0};
    source->issued_count = 0; source->issue_started = false;
    source->collecting = source->pictures = source->dispatching = source->issuing = false;
    frame->source_pending = NULL;
    material_source_order_detach(source->queued_order, source);
    material_source_leave(source);
    return ok;
}
bool qa_material_source_swap_end(qa_material_source_scratch *source, qa_scene_frame *frame, qa_error *error)
{
    if (!source || !frame) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source flush requires its actual renderer frame"); return false;
    }
    if (source->entered || source->index_count) frame->source_backend = true;
    if (source->runtime_frame_policy &&
        !source->runtime_frame_policy(source->runtime_frame_context, frame, error)) return false;
    if (source->entered && !qa_material_source_frame_end(source, frame, true, error)) return false;
    if (frame->source_skip_backend || !source->index_count) return true;
    if (!material_source_enter(source, error)) return false;
    source->frame = frame; frame->source_pending = source;
    source->submitting = false;
    source->dispatching = source->issuing = true;
    source->issued_count = 0; source->issue_started = false;
    bool ok = source_flush_pending(source, frame, error);
    if (source->issue_started) {
        qa_error cleanup = {0};
        bool finished = material_source_execute_prefix(source, frame, true, ok ? error : &cleanup);
        if (ok && !finished) ok = false;
        frame->command_count = frame->group_count = 0;
    }
    source->frame = NULL; frame->source_pending = NULL;
    source->dispatching = source->issuing = false;
    source->issued_count = 0; source->issue_started = false;
    material_source_leave(source);
    return ok;
}
bool qa_material_submit(const qa_material *original, const qa_scene_mesh *mesh,
                         const qa_material_context *context, qa_scene_frame *frame, qa_error *error)
{
    if (original == NULL || mesh == NULL || context == NULL || frame == NULL ||
        (mesh->vertex_count != 0 && mesh->vertices == NULL) ||
        (mesh->index_count != 0 && mesh->indices == NULL) || mesh->primitive != QA_SCENE_TRIANGLES ||
        mesh->index_count % 3 != 0 || context->light_count > 32 ||
        (context->light_count != 0 && context->lights == NULL) ||
        (context->fragment_light_count != 0 && context->fragment_lights == NULL) ||
        !isfinite(context->seconds) || !isfinite(context->identity_light) ||
        !qa_vec_finite(context->ambient) || !qa_vec_finite(context->directed) ||
        !qa_vec_finite(context->light_direction)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid material submission");
        return false;
    }
    for (size_t i = 0; i < mesh->index_count; ++i) if (mesh->indices[i] >= mesh->vertex_count) {
        qa_error_set(error, QA_ERROR_FORMAT, i, "Material mesh index exceeds vertex array");
        return false;
    }
    if (context->source_scratch && context->source_scratch->collecting)
        return source_collect(original, mesh, context, frame, error);
    if ((mesh->vertex_count == 0 || mesh->index_count == 0) &&
        !(context->source_scratch && context->source_writer == QA_SOURCE_WRITE_CLOUD)) return true;
    if (!context->source_scratch && frame->source_pending &&
        !qa_material_source_swap_end(frame->source_pending, frame, error)) return false;
    const qa_material *material = original;
    if (material->remapped != NULL) material = material->remapped;
    if ((material->surface_flags & 128u) != 0) return true;
    double seconds = shader_seconds(original, material, context);
    if (material->clamp_time != 0.0f && seconds >= material->clamp_time) seconds = material->clamp_time;
    float time = (float)seconds;
    if (!isfinite(time)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material shader time is nonfinite");
        return false;
    }
    if (context->source_scratch) {
        return submit_source(original, material, mesh, context, time, frame, error);
    }
    qa_scene_mesh geometry;
    if (!qa_material_deform_mesh(material, mesh, context, time, frame, &geometry, error)) return false;
    if (geometry.vertex_count == 0 || geometry.index_count == 0) return true;
    for (size_t i = 0; i < geometry.vertex_count; ++i) if (!qa_vec_finite(geometry.vertices[i].position) || !qa_vec_finite(geometry.vertices[i].normal)) {
        qa_error_set(error, QA_ERROR_FORMAT, i, "Material deformation generated nonfinite geometry");
        return false;
    }
    size_t command_start = frame->command_count, image_start = frame->image_count;
    if (execute_material(material, original, &geometry, context, time, frame, error)) return true;
    /* A failed material never leaves a partial multipass draw in the frame. */
    frame->command_count = command_start;
    while (frame->image_count > image_start) qa_scene_image_release(frame->images[--frame->image_count]);
    return false;
}
bool qa_material_shadow_mesh(const qa_material *material, const qa_scene_mesh *mesh,
                              const qa_material_context *context, qa_scene_frame *frame,
                              qa_scene_mesh *out, qa_error *error)
{
    if (material == NULL || mesh == NULL || context == NULL || frame == NULL || out == NULL ||
        !isfinite(context->seconds)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid material shadow geometry request");
        return false;
    }
    const qa_material *original = material;
    if (material->remapped != NULL) material = material->remapped;
    bool excluded = material->sky || material->sort > 3.0f ||
        (material->surface_flags & 128u) != 0 ||
        (material->content_flags & (UINT32_C(0x20000000) | 8u | 16u | 32u)) != 0;
    for (size_t i = 0; i < material->deform_count; ++i)
        if (material->deforms[i].kind == QA_DEFORM_AUTOSPRITE ||
            material->deforms[i].kind == QA_DEFORM_AUTOSPRITE2 ||
            material->deforms[i].kind == QA_DEFORM_PROJECTION_SHADOW) excluded = true;
    if (excluded) { *out = (qa_scene_mesh){0}; return true; }
    double seconds = shader_seconds(original, material, context);
    if (material->clamp_time != 0.0f && seconds >= material->clamp_time) seconds = material->clamp_time;
    if (!isfinite((float)seconds)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material shadow time is nonfinite");
        return false;
    }
    qa_scene_mesh geometry;
    if (!qa_material_deform_mesh(material, mesh, context, (float)seconds, frame, &geometry, error)) return false;
    for (size_t i = 0; i < geometry.vertex_count; ++i) if (!qa_vec_finite(geometry.vertices[i].position)) {
        qa_error_set(error, QA_ERROR_FORMAT, i, "Material shadow deformation is nonfinite");
        return false;
    }
    *out = geometry;
    return true;
}

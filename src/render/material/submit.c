#include "internal.h"
#include <math.h>
#include <stdalign.h>
#include <string.h>

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
static bool texture(const qa_material_stage *stage, const qa_material_context *context,
                    float time, const qa_scene_image **out, qa_error *error)
{
    if (stage->retain_texture) { *out = NULL; return true; }
    if (stage->video && context->video_frame != NULL && stage->video_identity != 0) {
        *out = context->video_frame(context->video_context, stage->video_identity, context->seconds, error);
        return *out != NULL;
    }
    if (stage->video && context->video_image != NULL && stage->video_name != NULL) {
        *out = context->video_image(context->video_context, stage->video_name, context->seconds, error);
        return *out != NULL;
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
    draw.environment = QA_TEXTURE_MODULATE;
    draw.lighting = QA_LIGHT_VERTEX;
    draw.shade_scale = 1.0f;
    draw.entity = context->entity;
    draw.fog_index = context->fog_index;
    draw.light_mask = context->light_mask;
    draw.sort_key = ((uint64_t)original->sorted_index << 17) | ((uint64_t)context->entity << 7) |
                    ((uint64_t)context->fog_index << 2) | (context->light_mask != 0 ? 1u : 0u);
    qa_scene_state_default(&draw.state);
    draw.state.cull = material_cull(material, context);
    draw.state.polygon_offset = material->polygon_offset;
    draw.state.offset_factor = -1.0f;
    draw.state.offset_units = -2.0f;
    return draw;
}
static bool emit_stage(const qa_material *material, const qa_material *original,
                        const qa_material_stage *stage, const qa_material_stage *second,
                        qa_scene_texture_environment environment, qa_scene_state state,
                        const qa_scene_mesh *geometry, const qa_material_context *context,
                        float time, qa_scene_vec4 *previous_colors,
                        qa_scene_frame *frame, qa_error *error)
{
    if (stage->invalid_blend || (second != NULL && second->invalid_blend)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Material blend function has an uninitialized source factor");
        return false;
    }
    const qa_material_stage *first_binding = stage, *second_binding = second;
    if (second != NULL && stage->is_lightmap) { first_binding = second; second_binding = stage; }
    qa_scene_draw draw = initial_draw(material, original, *geometry, context);
    draw.mesh.identity = draw.mesh.revision = 0;
    draw.environment = environment;
    qa_scene_cull cull = draw.state.cull;
    draw.state = state;
    draw.state.cull = cull;
    draw.state.polygon_offset = material->polygon_offset;
    draw.state.offset_factor = -1.0f;
    draw.state.offset_units = -2.0f;
    if (!texture(first_binding, context, time, &draw.textures[0], error)) return false;
    draw.retain_texture[0] = first_binding->retain_texture;
    draw.texture_count = 1;
    if (second_binding != NULL) {
        if (!texture(second_binding, context, time, &draw.textures[1], error)) return false;
        draw.retain_texture[1] = second_binding->retain_texture;
        draw.texture_count = 2;
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
    qa_scene_vertex *vertices = frame_array(frame, geometry->vertex_count, sizeof(*vertices), alignof(qa_scene_vertex), error);
    if (vertices == NULL) return false;
    for (size_t i = 0; i < geometry->vertex_count; ++i) {
        vertices[i] = geometry->vertices[i];
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
    return qa_scene_frame_draw(frame, &draw, error);
}
static float dlight_byte(float component)
{
    return (float)((uint32_t)(int32_t)component & 255u) / 255.0f;
}
static bool emit_dlights(const qa_material *material, const qa_material *original,
                          const qa_scene_mesh *geometry, const qa_material_context *context,
                          bool fast_iterator, qa_scene_frame *frame, qa_error *error)
{
    if (context->light_mask == 0 || material->sort > 3.0f ||
        (!fast_iterator && (material->surface_flags & (UINT32_C(0x20000) | 4u)) != 0)) return true;
    if (material->dlight_image == NULL) {
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
        draw.textures[0] = material->dlight_image;
        draw.texture_count = 1;
        draw.state.blend_source = light->additive ? QA_BLEND_ONE : QA_BLEND_DST_COLOR;
        draw.state.blend_destination = QA_BLEND_ONE;
        draw.state.depth_test = QA_DEPTH_EQUAL;
        draw.state.depth_write = false;
        if (context->fog.kind != QA_FOG_NONE) {
            draw.fog = context->fog;
            draw.fog.effect = QA_FOG_NO_EFFECT;
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
    qa_scene_vertex *vertices = frame_array(frame, geometry->vertex_count, sizeof(*vertices), alignof(qa_scene_vertex), error);
    if (vertices == NULL) return false;
    if (volume) {
        if (material->fog_image == NULL) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Fog lookup texture is unavailable");
            return false;
        }
        draw.texture_count = 1;
        draw.textures[0] = material->fog_image;
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
    }
    draw.mesh.vertices = vertices;
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
static bool execute_material(const qa_material *material, const qa_material *original,
                              const qa_scene_mesh *geometry, const qa_material_context *context,
                              float time, qa_scene_frame *frame, qa_error *error)
{
    qa_scene_vec4 *previous = frame_array(frame, geometry->vertex_count, sizeof(*previous), alignof(qa_scene_vec4), error);
    if (previous == NULL) return false;
    memset(previous, 0, geometry->vertex_count * sizeof(*previous));
    qa_scene_state collapsed_state = {0};
    qa_scene_texture_environment collapsed_environment = QA_TEXTURE_MODULATE;
    bool collapsed = material->profile.multitexture && !context->fragment_lighting && material->stage_count >= 2 &&
        active_stage(&material->stages[0]) && active_stage(&material->stages[1]) &&
        collapse_stages(&material->stages[0], &material->stages[1], &collapsed_environment, &collapsed_state);
    if (collapsed && collapsed_environment == QA_TEXTURE_ADD && !material->profile.texture_env_add) collapsed = false;
    size_t pass_count = material->stage_count - (collapsed ? 1u : 0u);
    bool fast_iterator = false;
    if (!material->profile.ignore_fast_path && !context->fragment_lighting && pass_count == 1 &&
        !material->sky && !material->polygon_offset && material->deform_count == 0) {
        const qa_material_stage *first = &material->stages[0];
        fast_iterator = (first->rgb == QA_COLOR_LIGHTING_DIFFUSE && first->alpha == QA_COLOR_IDENTITY &&
                         first->tcgen == QA_TC_TEXTURE && !collapsed) ||
            (collapsed && first->rgb == QA_COLOR_IDENTITY && first->alpha == QA_COLOR_IDENTITY &&
             ((first->tcgen == QA_TC_TEXTURE && material->stages[1].tcgen == QA_TC_LIGHTMAP) ||
              (first->tcgen == QA_TC_LIGHTMAP && material->stages[1].tcgen == QA_TC_TEXTURE)));
    }
    for (size_t i = 0; i < material->stage_count; ++i) {
        const qa_material_stage *stage = &material->stages[i];
        if (!active_stage(stage)) continue;
        const qa_material_stage *second = i == 0 && collapsed ? &material->stages[1] : NULL;
        if (!emit_stage(material, original, stage, second,
                         second == NULL ? QA_TEXTURE_MODULATE : collapsed_environment,
                         second == NULL ? stage->state : collapsed_state,
                         geometry, context, time, previous, frame, error)) return false;
        if (second != NULL) ++i;
    }
    return emit_dlights(material, original, geometry, context, fast_iterator, frame, error) &&
           emit_fog(material, original, geometry, context, frame, error);
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
    if (mesh->vertex_count == 0 || mesh->index_count == 0) return true;
    for (size_t i = 0; i < mesh->index_count; ++i) if (mesh->indices[i] >= mesh->vertex_count) {
        qa_error_set(error, QA_ERROR_FORMAT, i, "Material mesh index exceeds vertex array");
        return false;
    }
    const qa_material *material = original;
    float offset = context->time_offset;
    for (size_t hop = 0; material->remapped != NULL; ++hop) {
        if (hop >= 16384) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Material remap chain contains a cycle");
            return false;
        }
        offset += material->remap_time_offset;
        material = material->remapped;
    }
    if ((material->surface_flags & 128u) != 0) return true;
    double seconds = context->seconds - offset;
    if (material->clamp_time != 0.0f && seconds >= material->clamp_time) seconds = material->clamp_time;
    float time = (float)seconds;
    if (!isfinite(time)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Material shader time is nonfinite");
        return false;
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
    float offset = context->time_offset;
    for (size_t hop = 0; material->remapped != NULL; ++hop) {
        if (hop >= 16384) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Material shadow remap chain contains a cycle");
            return false;
        }
        offset += material->remap_time_offset;
        material = material->remapped;
    }
    bool excluded = material->sky || material->sort > 3.0f ||
        (material->surface_flags & 128u) != 0 ||
        (material->content_flags & (UINT32_C(0x20000000) | 8u | 16u | 32u)) != 0;
    for (size_t i = 0; i < material->deform_count; ++i)
        if (material->deforms[i].kind == QA_DEFORM_AUTOSPRITE ||
            material->deforms[i].kind == QA_DEFORM_AUTOSPRITE2 ||
            material->deforms[i].kind == QA_DEFORM_PROJECTION_SHADOW) excluded = true;
    if (excluded) { *out = (qa_scene_mesh){0}; return true; }
    double seconds = context->seconds - offset;
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

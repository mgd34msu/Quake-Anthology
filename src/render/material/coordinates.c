/* Q3 stage coordinates adapted from tr_shade_calc.c.
 * SPDX-License-Identifier: GPL-2.0-or-later */
#include "internal.h"
#include <math.h>

qa_scene_vec2 qa_material_fog_coordinates(const qa_material_context *context, qa_vec3 local_position)
{
    qa_scene_vec4 point = qa_scene_matrix_point(context->model, local_position);
    qa_vec3 position = qa_v3(point.x, point.y, point.z);
    qa_vec3 distance_vector = qa_vec_scale(context->view.axis[0], context->fog_tc_scale);
    float offset = qa_vec_dot(qa_vec_scale(context->view.origin, -1.0f), context->view.axis[0]);
    offset = offset * context->fog_tc_scale + 1.0f / 512.0f;
    float s = qa_vec_dot(position, distance_vector) + offset;
    if (!context->fog_has_surface) return (qa_scene_vec2){s, 31.0f / 32.0f};
    float eye_depth = qa_vec_dot(context->view.origin, context->fog_surface.normal) - context->fog_surface.distance;
    float depth = qa_vec_dot(position, context->fog_surface.normal) - context->fog_surface.distance;
    float t;
    if (eye_depth < 0.0f) t = depth < 1.0f ? 1.0f / 32.0f :
        1.0f / 32.0f + (30.0f / 32.0f * depth) / (depth - eye_depth);
    else t = depth < 0.0f ? 1.0f / 32.0f : 31.0f / 32.0f;
    return (qa_scene_vec2){s, t};
}
static bool table_index(double value, unsigned *out, qa_error *error)
{
    if (!isfinite(value) || (double)value < -2147483648.0 || (double)value >= 2147483648.0) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Material texture table index is outside source integer range");
        return false;
    }
    *out = (unsigned)(int32_t)value & 1023u;
    return true;
}
bool qa_material_stage_texcoord(const qa_material_stage *stage, const qa_scene_vertex *vertex,
                                const qa_material_context *context, float time,
                                qa_scene_vec2 *out, qa_error *error)
{
    qa_scene_vec2 result;
    switch (stage->tcgen) {
    case QA_TC_TEXTURE: result = vertex->texcoord; break;
    case QA_TC_LIGHTMAP: result = vertex->lightmap; break;
    case QA_TC_IDENTITY: result = (qa_scene_vec2){0, 0}; break;
    case QA_TC_FOG:
        if (context->fog_tc_scale <= 0.0f) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Fog texture coordinates require a fog volume");
            return false;
        }
        result = qa_material_fog_coordinates(context, vertex->position);
        break;
    case QA_TC_VECTOR:
        result = (qa_scene_vec2){qa_vec_dot(vertex->position, stage->tc_vectors[0]),
                                qa_vec_dot(vertex->position, stage->tc_vectors[1])};
        break;
    case QA_TC_ENVIRONMENT: {
        qa_vec3 viewer = qa_material_fast_normalize(qa_vec_sub(context->local_view_origin, vertex->position));
        float dot = qa_vec_dot(vertex->normal, viewer);
        float reflected_y = (vertex->normal.y * 2.0f) * dot - viewer.y;
        float reflected_z = (vertex->normal.z * 2.0f) * dot - viewer.z;
        result = (qa_scene_vec2){0.5f + reflected_y * 0.5f, 0.5f - reflected_z * 0.5f};
        break;
    }
    default:
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Unknown material texture coordinate generator");
        return false;
    }
    for (size_t i = 0; i < stage->tcmod_count; ++i) {
        const qa_material_tcmod *mod = &stage->tcmods[i];
        float s = result.x, t = result.y;
        switch (mod->kind) {
        case QA_TCMOD_NONE: goto finished;
        case QA_TCMOD_SCALE:
            result = (qa_scene_vec2){s * mod->values[0], t * mod->values[1]};
            break;
        case QA_TCMOD_SCROLL:
        case QA_TCMOD_ENTITY_TRANSLATE: {
            float x = (mod->kind == QA_TCMOD_SCROLL ? mod->values[0] : context->entity_texcoord.x) * time;
            float y = (mod->kind == QA_TCMOD_SCROLL ? mod->values[1] : context->entity_texcoord.y) * time;
            result = (qa_scene_vec2){s + (x - floorf(x)), t + (y - floorf(y))};
            break;
        }
        case QA_TCMOD_TRANSFORM:
            result = (qa_scene_vec2){s * mod->values[0] + t * mod->values[2] + mod->values[4],
                                    s * mod->values[1] + t * mod->values[3] + mod->values[5]};
            break;
        case QA_TCMOD_ROTATE: {
            unsigned index;
            if (!table_index((-mod->values[0] * time) * (1024.0f / 360.0f), &index, error)) return false;
            float sine = qa_material_sine(index), cosine = qa_material_sine(index + 256u);
            float x_translate = (float)(0.5 - 0.5 * cosine + 0.5 * sine);
            float y_translate = (float)(0.5 - 0.5 * sine - 0.5 * cosine);
            result = (qa_scene_vec2){s * cosine + t * -sine + x_translate,
                                    s * sine + t * cosine + y_translate};
            break;
        }
        case QA_TCMOD_STRETCH: {
            float scale = 1.0f / qa_material_wave_evaluate(&mod->wave, time);
            float translate = 0.5f - 0.5f * scale;
            result = (qa_scene_vec2){s * scale + t * 0.0f + translate,
                                    s * 0.0f + t * scale + translate};
            break;
        }
        case QA_TCMOD_TURBULENCE: {
            float now = mod->wave.phase + time * mod->wave.frequency;
            unsigned sx, sy;
            /* The source position sum is binary32 before the double constant division. */
            float position_sum = vertex->position.x + vertex->position.z;
            if (!table_index(((double)position_sum / 1024.0 + now) * 1024.0, &sx, error) ||
                !table_index(((double)vertex->position.y / 1024.0 + now) * 1024.0, &sy, error)) return false;
            result = (qa_scene_vec2){s + qa_material_sine(sx) * mod->wave.amplitude,
                                    t + qa_material_sine(sy) * mod->wave.amplitude};
            break;
        }
        default:
            qa_error_set(error, QA_ERROR_FORMAT, i, "Unknown material texture modifier");
            return false;
        }
    }
finished:
    if (!isfinite(result.x) || !isfinite(result.y)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Material generated nonfinite texture coordinates");
        return false;
    }
    *out = result;
    return true;
}

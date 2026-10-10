#include "internal.h"
#include <math.h>

qa_vec2 qa_material_fog_coordinates(const qa_material_context *context, qa_vec3 local_position)
{
    qa_scene_vec4 point = qa_scene_matrix_point(context->model, local_position);
    qa_vec3 position = qa_v3(point.x, point.y, point.z);
    qa_vec3 distance_vector = qa_vec_scale(context->view.axis[0], context->fog_tc_scale);
    float offset = qa_vec_dot(qa_vec_scale(context->view.origin, -1.0f), context->view.axis[0]);
    offset = offset * context->fog_tc_scale + 1.0f / 512.0f;
    float s = qa_vec_dot(position, distance_vector) + offset;
    if (!context->fog_has_surface) return (qa_vec2){s, 31.0f / 32.0f};
    float eye_depth = qa_vec_dot(context->view.origin, context->fog_surface.normal) - context->fog_surface.distance;
    float depth = qa_vec_dot(position, context->fog_surface.normal) - context->fog_surface.distance;
    float t;
    if (eye_depth < 0.0f) t = depth < 1.0f ? 1.0f / 32.0f :
        1.0f / 32.0f + (30.0f / 32.0f * depth) / (depth - eye_depth);
    else t = depth < 0.0f ? 1.0f / 32.0f : 31.0f / 32.0f;
    return (qa_vec2){s, t};
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
void material_tcmod_prepare(const qa_material_tcmod *mod, const qa_material_context *context,
                             float time, material_tcmod_state *state)
{
    *state = (material_tcmod_state){0};
    switch (mod->kind) {
    case QA_TCMOD_SCROLL:
    case QA_TCMOD_ENTITY_TRANSLATE: {
        float x = (mod->kind == QA_TCMOD_SCROLL ? mod->values[0] : context->entity_texcoord.x) * time;
        float y = (mod->kind == QA_TCMOD_SCROLL ? mod->values[1] : context->entity_texcoord.y) * time;
        state->scroll = (qa_vec2){x - floorf(x), y - floorf(y)};
        break;
    }
    case QA_TCMOD_ROTATE: {
        unsigned index;
        state->rotation = (-mod->values[0] * time) * (1024.0f / 360.0f);
        state->rotation_valid = table_index(state->rotation, &index, NULL);
        if (!state->rotation_valid) break;
        state->sine = qa_material_sine(index); state->cosine = qa_material_sine(index + 256u);
        state->translate = (qa_vec2){(float)(0.5 - 0.5 * state->cosine + 0.5 * state->sine),
            (float)(0.5 - 0.5 * state->sine - 0.5 * state->cosine)};
        break;
    }
    case QA_TCMOD_STRETCH:
        state->scale = 1.0f / qa_material_wave_evaluate(&mod->wave, time);
        state->translate.x = 0.5f - 0.5f * state->scale;
        break;
    case QA_TCMOD_TURBULENCE:
        state->now = mod->wave.phase + time * mod->wave.frequency;
        break;
    default: break;
    }
}
void material_tcmods_prepare(const qa_material_stage *stage, const qa_material_context *context,
                             float time, material_tcmod_state *states)
{
    for (size_t i = 0; i < stage->tcmod_count; ++i) {
        if (stage->tcmods[i].kind == QA_TCMOD_NONE) break;
        material_tcmod_prepare(stage->tcmods + i, context, time, states + i);
    }
}
bool material_texcoord_generate(const qa_material_stage *stage, const qa_scene_vertex *vertex,
                               const qa_material_context *context, qa_vec2 *out, qa_error *error)
{
    qa_vec2 result;
    switch (stage->tcgen) {
    case QA_TC_TEXTURE: result = vertex->texcoord; break;
    case QA_TC_LIGHTMAP: result = vertex->lightmap; break;
    case QA_TC_IDENTITY: result = (qa_vec2){0, 0}; break;
    case QA_TC_FOG:
        if (context->fog_tc_scale <= 0.0f) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Fog texture coordinates require a fog volume");
            return false;
        }
        result = qa_material_fog_coordinates(context, vertex->position);
        break;
    case QA_TC_VECTOR:
        result = (qa_vec2){qa_vec_dot(vertex->position, stage->tc_vectors[0]),
                                qa_vec_dot(vertex->position, stage->tc_vectors[1])};
        break;
    case QA_TC_ENVIRONMENT: {
        qa_vec3 viewer = qa_material_fast_normalize(qa_vec_sub(context->local_view_origin, vertex->position));
        float dot = qa_vec_dot(vertex->normal, viewer);
        float reflected_y = (vertex->normal.y * 2.0f) * dot - viewer.y;
        float reflected_z = (vertex->normal.z * 2.0f) * dot - viewer.z;
        result = (qa_vec2){0.5f + reflected_y * 0.5f, 0.5f - reflected_z * 0.5f};
        break;
    }
    default:
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Unknown material texture coordinate generator");
        return false;
    }
    *out = result;
    return true;
}
bool material_texcoord_modify(const qa_material_tcmod *mod, size_t mod_index, qa_vec3 position,
                             const material_tcmod_state *state, const qa_vec2 *input,
                             qa_vec2 *out, qa_error *error)
{
    qa_vec2 result = *input;
    float s = result.x, t = result.y;
    switch (mod->kind) {
    case QA_TCMOD_NONE: break;
    case QA_TCMOD_SCALE:
        result = (qa_vec2){s * mod->values[0], t * mod->values[1]};
        break;
    case QA_TCMOD_SCROLL:
    case QA_TCMOD_ENTITY_TRANSLATE: {
        result = (qa_vec2){s + state->scroll.x, t + state->scroll.y};
        break;
    }
    case QA_TCMOD_TRANSFORM:
        result = (qa_vec2){s * mod->values[0] + t * mod->values[2] + mod->values[4],
                                s * mod->values[1] + t * mod->values[3] + mod->values[5]};
        break;
    case QA_TCMOD_ROTATE: {
        if (!state->rotation_valid) {
            unsigned index;
            (void)table_index(state->rotation, &index, error);
            return false;
        }
        result.x = s * state->cosine - t * state->sine + state->translate.x;
        result.y = s * state->sine + t * state->cosine + state->translate.y;
        break;
    }
    case QA_TCMOD_STRETCH: {
        result = (qa_vec2){s * state->scale + t * 0.0f + state->translate.x,
                                s * 0.0f + t * state->scale + state->translate.x};
        break;
    }
    case QA_TCMOD_TURBULENCE: {
        unsigned sx, sy;
        /* The source position sum is binary32 before the double constant division. */
        float position_sum = position.x + position.z;
        if (!table_index(((double)position_sum / 1024.0 + state->now) * 1024.0, &sx, error) ||
            !table_index(((double)position.y / 1024.0 + state->now) * 1024.0, &sy, error)) return false;
        result = (qa_vec2){s + qa_material_sine(sx) * mod->wave.amplitude,
                                t + qa_material_sine(sy) * mod->wave.amplitude};
        break;
    }
    default:
        qa_error_set(error, QA_ERROR_FORMAT, mod_index, "Unknown material texture modifier");
        return false;
    }
    *out = result;
    return true;
}
bool material_texcoord_vertex(const qa_material_stage *stage, const qa_scene_vertex *vertex,
                               const qa_material_context *context, float time,
                               const material_tcmod_state *states, qa_vec2 *out, qa_error *error)
{
    qa_vec2 result;
    if (!material_texcoord_generate(stage, vertex, context, &result, error)) return false;
    for (size_t i = 0; i < stage->tcmod_count; ++i) {
        const qa_material_tcmod *mod = &stage->tcmods[i];
        if (mod->kind == QA_TCMOD_NONE) break;
        material_tcmod_state local;
        const material_tcmod_state *state = states ? states + i : &local;
        if (!states) material_tcmod_prepare(mod, context, time, &local);
        if (!material_texcoord_modify(mod, i, vertex->position, state, &result, &result, error)) return false;
    }
    if (!material_texcoord_finite(result, error)) return false;
    *out = result;
    return true;
}
bool qa_material_stage_texcoord(const qa_material_stage *stage, const qa_scene_vertex *vertex,
                                const qa_material_context *context, float time,
                                qa_vec2 *out, qa_error *error)
{
    return material_texcoord_vertex(stage, vertex, context, time, NULL, out, error);
}

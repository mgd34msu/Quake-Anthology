#include "internal.h"
#include <fenv.h>
#include <math.h>

static bool byte_input_valid(float value, qa_error *error)
{
    if (!isfinite(value) || (double)value < -2147483648.0 || (double)value >= 2147483648.0) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Material color is outside source integer range");
        return false;
    }
    return true;
}
static bool byte_value(float value, float *out, qa_error *error)
{
    if (!byte_input_valid(value, error)) return false;
    *out = (float)((uint32_t)(int32_t)value & 255u) / 255.0f;
    return true;
}
static bool normalized_byte(float value, float *out, qa_error *error)
{
    return byte_value(value * 255.0f, out, error);
}
static bool wave_byte(float value, float *out, qa_error *error)
{
    if (isnan(value)) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Material waveform generated nonfinite color");
        return false;
    }
    return normalized_byte(fmaxf(0.0f, fminf(1.0f, value)), out, error);
}
static bool quantize(qa_vec4 input, qa_vec4 *out, qa_error *error)
{
    return normalized_byte(input.x, &out->x, error) && normalized_byte(input.y, &out->y, error) &&
           normalized_byte(input.z, &out->z, error) && normalized_byte(input.w, &out->w, error);
}
static bool source_component(float input, float *out, qa_error *error)
{
    float value = input * 255.0f;
    return out ? byte_value(roundf(value), out, error) : byte_input_valid(value, error);
}
static bool source_color(qa_vec4 input, qa_vec4 *out, qa_error *error)
{
    return source_component(input.x, out ? &out->x : NULL, error) &&
           source_component(input.y, out ? &out->y : NULL, error) &&
           source_component(input.z, out ? &out->z : NULL, error) &&
           source_component(input.w, out ? &out->w : NULL, error);
}
static float specular(const qa_scene_vertex *vertex, const qa_material_context *context)
{
    qa_vec3 light = qa_material_fast_normalize(qa_vec_sub(qa_v3(-960.0f, 1980.0f, 96.0f), vertex->position));
    float d = qa_vec_dot(vertex->normal, light);
    qa_vec3 reflected = qa_vec_sub(qa_vec_scale(qa_vec_scale(vertex->normal, 2.0f), d), light);
    qa_vec3 viewer = qa_vec_sub(context->local_view_origin, vertex->position);
    float amount = qa_vec_dot(reflected, viewer) * qa_material_inverse_sqrt(qa_vec_dot(viewer, viewer));
    if (amount < 0.0f) return 0.0f;
    amount *= amount;
    amount *= amount;
    return fminf(255.0f, truncf(amount * 255.0f)) / 255.0f;
}
bool material_color_prepare(const qa_material_stage *stage, const qa_material_context *context,
                            float time, material_color_state *state, qa_error *error)
{
    *state = (material_color_state){0};
    if (!source_color(context->entity_color, &state->entity, error)) return false;
    if (stage->rgb == QA_COLOR_WAVE) {
        if (stage->rgb_wave.kind == QA_WAVE_NOISE) {
            float sample_time = (time + stage->rgb_wave.phase) * stage->rgb_wave.frequency;
            state->rgb_wave = stage->rgb_wave.base + qa_material_noise(0, 0, 0, sample_time) * stage->rgb_wave.amplitude;
        } else state->rgb_wave = qa_material_wave_evaluate(&stage->rgb_wave, time) * context->identity_light;
    }
    if (stage->alpha == QA_COLOR_WAVE)
        state->alpha_wave = qa_material_wave_evaluate(&stage->alpha_wave, time);
    state->validate_unused_color = stage->rgb != QA_COLOR_VERTEX && stage->rgb != QA_COLOR_EXACT_VERTEX &&
        stage->rgb != QA_COLOR_ONE_MINUS_VERTEX && stage->alpha != QA_COLOR_VERTEX &&
        stage->alpha != QA_COLOR_EXACT_VERTEX && stage->alpha != QA_COLOR_ONE_MINUS_VERTEX &&
        (fetestexcept(FE_INEXACT) & FE_INEXACT) != 0;
    return true;
}
bool material_color_vertex(const qa_material_stage *stage, const qa_scene_vertex *vertex,
                           const qa_material_context *context, const material_color_state *state,
                           qa_vec4 previous, qa_vec4 *out, qa_error *error)
{
    qa_vec4 result = {0, 0, 0, previous.w};
    qa_vec4 entity = state->entity, color;
    if (!source_color(vertex->color, state->validate_unused_color ? NULL : &color, error)) return false;
    switch (stage->rgb) {
    case QA_COLOR_IDENTITY: result = (qa_vec4){1, 1, 1, 1}; break;
    case QA_COLOR_IDENTITY_LIGHTING:
    case QA_COLOR_BAD: {
        float value;
        if (!normalized_byte(context->identity_light, &value, error)) return false;
        result = (qa_vec4){value, value, value, value};
        break;
    }
    case QA_COLOR_VERTEX:
        if (!byte_value(roundf(color.x * 255.0f) * context->identity_light, &result.x, error) ||
            !byte_value(roundf(color.y * 255.0f) * context->identity_light, &result.y, error) ||
            !byte_value(roundf(color.z * 255.0f) * context->identity_light, &result.z, error)) return false;
        result.w = color.w;
        break;
    case QA_COLOR_EXACT_VERTEX: result = color; break;
    case QA_COLOR_ONE_MINUS_VERTEX:
        if (!byte_value((255.0f - roundf(color.x * 255.0f)) * context->identity_light, &result.x, error) ||
            !byte_value((255.0f - roundf(color.y * 255.0f)) * context->identity_light, &result.y, error) ||
            !byte_value((255.0f - roundf(color.z * 255.0f)) * context->identity_light, &result.z, error)) return false;
        break;
    case QA_COLOR_ENTITY: result = entity; break;
    case QA_COLOR_ONE_MINUS_ENTITY:
        result = (qa_vec4){1.0f - entity.x, 1.0f - entity.y, 1.0f - entity.z, 1.0f - entity.w};
        break;
    case QA_COLOR_CONSTANT:
        if (!quantize(stage->constant, &result, error)) return false;
        if (stage->alpha != QA_COLOR_CONSTANT) result.w = 0.0f;
        break;
    case QA_COLOR_WAVE: {
        float value = state->rgb_wave;
        if (!wave_byte(value, &value, error)) return false;
        result = (qa_vec4){value, value, value, 1};
        break;
    }
    case QA_COLOR_LIGHTING_DIFFUSE: {
        float incoming = qa_vec_dot(vertex->normal, context->light_direction);
        qa_vec3 ambient = qa_vec_scale(context->ambient, 255.0f), directed = qa_vec_scale(context->directed, 255.0f);
        if (incoming <= 0.0f) {
            if (!byte_value(ambient.x, &result.x, error) || !byte_value(ambient.y, &result.y, error) ||
                !byte_value(ambient.z, &result.z, error)) return false;
        } else {
            if (!byte_value(fminf(255.0f, ambient.x + incoming * directed.x), &result.x, error) ||
                !byte_value(fminf(255.0f, ambient.y + incoming * directed.y), &result.y, error) ||
                !byte_value(fminf(255.0f, ambient.z + incoming * directed.z), &result.z, error)) return false;
        }
        result.w = incoming <= 0.0f ? context->ambient_alpha : 1.0f;
        break;
    }
    case QA_COLOR_SKIP: result = previous; break;
    default:
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid material RGB generator");
        return false;
    }
    switch (stage->alpha) {
    case QA_COLOR_IDENTITY:
    case QA_COLOR_IDENTITY_LIGHTING:
        if (stage->rgb != QA_COLOR_IDENTITY &&
            (stage->rgb != QA_COLOR_VERTEX || context->identity_light != 1.0f)) result.w = 1.0f;
        break;
    case QA_COLOR_ENTITY: result.w = entity.w; break;
    case QA_COLOR_ONE_MINUS_ENTITY: result.w = 1.0f - entity.w; break;
    case QA_COLOR_VERTEX:
    case QA_COLOR_EXACT_VERTEX: result.w = color.w; break;
    case QA_COLOR_ONE_MINUS_VERTEX: result.w = 1.0f - color.w; break;
    case QA_COLOR_CONSTANT:
        if (!normalized_byte(stage->constant.w, &result.w, error)) return false;
        break;
    case QA_COLOR_WAVE:
        if (!wave_byte(state->alpha_wave, &result.w, error)) return false;
        break;
    case QA_COLOR_PORTAL: {
        /* Portal alpha deliberately compares local tess positions to the world
         * view origin, matching the source helper's retained input. */
        float range = stage->portal_range;
        float distance = qa_vec_length(qa_vec_sub(vertex->position, context->view.origin));
        if (!wave_byte(distance / range, &result.w, error)) return false;
        break;
    }
    case QA_COLOR_LIGHTING_SPECULAR: result.w = specular(vertex, context); break;
    case QA_COLOR_SKIP: break;
    default:
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid material alpha generator");
        return false;
    }
    *out = result;
    return true;
}
bool qa_material_stage_color(const qa_material_stage *stage, const qa_scene_vertex *vertex,
                             const qa_material_context *context, float time,
                             qa_vec4 previous, qa_vec4 *out, qa_error *error)
{
    material_color_state state;
    return material_color_prepare(stage, context, time, &state, error) &&
        material_color_vertex(stage, vertex, context, &state, previous, out, error);
}

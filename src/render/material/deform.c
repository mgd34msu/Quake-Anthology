#include "internal.h"
#include "source_scratch_private.h"

#include <math.h>
#include <string.h>

typedef struct deform_mesh {
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    qa_material_source_scratch *retained;
} deform_mesh;

static qa_vec3 model_axis(const qa_material_context *context, unsigned axis)
{
    const float *m = context->model.m + axis * 4u;
    return qa_v3(m[0], m[1], m[2]);
}

static qa_vec3 local_direction(const qa_material_context *context, qa_vec3 direction)
{
    return qa_v3(qa_vec_dot(direction, model_axis(context, 0)),
                 qa_vec_dot(direction, model_axis(context, 1)),
                 qa_vec_dot(direction, model_axis(context, 2)));
}

/* These two source operations retain a double multiplier until vector storage. */
static qa_vec3 scale_double(qa_vec3 value, double scale)
{
    return qa_v3((float)((double)value.x * scale),
                 (float)((double)value.y * scale),
                 (float)((double)value.z * scale));
}

static qa_vec3 normalize(qa_vec3 value)
{
    float length = qa_vec_length(value);
    return length == 0.0f ? value : qa_vec_scale(value, 1.0f / length);
}

static bool allocate_geometry(qa_scene_frame *frame, size_t vertex_count,
                              size_t index_count, deform_mesh *out,
                              qa_error *error)
{
    if (out->retained) {
        if (vertex_count >= QA_SOURCE_TESS_VERTICES || index_count >= QA_SOURCE_TESS_INDEXES) {
            qa_error_set(error, QA_ERROR_FORMAT, 0, "Deformed Source geometry exceeds tess allocation"); return false;
        }
        out->vertices = out->retained->vertices; out->indices = out->retained->indices;
        out->mesh.vertices = out->vertices; out->mesh.indices = out->indices;
        out->mesh.vertex_count = out->retained->vertex_count = vertex_count;
        out->mesh.index_count = out->retained->index_count = index_count;
        out->mesh.identity = out->mesh.revision = 0; out->mesh.geometry = NULL;
        return true;
    }
    if (vertex_count > UINT32_MAX || vertex_count > SIZE_MAX / sizeof(*out->vertices) ||
        index_count > SIZE_MAX / sizeof(*out->indices)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Material deformation geometry is too large");
        return false;
    }
    qa_scene_vertex *vertices = NULL;
    uint32_t *indices = NULL;
    if (vertex_count != 0) {
        vertices = qa_arena_alloc(&frame->storage, vertex_count * sizeof(*vertices),
                                  _Alignof(qa_scene_vertex), error);
        if (vertices == NULL) return false;
    }
    if (index_count != 0) {
        indices = qa_arena_alloc(&frame->storage, index_count * sizeof(*indices),
                                 _Alignof(uint32_t), error);
        if (indices == NULL) return false;
    }
    out->vertices = vertices;
    out->indices = indices;
    out->mesh.vertices = vertices;
    out->mesh.indices = indices;
    out->mesh.vertex_count = vertex_count;
    out->mesh.index_count = index_count;
    out->mesh.identity = 0;
    out->mesh.revision = 0;
    return true;
}

static bool require_quads(const deform_mesh *geometry, const char *kind, qa_error *error)
{
    if (geometry->mesh.primitive != QA_SCENE_TRIANGLES ||
        geometry->mesh.vertex_count % 4u != 0 ||
        geometry->mesh.index_count % 6u != 0 ||
        geometry->mesh.index_count / 6u != geometry->mesh.vertex_count / 4u) {
        qa_error_set(error, QA_ERROR_FORMAT, 0,
                     "%s requires independent four-vertex quads", kind);
        return false;
    }
    return true;
}

static void stamp_quad(deform_mesh *geometry, size_t start, qa_vec3 center,
                       qa_vec3 left, qa_vec3 up, qa_vec3 normal,
                       qa_vec4 color, float s, float t, float step)
{
    qa_vec3 positions[4] = {
        qa_vec_add(qa_vec_add(center, left), up),
        qa_vec_add(qa_vec_sub(center, left), up),
        qa_vec_sub(qa_vec_sub(center, left), up),
        qa_vec_sub(qa_vec_add(center, left), up)
    };
    qa_vec2 coords[4] = {
        {s, t}, {s + step, t}, {s + step, t + step}, {s, t + step}
    };
    for (size_t corner = 0; corner < 4; ++corner) {
        geometry->vertices[start + corner] = (qa_scene_vertex){
            .position = positions[corner], .normal = normal,
            .texcoord = coords[corner], .lightmap = coords[corner], .color = color
        };
    }
    uint32_t base = (uint32_t)start;
    uint32_t *indices = geometry->indices + start / 4u * 6u;
    indices[0] = base;
    indices[1] = base + 1u;
    indices[2] = base + 3u;
    indices[3] = base + 3u;
    indices[4] = base + 1u;
    indices[5] = base + 2u;
}

static bool autosprite(deform_mesh *geometry, const qa_material_context *context,
                       qa_scene_frame *frame, qa_error *error)
{
    if (!geometry->retained && !require_quads(geometry, "autosprite", error)) return false;
    size_t count = geometry->mesh.vertex_count;
    if (geometry->retained) geometry->retained->vertex_count = geometry->retained->index_count = 0;
    qa_vec3 left_direction = local_direction(context, context->view.axis[1]);
    qa_vec3 up_direction = local_direction(context, context->view.axis[2]);
    qa_vec3 normal = qa_vec_sub(qa_v3(0, 0, 0), context->view.axis[0]);
    float axis_scale = 1.0f;
    if (context->non_normalized_axis) {
        float length = qa_vec_length(model_axis(context, 0));
        axis_scale = length == 0.0f ? 0.0f : 1.0f / length;
    }
    for (size_t start = 0; start < count; start += 4) {
        if (start > QA_SOURCE_TESS_VERTICES - 4 && geometry->retained) {
            qa_error_set(error, QA_ERROR_FORMAT, start, "Source autosprite read exceeds its physical cells"); return false;
        }
        qa_scene_vertex first = geometry->vertices[start];
        qa_vec3 first_pair = qa_vec_add(first.position, geometry->vertices[start + 1].position);
        qa_vec3 second_pair = qa_vec_add(geometry->vertices[start + 2].position,
                                          geometry->vertices[start + 3].position);
        qa_vec3 center = qa_vec_scale(qa_vec_add(first_pair, second_pair), 0.25f);
        qa_vec3 delta = qa_vec_sub(first.position, center);
        float radius = (float)(sqrt((double)qa_vec_dot(delta, delta)) * 0.707);
        qa_vec3 left = qa_vec_scale(qa_vec_scale(left_direction,
                                   context->mirror ? -radius : radius), axis_scale);
        qa_vec3 up = qa_vec_scale(qa_vec_scale(up_direction, radius), axis_scale);
        /* RB_AddQuadStamp retains the global view normal in entity-local geometry. */
        size_t destination = start;
        if (geometry->retained) {
            if (geometry->retained->vertex_count + 4 >= QA_SOURCE_TESS_VERTICES ||
                geometry->retained->index_count + 6 >= QA_SOURCE_TESS_INDEXES)
                if (!material_source_deform_overflow(geometry->retained, context, frame, error)) return false;
            destination = geometry->retained->vertex_count;
        }
        stamp_quad(geometry, destination, center, left, up, normal, first.color, 0, 0, 1);
        if (geometry->retained) {
            geometry->retained->vertex_count += 4; geometry->retained->index_count += 6;
        }
    }
    if (geometry->retained) {
        geometry->mesh.vertex_count = geometry->retained->vertex_count;
        geometry->mesh.index_count = geometry->retained->index_count;
    }
    return true;
}

static bool autosprite2(deform_mesh *geometry, const qa_material_context *context,
                        qa_error *error)
{
    static const unsigned edges[6][2] = {{0, 1}, {0, 2}, {0, 3}, {1, 2}, {1, 3}, {2, 3}};
    if (!geometry->retained && !require_quads(geometry, "autosprite2", error)) return false;
    qa_vec3 forward = local_direction(context, context->view.axis[0]);
    for (size_t start = 0; start < geometry->mesh.vertex_count; start += 4) {
        if (geometry->retained && (start > QA_SOURCE_TESS_VERTICES - 4 || start / 4u * 6u + 5 >= QA_SOURCE_TESS_INDEXES)) {
            qa_error_set(error, QA_ERROR_FORMAT, start, "Source autosprite2 read exceeds its physical cells"); return false;
        }
        unsigned selected[2] = {0, 0};
        float lengths[2] = {999999.0f, 999999.0f};
        for (unsigned edge = 0; edge < 6; ++edge) {
            qa_vec3 delta = qa_vec_sub(geometry->vertices[start + edges[edge][0]].position,
                                        geometry->vertices[start + edges[edge][1]].position);
            float length = qa_vec_dot(delta, delta);
            if (length < lengths[0]) {
                selected[1] = selected[0];
                lengths[1] = lengths[0];
                selected[0] = edge;
                lengths[0] = length;
            } else if (length < lengths[1]) {
                selected[1] = edge;
                lengths[1] = length;
            }
        }
        qa_vec3 midpoints[2];
        for (unsigned i = 0; i < 2; ++i) {
            unsigned a = edges[selected[i]][0], b = edges[selected[i]][1];
            midpoints[i] = qa_vec_scale(qa_vec_add(geometry->vertices[start + a].position,
                                                    geometry->vertices[start + b].position), 0.5f);
        }
        qa_vec3 minor = normalize(qa_vec_cross(qa_vec_sub(midpoints[1], midpoints[0]), forward));
        size_t index_start = start / 4u * 6u;
        for (unsigned i = 0; i < 2; ++i) {
            unsigned a = edges[selected[i]][0], b = edges[selected[i]][1];
            bool follows = false;
            for (size_t k = 0; k < 5; ++k) {
                if (geometry->indices[index_start + k] == start + a &&
                    geometry->indices[index_start + k + 1] == start + b) follows = true;
            }
            double radius = sqrt((double)lengths[i]) * 0.5 * (follows ? -1.0 : 1.0);
            geometry->vertices[start + a].position = qa_vec_add(midpoints[i], scale_double(minor, radius));
            geometry->vertices[start + b].position = qa_vec_add(midpoints[i], scale_double(minor, -radius));
        }
    }
    return true;
}

static bool text_geometry(deform_mesh *geometry, uint32_t text_index,
                          const qa_material_context *context, qa_scene_frame *frame,
                          qa_error *error)
{
    if (context->texts == NULL || text_index >= context->text_count ||
        context->texts[text_index] == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, text_index,
                     "Text deformation has no retained source text row");
        return false;
    }
    if (geometry->mesh.vertex_count < 4 && !geometry->retained) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Text deformation requires a four-vertex source quad");
        return false;
    }
    const unsigned char *text = (const unsigned char *)context->texts[text_index];
    size_t length = strlen(context->texts[text_index]), glyphs = 0;
    for (size_t i = 0; i < length; ++i) if (text[i] != 32) ++glyphs;
    if (glyphs > UINT32_MAX / 4u || glyphs > SIZE_MAX / 6u) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Text deformation has too many glyphs");
        return false;
    }
    qa_vec3 width = qa_vec_cross(geometry->vertices[0].normal, qa_v3(0, 0, -1));
    qa_vec3 midpoint = qa_v3(0, 0, 0);
    float bottom = 999999.0f, top = -999999.0f;
    for (size_t i = 0; i < 4; ++i) {
        qa_vec3 position = geometry->vertices[i].position;
        midpoint = qa_vec_add(position, midpoint);
        if (position.z < bottom) bottom = position.z;
        if (position.z > top) top = position.z;
    }
    qa_vec3 height = qa_v3(0, 0, (top - bottom) * 0.5f);
    width = qa_vec_scale(width, height.z * -0.75f);
    qa_vec3 origin = qa_vec_add(qa_vec_scale(midpoint, 0.25f),
                                scale_double(width, (double)length - 1.0));
    qa_vec3 normal = qa_vec_sub(qa_v3(0, 0, 0), context->view.axis[0]);
    if (!allocate_geometry(frame, glyphs * 4u, glyphs * 6u, geometry, error)) return false;
    geometry->mesh.primitive = QA_SCENE_TRIANGLES;
    size_t start = 0;
    for (size_t i = 0; i < length; ++i) {
        unsigned ch = text[i];
        if (ch != 32) {
            float s = (float)(ch & 15u) * 0.0625f;
            float t = (float)(ch >> 4u) * 0.0625f;
            stamp_quad(geometry, start, origin, width, height, normal,
                       (qa_vec4){1, 1, 1, 1}, s, t, 0.0625f);
            start += 4;
        }
        origin = qa_vec_add(origin, qa_vec_scale(width, -2.0f));
    }
    return true;
}

static bool projection_shadow(deform_mesh *geometry, const qa_material_context *context,
                               qa_error *error)
{
    if (!context->source_scratch && !context->projection_shadow) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0,
                     "projectionshadow requires retained entity orientation and lighting");
        return false;
    }
    qa_vec3 ground = qa_v3(context->model.m[2], context->model.m[6], context->model.m[10]);
    float ground_distance = context->model.m[14] - context->shadow_plane;
    qa_vec3 light_direction = context->light_direction;
    float distance = qa_vec_dot(light_direction, ground);
    if (distance < 0.5f) {
        double adjustment = 0.5 - (double)distance;
        light_direction = qa_v3(
            (float)((double)light_direction.x + (double)ground.x * adjustment),
            (float)((double)light_direction.y + (double)ground.y * adjustment),
            (float)((double)light_direction.z + (double)ground.z * adjustment));
        distance = qa_vec_dot(light_direction, ground);
    }
    qa_vec3 light = qa_vec_scale(light_direction, 1.0f / distance);
    for (size_t i = 0; i < geometry->mesh.vertex_count; ++i) {
        qa_vec3 position = geometry->vertices[i].position;
        float height = qa_vec_dot(position, ground) + ground_distance;
        geometry->vertices[i].position = qa_vec_sub(position, qa_vec_scale(light, height));
    }
    return true;
}

static bool table_wave(const qa_material *material, const qa_material_wave *wave,
                       qa_error *error)
{
    switch (wave->kind) {
    case QA_WAVE_SIN:
    case QA_WAVE_SQUARE:
    case QA_WAVE_TRIANGLE:
    case QA_WAVE_SAWTOOTH:
    case QA_WAVE_INVERSE_SAWTOOTH:
        return true;
    default:
        qa_error_set(error, QA_ERROR_FORMAT, 0,
                     "Material '%s' has a non-table move or wave deformation",
                     material->name != NULL ? material->name : "<unnamed>");
        return false;
    }
}

static void wave_deform(deform_mesh *geometry, const qa_material_deform *deform, float time)
{
    float constant_scale = deform->wave.frequency == 0.0f ?
        qa_material_wave_evaluate(&deform->wave, time) : 0.0f;
    for (size_t i = 0; i < geometry->mesh.vertex_count; ++i) {
        qa_scene_vertex *vertex = &geometry->vertices[i];
        float amount = constant_scale;
        if (deform->wave.frequency != 0.0f) {
            float sum = vertex->position.x + vertex->position.y;
            sum += vertex->position.z;
            float offset = sum * deform->spread;
            qa_material_wave wave = deform->wave;
            wave.phase += offset;
            amount = qa_material_wave_evaluate(&wave, time);
        }
        vertex->position = qa_vec_add(vertex->position, qa_vec_scale(vertex->normal, amount));
    }
}

static void normal_deform(deform_mesh *geometry, const qa_material_deform *deform, float time)
{
    float sample_time = time * deform->wave.frequency;
    for (size_t i = 0; i < geometry->mesh.vertex_count; ++i) {
        qa_scene_vertex *vertex = &geometry->vertices[i];
        qa_vec3 point = qa_vec_scale(vertex->position, 0.98f);
        qa_vec3 offset = qa_v3(
            deform->wave.amplitude * qa_material_noise(point.x, point.y, point.z, sample_time),
            deform->wave.amplitude * qa_material_noise(100.0f + point.x, point.y, point.z, sample_time),
            deform->wave.amplitude * qa_material_noise(200.0f + point.x, point.y, point.z, sample_time));
        vertex->normal = qa_material_fast_normalize(qa_vec_add(vertex->normal, offset));
    }
}

static bool bulge_deform(deform_mesh *geometry, const qa_material_deform *deform,
                         const qa_material_context *context, qa_error *error)
{
    const float table_scale = (float)(1024.0 / (3.14159265358979323846 * 2.0));
    float now = (float)context->milliseconds;
    now *= deform->speed;
    now *= 0.001f;
    for (size_t i = 0; i < geometry->mesh.vertex_count; ++i) {
        qa_scene_vertex *vertex = &geometry->vertices[i];
        float phase = vertex->texcoord.x * deform->width;
        phase += now;
        float raw_index = table_scale * phase;
        if (!isfinite(raw_index) || (double)raw_index < -2147483648.0 ||
            (double)raw_index >= 2147483648.0) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Material bulge index is outside source integer range");
            return false;
        }
        unsigned index = qa_material_table_index(raw_index);
        float amount = qa_material_sine(index) * deform->height;
        vertex->position = qa_vec_add(vertex->position, qa_vec_scale(vertex->normal, amount));
    }
    return true;
}

static void update_bounds(deform_mesh *geometry)
{
    if (geometry->mesh.vertex_count == 0) {
        geometry->mesh.bounds = (qa_bounds){qa_v3(0, 0, 0), qa_v3(0, 0, 0)};
        return;
    }
    qa_vec3 low = geometry->vertices[0].position, high = low;
    for (size_t i = 1; i < geometry->mesh.vertex_count; ++i) {
        qa_vec3 position = geometry->vertices[i].position;
        if (position.x < low.x) low.x = position.x;
        if (position.y < low.y) low.y = position.y;
        if (position.z < low.z) low.z = position.z;
        if (position.x > high.x) high.x = position.x;
        if (position.y > high.y) high.y = position.y;
        if (position.z > high.z) high.z = position.z;
    }
    geometry->mesh.bounds = (qa_bounds){low, high};
}

bool qa_material_deform_mesh(const qa_material *material, const qa_scene_mesh *source,
                             const qa_material_context *context, float time,
                             qa_scene_frame *frame, qa_scene_mesh *out, qa_error *error)
{
    if (material == NULL || source == NULL || context == NULL || frame == NULL || out == NULL ||
        (material->deform_count != 0 && material->deforms == NULL) ||
        (source->vertex_count != 0 && source->vertices == NULL) ||
        (source->index_count != 0 && source->indices == NULL)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid material deformation input");
        return false;
    }
    bool has_deformation = false;
    for (size_t i = 0; i < material->deform_count; ++i)
        if (material->deforms[i].kind != QA_DEFORM_NONE) has_deformation = true;
    if (!has_deformation) {
        *out = *source;
        return true;
    }
    deform_mesh geometry = {.mesh = *source};
    if (context->source_scratch && context->source_scratch->submitting &&
        source->vertices == context->source_scratch->vertices && source->indices == context->source_scratch->indices) {
        geometry.retained = context->source_scratch;
        geometry.vertices = geometry.retained->vertices; geometry.indices = geometry.retained->indices;
    } else {
        if (!allocate_geometry(frame, source->vertex_count, source->index_count, &geometry, error)) return false;
        if (source->vertex_count != 0)
            memcpy(geometry.vertices, source->vertices, source->vertex_count * sizeof(*geometry.vertices));
        if (source->index_count != 0)
            memcpy(geometry.indices, source->indices, source->index_count * sizeof(*geometry.indices));
    }
    for (size_t i = 0; i < material->deform_count; ++i) {
        const qa_material_deform *deform = &material->deforms[i];
        switch (deform->kind) {
        case QA_DEFORM_NONE:
            break;
        case QA_DEFORM_WAVE:
            if (!table_wave(material, &deform->wave, error)) return false;
            wave_deform(&geometry, deform, time);
            break;
        case QA_DEFORM_MOVE: {
            if (!table_wave(material, &deform->wave, error)) return false;
            qa_vec3 offset = qa_vec_scale(deform->vector, qa_material_wave_evaluate(&deform->wave, time));
            for (size_t j = 0; j < geometry.mesh.vertex_count; ++j)
                geometry.vertices[j].position = qa_vec_add(geometry.vertices[j].position, offset);
            break;
        }
        case QA_DEFORM_NORMAL:
            normal_deform(&geometry, deform, time);
            break;
        case QA_DEFORM_BULGE:
            if (!bulge_deform(&geometry, deform, context, error)) return false;
            break;
        case QA_DEFORM_AUTOSPRITE:
            if (!autosprite(&geometry, context, frame, error)) return false;
            break;
        case QA_DEFORM_AUTOSPRITE2:
            if (!autosprite2(&geometry, context, error)) return false;
            break;
        case QA_DEFORM_PROJECTION_SHADOW:
            if (!projection_shadow(&geometry, context, error)) return false;
            break;
        case QA_DEFORM_TEXT:
            if (!text_geometry(&geometry, deform->text_index, context, frame, error)) return false;
            break;
        default:
            qa_error_set(error, QA_ERROR_FORMAT, i, "Unknown material deformation");
            return false;
        }
    }
    update_bounds(&geometry);
    *out = geometry.mesh;
    return true;
}

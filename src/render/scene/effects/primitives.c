#include "internal.h"
#include "qa/scene_effects.h"

#include <limits.h>
#include <string.h>

static void rail_vertex(qa_scene_vertex *vertex, qa_vec3 position, float s, float t,
                         qa_scene_vec4 color, bool dim)
{
    vertex->position = position;
    vertex->texcoord = (qa_vec2){s, t};
    vertex->color.x = dim ? truncf(color.x * 255 * 0.25f) / 255 : color.x;
    vertex->color.y = dim ? truncf(color.y * 255 * 0.25f) / 255 : color.y;
    vertex->color.z = dim ? truncf(color.z * 255 * 0.25f) / 255 : color.z;
}

static bool rail_geometry(qa_scene_frame *frame, const qa_scene_view *view,
                            qa_scene_rail_kind kind, qa_vec3 origin, qa_vec3 old_origin,
                            qa_scene_vec4 color, const qa_scene_rail_options *options,
                            qa_scene_mesh *out, bool source, qa_error *error)
{
    const qa_scene_rail_options defaults = {.core_width = 6, .ring_width = 16, .segment_length = 32};
    if (!options) options = &defaults;
    if (!frame || !view || !out || !qa_vec_finite(origin) || !qa_vec_finite(old_origin) ||
        kind < QA_RAIL_CORE || kind > QA_RAIL_LIGHTNING ||
        ((!source || kind == QA_RAIL_RINGS) && (!isfinite(options->segment_length) ||
            (source ? options->segment_length == 0 : options->segment_length <= 0))) ||
        (!options->retained_vertices && options->retained_count)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid rail parameters");
        return false;
    }
    qa_vec3 start = kind == QA_RAIL_LIGHTNING ? origin : old_origin;
    qa_vec3 end = kind == QA_RAIL_LIGHTNING ? old_origin : origin;
    qa_vec3 delta = qa_vec_sub(end, start), direction = qa_vec_normalize(delta);
    double length_value = trunc(qa_vec_length(delta));
    if (length_value > INT32_MAX || !isfinite(length_value)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Rail length exceeds source int32 range");
        return false;
    }
    int32_t length = (int32_t)length_value;
    size_t segments = kind == QA_RAIL_LIGHTNING ? 4 : 1;
    if (kind == QA_RAIL_RINGS) {
        double count = trunc((float)length / options->segment_length);
        if (count > 1000000 || !isfinite(count)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Rail ring count exceeds source geometry bound");
            return false;
        }
        segments = count > 1 ? (size_t)count - 1 : 1;
    }
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, segments * 4, segments * 6, &mesh, &vertices, &indices, error)) return false;
    size_t retained = options->retained_count < mesh.vertex_count ? options->retained_count : mesh.vertex_count;
    if (retained) memcpy(vertices, options->retained_vertices, retained * sizeof(*vertices));
    if (kind != QA_RAIL_RINGS) {
        qa_vec3 right = qa_vec_normalize(qa_vec_cross(qa_vec_normalize(qa_vec_sub(start, view->origin)),
                                                     qa_vec_normalize(qa_vec_sub(end, view->origin))));
        float width = kind == QA_RAIL_LIGHTNING ? 8 : (float)options->core_width;
        float t = (float)length / 256;
        for (size_t i = 0; i < segments; ++i) {
            qa_vec3 offset = qa_vec_scale(right, width);
            qa_scene_vertex *v = vertices + i * 4;
            rail_vertex(&v[0], qa_vec_add(start, offset), 0, 0, color, true);
            rail_vertex(&v[1], qa_vec_sub(start, offset), 0, 1, color, false);
            rail_vertex(&v[2], qa_vec_add(end, offset), t, 0, color, false);
            rail_vertex(&v[3], qa_vec_sub(end, offset), t, 1, color, false);
            uint32_t base = (uint32_t)i * 4;
            const uint32_t quad[6] = {base, base + 1, base + 2, base + 2, base + 1, base + 3};
            memcpy(indices + i * 6, quad, sizeof(quad));
            if (kind == QA_RAIL_LIGHTNING) right = qa_effect_rotate(right, direction, 45);
        }
    } else {
        qa_vec3 seed = qa_v3(direction.z, -direction.x, direction.y);
        qa_vec3 right = qa_vec_normalize(qa_vec_sub(seed, qa_vec_scale(direction, qa_vec_dot(seed, direction))));
        qa_vec3 up = qa_vec_cross(right, direction);
        qa_vec3 step = qa_vec_scale(direction, options->segment_length), positions[4];
        for (unsigned i = 0; i < 4; ++i) {
            double angle = (45 + i * 90) * QA_EFFECT_PI / 180;
            qa_vec3 offset = qa_vec_add(qa_vec_scale(right, (float)cos(angle)), qa_vec_scale(up, (float)sin(angle)));
            offset = qa_vec_scale(qa_vec_scale(offset, 0.25f), (float)options->ring_width);
            positions[i] = qa_vec_add(start, offset);
            if (segments > 1) positions[i] = qa_vec_add(positions[i], step);
        }
        for (size_t i = 0; i < segments; ++i) {
            uint32_t base = (uint32_t)i * 4;
            for (unsigned j = 0; j < 4; ++j) {
                rail_vertex(&vertices[base + j], positions[j], j < 2 ? 1 : 0,
                             j != 0 && j != 3 ? 1 : 0, color, false);
                positions[j] = qa_vec_add(positions[j], step);
            }
            const uint32_t quad[6] = {base, base + 1, base + 3, base + 3, base + 1, base + 2};
            memcpy(indices + i * 6, quad, sizeof(quad));
        }
    }
    qa_effect_bounds(&mesh);
    *out = mesh;
    return true;
}

bool qa_scene_rail_geometry(qa_scene_frame *frame, const qa_scene_view *view,
    qa_scene_rail_kind kind, qa_vec3 origin, qa_vec3 old_origin, qa_scene_vec4 color,
    const qa_scene_rail_options *options, qa_scene_mesh *out, qa_error *error)
{ return rail_geometry(frame, view, kind, origin, old_origin, color, options, out, false, error); }
bool qa_scene_source_rail_geometry(qa_scene_frame *frame, const qa_scene_view *view,
    qa_scene_rail_kind kind, qa_vec3 origin, qa_vec3 old_origin, qa_scene_vec4 color,
    const qa_scene_rail_options *options, qa_scene_mesh *out, qa_error *error)
{ return rail_geometry(frame, view, kind, origin, old_origin, color, options, out, true, error); }

bool qa_scene_flare(qa_scene_frame *frame, const qa_scene_view *view, qa_vec3 origin,
                    const qa_scene_flare_options *options, const qa_scene_image *image,
                    qa_error *error)
{
    if (!frame || !view || !options || !qa_vec_finite(origin) || !isfinite(options->scale) ||
        !isfinite(options->fade_start) || !isfinite(options->fade_end)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid flare parameters");
        return false;
    }
    qa_vec3 delta = qa_vec_sub(origin, view->origin);
    float distance = qa_vec_length(delta);
    if (distance < options->fade_start) return true;
    float fraction = distance >= options->fade_end ? 1 :
        (distance - options->fade_start) / (options->fade_end - options->fade_start);
    float size = (options->standard_image ? 50 : 25) * options->scale;
    qa_vec3 direction = qa_vec_normalize(delta);
    qa_vec3 rotated = qa_v3(direction.z, -direction.x, direction.y);
    qa_vec3 right = options->lock_angle ? qa_vec_scale(view->axis[1], -1) :
        qa_vec_normalize(qa_vec_sub(rotated, qa_vec_scale(direction, qa_vec_dot(rotated, direction))));
    qa_vec3 up = options->lock_angle ? view->axis[2] : qa_vec_cross(right, direction);
    float alpha = truncf((options->standard_image ? 160 : 128) * fraction) / 255;
    qa_vec3 rim = options->separate_rim ? options->rim_color : options->color;
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, 5, 12, &mesh, &vertices, &indices, error)) return false;
    const float horizontal[5] = {0, -1, -1, 1, 1}, vertical[5] = {0, -1, 1, 1, -1};
    const qa_vec2 uv[5] = {{0.5f, 0.5f}, {0, 1}, {0, 0}, {1, 0}, {1, 1}};
    for (unsigned i = 0; i < 5; ++i) {
        vertices[i].position = qa_vec_add(origin, qa_vec_add(qa_vec_scale(right, horizontal[i] * size), qa_vec_scale(up, vertical[i] * size)));
        vertices[i].normal = qa_vec_scale(direction, -1);
        vertices[i].texcoord = uv[i];
        qa_vec3 rgb = i == 0 ? options->color : rim;
        vertices[i].color = (qa_scene_vec4){rgb.x, rgb.y, rgb.z, alpha};
    }
    const uint32_t order[12] = {0, 2, 3, 0, 3, 4, 0, 4, 1, 0, 1, 2};
    memcpy(indices, order, sizeof(order));
    qa_effect_bounds(&mesh);
    qa_scene_draw draw;
    qa_effect_draw(&draw, view, &mesh, image, true);
    draw.luminance_alpha = options->standard_image;
    return qa_scene_frame_draw(frame, &draw, error);
}

bool qa_scene_q3_beam_draw(qa_scene_frame *frame, const qa_scene_view *view, qa_vec3 origin,
                      qa_vec3 old_origin, const qa_scene_image *image,
                      const qa_scene_state *previous_state, qa_scene_draw *out, bool *present, qa_error *error)
{
    if (!frame || !view || !previous_state || !out || !present || !qa_vec_finite(origin) || !qa_vec_finite(old_origin)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid Q3 beam parameters");
        return false;
    }
    *present = false;
    qa_vec3 delta = qa_vec_sub(old_origin, origin);
    if (qa_vec_length(delta) == 0) return true;
    qa_vec3 direction = qa_vec_normalize(delta), perpendicular = qa_vec_scale(qa_effect_perpendicular(direction), 4);
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, 12, 36, &mesh, &vertices, &indices, error)) return false;
    for (unsigned i = 0; i < 6; ++i) {
        /* Q3's source intentionally does not add the entity origin here. */
        qa_vec3 start = qa_effect_rotate(perpendicular, direction, (float)i * 60);
        vertices[i * 2].position = start;
        vertices[i * 2 + 1].position = qa_vec_add(start, delta);
        vertices[i * 2].color = vertices[i * 2 + 1].color = (qa_scene_vec4){1, 0, 0, 1};
    }
    for (uint32_t i = 0; i < 12; ++i) {
        indices[i * 3] = (i + i % 2) % 12;
        indices[i * 3 + 1] = (i + 1 - i % 2) % 12;
        indices[i * 3 + 2] = (i + 2) % 12;
    }
    qa_effect_bounds(&mesh);
    qa_scene_draw draw;
    qa_effect_draw(&draw, view, &mesh, image, true);
    draw.state = *previous_state;
    draw.state.blend_source = QA_BLEND_ONE;
    draw.state.blend_destination = QA_BLEND_ONE;
    draw.state.depth_test = QA_DEPTH_LEQUAL;
    draw.state.depth_write = false;
    draw.state.alpha_test = QA_ALPHA_NONE;
    *out = draw; *present = true; return true;
}
bool qa_scene_q3_beam(qa_scene_frame *frame, const qa_scene_view *view, qa_vec3 origin,
    qa_vec3 old_origin, const qa_scene_image *image, const qa_scene_state *state, qa_error *error)
{
    qa_scene_draw draw; bool present;
    return qa_scene_q3_beam_draw(frame, view, origin, old_origin, image, state, &draw, &present, error) &&
        (!present || qa_scene_frame_draw(frame, &draw, error));
}

bool qa_scene_poly_geometry(qa_scene_frame *frame, const qa_scene_vertex *source,
                            size_t count, qa_scene_mesh *out, qa_error *error)
{
    if (!frame || !out || (!source && count) || (count > 2 && count - 2 > SIZE_MAX / 3)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid polygon geometry");
        return false;
    }
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, count, count > 2 ? (count - 2) * 3 : 0, &mesh, &vertices, &indices, error)) return false;
    for (size_t i = 0; i < count; ++i) {
        vertices[i].position = source[i].position;
        vertices[i].texcoord = source[i].texcoord;
        vertices[i].color = source[i].color;
    }
    for (size_t i = 0; i + 2 < count; ++i) {
        indices[i * 3] = 0;
        indices[i * 3 + 1] = (uint32_t)i + 1;
        indices[i * 3 + 2] = (uint32_t)i + 2;
    }
    qa_effect_bounds(&mesh);
    *out = mesh;
    return true;
}

bool qa_scene_default_model_draw(qa_scene_frame *frame, const qa_scene_view *view,
                            qa_scene_matrix model, const qa_scene_image *white,
                            const qa_scene_state *state, qa_scene_draw *out, qa_error *error)
{
    if (!frame || !view || !state || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Default model requires a view and draw state");
        return false;
    }
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, 6, 6, &mesh, &vertices, &indices, error)) return false;
    for (uint32_t i = 0; i < 3; ++i) {
        vertices[i * 2 + 1].position = qa_v3(i == 0 ? 16 : 0, i == 1 ? 16 : 0, i == 2 ? 16 : 0);
        vertices[i * 2].color = vertices[i * 2 + 1].color =
            (qa_scene_vec4){i == 0 ? 1 : 0, i == 1 ? 1 : 0, i == 2 ? 1 : 0, 1};
        indices[i * 2] = i * 2;
        indices[i * 2 + 1] = i * 2 + 1;
    }
    mesh.primitive = QA_SCENE_LINES;
    qa_effect_bounds(&mesh);
    qa_scene_draw draw;
    qa_effect_draw(&draw, view, &mesh, white, false);
    draw.model = model;
    draw.mvp = qa_scene_matrix_multiply(draw.mvp, model);
    draw.state = *state;
    draw.state.line_width = 3;
    *out = draw; return true;
}
bool qa_scene_default_model(qa_scene_frame *frame, const qa_scene_view *view, qa_scene_matrix model,
    const qa_scene_image *white, const qa_scene_state *state, qa_error *error)
{
    qa_scene_draw draw;
    return qa_scene_default_model_draw(frame, view, model, white, state, &draw, error) &&
        qa_scene_frame_draw(frame, &draw, error);
}

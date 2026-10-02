#include "internal.h"
#include "qa/scene_effects.h"

#include <string.h>

typedef struct shadow_edge { uint32_t end; bool facing; } shadow_edge;

static bool silhouette(const shadow_edge *edges, const size_t *offsets,
                        uint32_t start, const shadow_edge *edge)
{
    if (!edge->facing) return false;
    for (size_t i = offsets[edge->end]; i < offsets[(size_t)edge->end + 1]; ++i)
        if (edges[i].end == start && edges[i].facing) return false;
    return true;
}

static bool stencil_shadow(qa_scene_frame *frame, const qa_scene_view *view,
                             const qa_scene_mesh *source, qa_scene_matrix model,
                             qa_vec3 local_light, const qa_scene_image *white, bool source_edges, qa_error *error)
{
    if (!frame || !view || !source || !qa_vec_finite(local_light) || source->primitive != QA_SCENE_TRIANGLES ||
        source->index_count % 3 || (!source->vertices && source->vertex_count) ||
        (!source->indices && source->index_count)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Stencil shadows require a finite light and triangle mesh");
        return false;
    }
    if (!source->index_count || !source->vertex_count) return true;
    if (source->vertex_count > UINT32_MAX / 2 || source->vertex_count + 1 > SIZE_MAX / sizeof(size_t) ||
        source->index_count > SIZE_MAX / sizeof(shadow_edge)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Stencil shadow geometry size overflow");
        return false;
    }
    size_t *offsets = qa_arena_alloc(&frame->storage, (source->vertex_count + 1) * sizeof(*offsets), _Alignof(size_t), error);
    size_t *cursors = qa_arena_alloc(&frame->storage, source->vertex_count * sizeof(*cursors), _Alignof(size_t), error);
    shadow_edge *edges = qa_arena_alloc(&frame->storage, source->index_count * sizeof(*edges), _Alignof(shadow_edge), error);
    if (!offsets || !cursors || !edges) return false;
    memset(offsets, 0, (source->vertex_count + 1) * sizeof(*offsets));
    for (size_t i = 0; i < source->index_count; ++i) {
        uint32_t index = source->indices[i];
        if (index >= source->vertex_count) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Stencil shadow index outside mesh");
            return false;
        }
        if (!source_edges || offsets[(size_t)index + 1] < 32) ++offsets[(size_t)index + 1];
    }
    for (size_t i = 1; i <= source->vertex_count; ++i) offsets[i] += offsets[i - 1];
    memcpy(cursors, offsets, source->vertex_count * sizeof(*cursors));
    for (size_t i = 0; i < source->index_count; i += 3) {
        uint32_t a = source->indices[i], b = source->indices[i + 1], c = source->indices[i + 2];
        qa_vec3 first = qa_vec_sub(source->vertices[b].position, source->vertices[a].position);
        qa_vec3 second = qa_vec_sub(source->vertices[c].position, source->vertices[a].position);
        bool facing = qa_vec_dot(qa_vec_cross(first, second), local_light) > 0;
        if (cursors[a] < offsets[(size_t)a + 1]) edges[cursors[a]++] = (shadow_edge){b, facing};
        if (cursors[b] < offsets[(size_t)b + 1]) edges[cursors[b]++] = (shadow_edge){c, facing};
        if (cursors[c] < offsets[(size_t)c + 1]) edges[cursors[c]++] = (shadow_edge){a, facing};
    }
    size_t edge_count = 0;
    for (size_t start = 0; start < source->vertex_count; ++start)
        for (size_t i = offsets[start]; i < offsets[start + 1]; ++i)
            if (silhouette(edges, offsets, (uint32_t)start, &edges[i])) ++edge_count;
    if (!edge_count) return true;
    if (edge_count > SIZE_MAX / 6) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Stencil shadow index count overflow");
        return false;
    }
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, source->vertex_count * 2, edge_count * 6, &mesh, &vertices, &indices, error)) return false;
    uint32_t extension = (uint32_t)source->vertex_count;
    qa_vec3 extrusion = qa_vec_scale(local_light, -512);
    for (size_t i = 0; i < source->vertex_count; ++i) {
        vertices[i].position = source->vertices[i].position;
        vertices[i + source->vertex_count].position = qa_vec_add(source->vertices[i].position, extrusion);
        vertices[i].color = vertices[i + source->vertex_count].color = (qa_scene_vec4){0.2f, 0.2f, 0.2f, 1};
    }
    size_t cursor = 0;
    for (size_t start = 0; start < source->vertex_count; ++start)
        for (size_t i = offsets[start]; i < offsets[start + 1]; ++i) {
            if (!silhouette(edges, offsets, (uint32_t)start, &edges[i])) continue;
            uint32_t a = (uint32_t)start, b = edges[i].end;
            const uint32_t quad[6] = {a, a + extension, b, b, a + extension, b + extension};
            memcpy(indices + cursor, quad, sizeof(quad));
            cursor += 6;
        }
    qa_effect_bounds(&mesh);
    qa_scene_draw draw;
    qa_effect_draw(&draw, view, &mesh, white, false);
    draw.source_retain_depth_range = source_edges;
    draw.model = model;
    draw.mvp = qa_scene_matrix_multiply(draw.mvp, model);
    draw.state.blend_source = QA_BLEND_ONE;
    draw.state.blend_destination = QA_BLEND_ZERO;
    draw.state.color_write = false;
    draw.state.stencil_enabled = true;
    draw.state.stencil_test = QA_STENCIL_ALWAYS;
    draw.state.stencil_reference = 1;
    draw.state.stencil_compare_mask = draw.state.stencil_write_mask = 255;
    draw.state.stencil_depth_pass = QA_STENCIL_INCREMENT;
    draw.state.cull = view->mirror ? QA_CULL_FRONT : QA_CULL_BACK;
    size_t count_before = frame->command_count, images_before = frame->image_count;
    if (qa_scene_frame_draw(frame, &draw, error)) {
        draw.state.stencil_depth_pass = QA_STENCIL_DECREMENT;
        draw.state.cull = view->mirror ? QA_CULL_BACK : QA_CULL_FRONT;
        if (qa_scene_frame_draw(frame, &draw, error)) return true;
    }
    if (!source_edges) {
        for (size_t i = images_before; i < frame->image_count; ++i) qa_scene_image_release(frame->images[i]);
        frame->image_count = images_before;
        frame->command_count = count_before;
    }
    return false;
}

bool qa_scene_stencil_shadow(qa_scene_frame *frame, const qa_scene_view *view,
    const qa_scene_mesh *mesh, qa_scene_matrix model, qa_vec3 light,
    const qa_scene_image *white, qa_error *error)
{
    return stencil_shadow(frame, view, mesh, model, light, white, false, error);
}
bool qa_scene_source_stencil_shadow(qa_scene_frame *frame, const qa_scene_view *view,
    const qa_scene_mesh *mesh, qa_scene_matrix model, qa_vec3 light,
    const qa_scene_image *white, qa_error *error)
{
    return stencil_shadow(frame, view, mesh, model, light, white, true, error);
}

static bool stencil_finish(qa_scene_frame *frame, const qa_scene_view *view,
                            const qa_scene_image *white, bool source, qa_error *error)
{
    if (!frame || !view) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Stencil finish requires a scene view");
        return false;
    }
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    if (!qa_effect_mesh(frame, 4, 6, &mesh, &vertices, &indices, error)) return false;
    const qa_vec3 points[4] = {{-100, 100, -10}, {100, 100, -10}, {100, -100, -10}, {-100, -100, -10}};
    for (size_t i = 0; i < 4; ++i) {
        vertices[i].position = points[i];
        vertices[i].color = (qa_scene_vec4){0.6f, 0.6f, 0.6f, 1};
    }
    const uint32_t order[6] = {0, 1, 2, 0, 2, 3};
    memcpy(indices, order, sizeof(order));
    qa_effect_bounds(&mesh);
    qa_scene_draw draw;
    qa_effect_draw(&draw, view, &mesh, white, false);
    draw.source_direct = source ? QA_SOURCE_DIRECT_SHADOW_FINISH : QA_SOURCE_DIRECT_NONE;
    draw.source_retain_depth_range = source;
    draw.mvp = view->projection;
    draw.state.blend_source = QA_BLEND_DST_COLOR;
    draw.state.blend_destination = QA_BLEND_ZERO;
    draw.state.depth_write = true;
    draw.state.stencil_enabled = true;
    draw.state.stencil_test = QA_STENCIL_NOTEQUAL;
    draw.state.stencil_compare_mask = draw.state.stencil_write_mask = 255;
    qa_scene_command command = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = *view};
    command.data.view.clip_enabled = false;
    command.data.view.clear_color = command.data.view.clear_depth = command.data.view.clear_stencil = false;
    size_t count_before = frame->command_count, images_before = frame->image_count;
    if (qa_scene_frame_emit(frame, &command, error) && qa_scene_frame_draw(frame, &draw, error)) return true;
    if (!source) {
        for (size_t i = images_before; i < frame->image_count; ++i) qa_scene_image_release(frame->images[i]);
        frame->image_count = images_before;
        frame->command_count = count_before;
    }
    return false;
}

bool qa_scene_stencil_finish(qa_scene_frame *frame, const qa_scene_view *view,
                             const qa_scene_image *white, qa_error *error)
{
    return stencil_finish(frame, view, white, false, error);
}
bool qa_scene_source_stencil_finish(qa_scene_frame *frame, const qa_scene_view *view,
                                    const qa_scene_image *white, qa_error *error)
{
    return stencil_finish(frame, view, white, true, error);
}

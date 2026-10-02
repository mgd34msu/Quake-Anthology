#include "qa/scene.h"
#include "qa/material_source_scratch.h"

#include <limits.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

static bool reserve(void **data, size_t *capacity, size_t needed, size_t stride, qa_error *error)
{
    if (needed <= *capacity) return true;
    if (stride == 0 || needed > (size_t)PTRDIFF_MAX / stride) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene array size overflow");
        return false;
    }
    size_t next = *capacity == 0 ? 64 : *capacity;
    while (next < needed) {
        if (next > (size_t)PTRDIFF_MAX / stride / 2) { next = needed; break; }
        next *= 2;
    }
    void *replacement = realloc(*data, next * stride);
    if (replacement == NULL) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "cannot grow scene frame storage");
        return false;
    }
    *data = replacement;
    *capacity = next;
    return true;
}

void qa_scene_frame_init(qa_scene_frame *frame, uint64_t owner)
{
    if (frame == NULL) return;
    *frame = (qa_scene_frame){.owner = owner};
    qa_arena_init(&frame->storage, 262144);
}

bool qa_scene_frame_material_order(qa_scene_frame *frame, qa_material_order *order, qa_error *error)
{
    if (!frame || frame->group_count) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "bind material order before preparing scene groups");
        return false;
    }
    frame->material_order = order; return true;
}

void qa_scene_frame_reset(qa_scene_frame *frame, uint64_t sequence)
{
    if (frame == NULL) return;
    if (frame->source_pending) {
        qa_error ignored = {0};
        (void)qa_material_source_frame_end(frame->source_pending, frame, false, &ignored);
    }
    for (size_t i = 0; i < frame->image_count; ++i) qa_scene_image_release(frame->images[i]);
    frame->image_count = 0;
    for (size_t i = 0; i < frame->geometry_count; ++i)
        qa_scene_geometry_release(frame->geometries[i]);
    frame->geometry_count = 0;
    frame->command_count = 0;
    frame->group_count = 0;
    frame->sequence = sequence;
    frame->source_backend = false;
    frame->source_skip_backend = false;
    frame->source_clear_draw_buffer = false;
    qa_arena_reset(&frame->storage);
}

void qa_scene_frame_destroy(qa_scene_frame *frame)
{
    if (frame == NULL) return;
    qa_scene_frame_reset(frame, 0);
    qa_arena_destroy(&frame->storage);
    free(frame->commands);
    free(frame->images);
    free(frame->geometries);
    free(frame->groups);
    free(frame->sort_groups);
    free(frame->sort_commands);
    *frame = (qa_scene_frame){0};
}

static bool pin(qa_scene_frame *frame, const qa_scene_image *image, qa_error *error)
{
    if (image == NULL) return true;
    for (size_t i = 0; i < frame->image_count; ++i)
        if (frame->images[i] == image) return true;
    if (frame->image_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene image reference count overflow");
        return false;
    }
    void *data = frame->images;
    if (!reserve(&data, &frame->image_capacity, frame->image_count + 1,
                 sizeof(*frame->images), error)) return false;
    frame->images = data;
    qa_scene_image_retain(image);
    frame->images[frame->image_count++] = image;
    return true;
}

bool qa_scene_frame_geometry(qa_scene_frame *frame, const qa_scene_geometry *geometry, qa_error *error)
{
    if (frame == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene geometry pin requires a frame");
        return false;
    }
    if (geometry == NULL || (frame->geometry_count != 0 &&
        frame->geometries[frame->geometry_count - 1] == geometry)) return true;
    if (frame->geometry_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene geometry reference count overflow");
        return false;
    }
    void *data = frame->geometries;
    if (!reserve(&data, &frame->geometry_capacity, frame->geometry_count + 1,
                 sizeof(*frame->geometries), error)) return false;
    frame->geometries = data;
    qa_scene_geometry_retain(geometry);
    frame->geometries[frame->geometry_count++] = geometry;
    return true;
}

bool qa_scene_frame_emit(qa_scene_frame *frame, const qa_scene_command *command, qa_error *error)
{
    if (frame == NULL || command == NULL || command->kind < QA_SCENE_COMMAND_VIEW ||
        command->kind > QA_SCENE_COMMAND_OUTPUT_DOMAIN) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene command");
        return false;
    }
    /* The command may be borrowed from this frame; snapshot before growth. */
    qa_scene_command copied = *command;
    if (frame->source_pending && (copied.kind == QA_SCENE_COMMAND_VIEW || copied.kind == QA_SCENE_COMMAND_TARGET ||
        copied.kind == QA_SCENE_COMMAND_OPACITY_BEGIN || copied.kind == QA_SCENE_COMMAND_OUTPUT_DOMAIN) &&
        !qa_material_source_picture_end(frame->source_pending, frame, error)) return false;
    if (frame->command_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene command count overflow");
        return false;
    }
    void *data = frame->commands;
    if (!reserve(&data, &frame->command_capacity, frame->command_count + 1,
                 sizeof(*frame->commands), error)) return false;
    frame->commands = data;
    if (copied.kind == QA_SCENE_COMMAND_DRAW) {
        qa_scene_draw *draw = &copied.data.draw;
        if (draw->texture_count > 2 || (draw->mesh.identity != 0 && draw->mesh.geometry == NULL) ||
            (draw->mesh.vertex_count != 0 && draw->mesh.vertices == NULL) ||
            (draw->mesh.index_count != 0 && draw->mesh.indices == NULL) ||
            (draw->light_count != 0 && draw->lights == NULL)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid scene draw storage");
            return false;
        }
        for (size_t i = 0; i < draw->texture_count; ++i)
            if (!pin(frame, draw->textures[i], error)) return false;
        if (!pin(frame, draw->shadow_atlas, error)) return false;
        if (draw->light_count != 0) {
            if (draw->light_count > (size_t)PTRDIFF_MAX / sizeof(*draw->lights)) {
                qa_error_set(error, QA_ERROR_MEMORY, 0, "scene light array overflow");
                return false;
            }
            size_t bytes = draw->light_count * sizeof(*draw->lights);
            qa_scene_shadow_light *lights = qa_arena_alloc(&frame->storage, bytes,
                _Alignof(qa_scene_shadow_light), error);
            if (lights == NULL) return false;
            memcpy(lights, draw->lights, bytes);
            draw->lights = lights;
        }
        /* Retain separately from commands: shadow extraction and failed surface
         * submissions rewind command_count while keeping frame arena data. */
        if (!qa_scene_frame_geometry(frame, draw->mesh.geometry, error)) return false;
    } else if (copied.kind == QA_SCENE_COMMAND_IMAGE) {
        if (copied.data.image == NULL) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene image update requires a version");
            return false;
        }
        if (!pin(frame, copied.data.image, error)) return false;
    } else if (copied.kind == QA_SCENE_COMMAND_TARGET) {
        if (copied.data.target.image != NULL && copied.data.target.image->kind != QA_SCENE_DEPTH32F) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene depth target requires depth pixels");
            return false;
        }
        if (!pin(frame, copied.data.target.image, error)) return false;
    }
    frame->commands[frame->command_count++] = copied;
    return !frame->source_pending || qa_material_source_issue_emitted(frame->source_pending, frame, error);
}
bool qa_scene_frame_output_domain(qa_scene_frame *frame, qa_scene_rect rect, bool source, qa_error *error)
{
    if (rect.x < 0 || rect.y < 0 || !rect.width || !rect.height) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid output color domain region"); return false;
    }
    qa_scene_command command = {.kind = QA_SCENE_COMMAND_OUTPUT_DOMAIN,
        .data.output_domain = {.rect = rect, .source = source}};
    return qa_scene_frame_emit(frame, &command, error);
}

bool qa_scene_frame_group(qa_scene_frame *frame, size_t first, qa_scene_group_kind kind,
                          const qa_material *material, float priority, uint32_t entity,
                          uint32_t fog, uint32_t dlight, qa_error *error)
{
    if (frame == NULL || first > frame->command_count || kind < QA_SCENE_GROUP_COMPILED ||
        kind > QA_SCENE_GROUP_SEQUENCE || !isfinite(priority) ||
        (kind == QA_SCENE_GROUP_SOURCE && (material == NULL || entity > 1022 || fog > 31 || dlight > 3)) ||
        (frame->group_count != 0 && first < frame->groups[frame->group_count-1].first +
            frame->groups[frame->group_count-1].count)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "invalid or overlapping scene group");
        return false;
    }
    if (frame->group_count == SIZE_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "scene group count overflow");
        return false;
    }
    void *data = frame->groups;
    if (!reserve(&data, &frame->group_capacity, frame->group_count + 1,
                 sizeof(*frame->groups), error)) return false;
    frame->groups = data;
    size_t ordinal = frame->group_count++;
    frame->groups[ordinal] = (qa_scene_group){.first = first,
        .count = frame->command_count-first, .ordinal = ordinal, .kind = kind,
        .material = material, .priority = priority, .entity = entity, .fog = fog, .dlight = dlight};
    return true;
}

bool qa_scene_frame_draw(qa_scene_frame *frame, const qa_scene_draw *draw, qa_error *error)
{
    if (draw == NULL) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "scene draw is absent");
        return false;
    }
    qa_scene_command command = {.kind = QA_SCENE_COMMAND_DRAW, .data.draw = *draw};
    return qa_scene_frame_emit(frame, &command, error);
}

bool qa_scene_frame_image(qa_scene_frame *frame, const qa_scene_image *image, qa_error *error)
{
    qa_scene_command command = {.kind = QA_SCENE_COMMAND_IMAGE, .data.image = image};
    return qa_scene_frame_emit(frame, &command, error);
}

void qa_scene_state_default(qa_scene_state *state)
{
    if (state == NULL) return;
    *state = (qa_scene_state){.blend_source = QA_BLEND_ONE, .blend_destination = QA_BLEND_ZERO,
        .depth_test = QA_DEPTH_LEQUAL, .alpha_test = QA_ALPHA_NONE, .cull = QA_CULL_NONE,
        .depth_write = true, .color_write = true, .depth_near = 0, .depth_far = 1,
        .line_width = 1, .stencil_compare_mask = UINT32_MAX, .stencil_write_mask = UINT32_MAX};
}

bool qa_scene_frame_picture(qa_scene_frame *frame, const qa_scene_image *image, qa_scene_rect target,
                           qa_scene_rect rect, qa_scene_vec4 uv, qa_scene_vec4 color, qa_error *error)
{
    qa_scene_rect_f destination = {(float)rect.x, (float)rect.y, (float)rect.width, (float)rect.height};
    return qa_scene_frame_picture_f(frame, image, target, destination, uv, color, error);
}

bool qa_scene_picture_geometry(qa_scene_frame *frame, qa_scene_rect target,
                               qa_scene_rect_f rect, qa_scene_vec4 uv, qa_scene_vec4 color,
                               qa_scene_mesh *out, qa_error *error)
{
    if (frame == NULL || out == NULL || target.width == 0 || target.height == 0 ||
        !isfinite(rect.x) || !isfinite(rect.y) || !isfinite(rect.width) || !isfinite(rect.height) ||
        !isfinite(rect.x + rect.width) || !isfinite(rect.y + rect.height)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "picture geometry requires a frame, output and viewport");
        return false;
    }
    float left = fmaxf(fminf(rect.x, rect.x + rect.width), (float)target.x);
    float top = fmaxf(fminf(rect.y, rect.y + rect.height), (float)target.y);
    float right = fminf(fmaxf(rect.x, rect.x + rect.width), (float)((int64_t)target.x + target.width));
    float bottom = fminf(fmaxf(rect.y, rect.y + rect.height), (float)((int64_t)target.y + target.height));
    if (right <= left || bottom <= top) { *out = (qa_scene_mesh){0}; return true; }
    float s0 = uv.x + (uv.z - uv.x) * (left - rect.x) / rect.width;
    float s1 = uv.x + (uv.z - uv.x) * (right - rect.x) / rect.width;
    float t0 = uv.y + (uv.w - uv.y) * (top - rect.y) / rect.height;
    float t1 = uv.y + (uv.w - uv.y) * (bottom - rect.y) / rect.height;
    qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage, 4 * sizeof(*vertices),
                                               _Alignof(qa_scene_vertex), error);
    uint32_t *indices = qa_arena_alloc(&frame->storage, 6 * sizeof(*indices),
                                       _Alignof(uint32_t), error);
    if (vertices == NULL || indices == NULL) return false;
    const uint32_t pattern[6] = {0, 1, 2, 0, 2, 3};
    memcpy(indices, pattern, sizeof(pattern));
    vertices[0] = (qa_scene_vertex){.position = {left, top, 0}, .texcoord = {s0, t0}, .color = color};
    vertices[1] = (qa_scene_vertex){.position = {right, top, 0}, .texcoord = {s1, t0}, .color = color};
    vertices[2] = (qa_scene_vertex){.position = {right, bottom, 0}, .texcoord = {s1, t1}, .color = color};
    vertices[3] = (qa_scene_vertex){.position = {left, bottom, 0}, .texcoord = {s0, t1}, .color = color};
    for (size_t i = 0; i < 4; ++i) vertices[i].normal.z = 1;
    *out = (qa_scene_mesh){.vertices = vertices, .indices = indices, .vertex_count = 4,
        .index_count = 6, .primitive = QA_SCENE_TRIANGLES,
        .bounds = {{left, top, 0}, {right, bottom, 0}}};
    return true;
}

bool qa_scene_frame_picture_f(qa_scene_frame *frame, const qa_scene_image *image, qa_scene_rect target,
                              qa_scene_rect_f rect, qa_scene_vec4 uv, qa_scene_vec4 color, qa_error *error)
{
    if (!image) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "picture requires an image"); return false;
    }
    qa_scene_mesh mesh;
    if (!qa_scene_picture_geometry(frame, target, rect, uv, color, &mesh, error)) return false;
    if (!mesh.vertex_count) return true;
    qa_scene_vertex *vertices = (qa_scene_vertex *)mesh.vertices;
    for (size_t i = 0; i < mesh.vertex_count; ++i) {
        vertices[i].normal = qa_v3(0, 0, 0);
        vertices[i].position.x = (vertices[i].position.x - (float)target.x) / (float)target.width * 2 - 1;
        vertices[i].position.y = 1 - (vertices[i].position.y - (float)target.y) / (float)target.height * 2;
    }
    mesh.bounds = (qa_bounds){{vertices[0].position.x, vertices[2].position.y, 0},
        {vertices[2].position.x, vertices[0].position.y, 0}};
    qa_scene_command view = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = {.viewport = target}};
    qa_scene_draw draw = {.mesh = mesh, .textures = {image, NULL}, .texture_count = 1};
    qa_scene_matrix_identity(&draw.model);
    qa_scene_matrix_identity(&draw.mvp);
    qa_scene_state_default(&draw.state);
    draw.state.blend_source = QA_BLEND_SRC_ALPHA;
    draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
    draw.state.depth_test = QA_DEPTH_ALWAYS;
    draw.state.depth_write = false;
    return qa_scene_frame_emit(frame, &view, error) && qa_scene_frame_draw(frame, &draw, error);
}

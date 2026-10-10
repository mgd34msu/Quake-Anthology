#include "particles.h"
#include "qa/text.h"
#include <limits.h>

bool gl_particles_prepare(qa_gl_renderer *renderer, const qa_scene_particle_batch *batch,
    qa_scene_draw *draw, qa_error *error)
{
    if (batch->count > (size_t)INT_MAX / 3 || batch->count > SIZE_MAX / (3 * sizeof(qa_scene_vertex))) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "GL particle batch exceeds native draw storage");
        return false;
    }
    size_t needed = batch->count * 3;
    if (needed > renderer->particle_capacity) {
        size_t capacity = renderer->particle_capacity ? renderer->particle_capacity : 768;
        while (capacity < needed) {
            if (capacity > (size_t)INT_MAX / 2) { capacity = needed; break; }
            capacity *= 2;
        }
        qa_scene_vertex *vertices = realloc(renderer->particle_vertices, capacity * sizeof(*vertices));
        if (!vertices) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Growing GL particle vertices"); return false; }
        renderer->particle_vertices = vertices;
        uint32_t *indices = realloc(renderer->particle_indices, capacity * sizeof(*indices));
        if (!indices) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Growing GL particle indices"); return false; }
        renderer->particle_indices = indices;
        for (size_t i = renderer->particle_capacity; i < capacity; ++i) indices[i] = (uint32_t)i;
        renderer->particle_capacity = capacity;
    }
    memset(draw, 0, sizeof(*draw));
    qa_scene_matrix_identity(&draw->model);
    draw->mvp = qa_scene_matrix_multiply(batch->view.projection, qa_scene_view_matrix(&batch->view));
    draw->textures[0] = batch->image;
    draw->texture_count = 1;
    draw->lighting = QA_LIGHT_VERTEX;
    qa_scene_state_default(&draw->state);
    draw->state.blend_source = QA_BLEND_SRC_ALPHA;
    draw->state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
    draw->state.depth_write = batch->family == QA_GAME_Q1;
    draw->state.alpha_test = batch->family == QA_GAME_Q1 ? QA_ALPHA_GT666 : QA_ALPHA_NONE;
    draw->state.cull = QA_CULL_NONE;
    draw->mesh.vertices = renderer->particle_vertices;
    draw->mesh.indices = renderer->particle_indices;
    draw->mesh.vertex_count = draw->mesh.index_count = needed;
    draw->mesh.primitive = QA_SCENE_TRIANGLES;
    qa_vec3 normal = qa_vec_scale(batch->view.axis[0], -1);
    float uv = batch->family == QA_GAME_Q2 ? .0625f : 0;
    for (size_t i = 0; i < batch->count; ++i) {
        const qa_scene_particle_sample *sample = batch->samples + i;
        float depth = qa_vec_dot(qa_vec_sub(sample->origin, batch->view.origin), batch->view.axis[0]);
        float scale = depth < 20 ? 1 : 1 + depth * .004f;
        qa_scene_vec4 color = sample->color;
        color.w = batch->family == QA_GAME_Q1 ? 1 :
            (float)(uint8_t)(uint32_t)qa_source_float_to_i32(color.w * 255.0f) / 255.0f;
        for (unsigned corner = 0; corner < 3; ++corner) {
            qa_scene_vertex *vertex = renderer->particle_vertices + i * 3 + corner;
            memset(vertex, 0, sizeof(*vertex));
            vertex->position = corner == 1 ? qa_vec_add(sample->origin, qa_vec_scale(batch->view.axis[2], 1.5f * scale)) :
                corner == 2 ? qa_vec_add(sample->origin, qa_vec_scale(batch->view.axis[1], -1.5f * scale)) : sample->origin;
            vertex->normal = normal;
            vertex->texcoord = (qa_scene_vec2){uv + (corner == 1 ? 1 : 0), uv + (corner == 2 ? 1 : 0)};
            vertex->color = color;
            if (!i && !corner) draw->mesh.bounds.mins = draw->mesh.bounds.maxs = vertex->position;
            else draw->mesh.bounds = qa_bounds_union(draw->mesh.bounds, (qa_bounds){vertex->position, vertex->position});
        }
    }
    return true;
}

void gl_particles_destroy(qa_gl_renderer *renderer)
{
    free(renderer->particle_vertices);
    free(renderer->particle_indices);
    renderer->particle_vertices = NULL;
    renderer->particle_indices = NULL;
    renderer->particle_capacity = 0;
}

#include "internal.h"

static uint64_t hash_word(uint64_t hash, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8) {
        hash ^= (value >> shift) & 255u;
        hash *= UINT64_C(1099511628211);
    }
    return hash;
}

static uint64_t hash_position(uint64_t hash, qa_vec3 position) {
    float values[3] = {position.x, position.y, position.z};
    for (unsigned i = 0; i < 3; ++i) {
        uint32_t word;
        memcpy(&word, &values[i], sizeof(word));
        hash = hash_word(hash, word);
    }
    return hash;
}

static bool caster_identity(qa_scene_model *model, uint32_t entity, uint64_t *out, qa_error *error) {
    for (scene_model_shadow_identity *entry = model->shadow_identities; entry; entry = entry->next)
        if (entry->entity == entity) { *out = entry->identity; return true; }
    scene_model_shadow_identity *entry = malloc(sizeof(*entry));
    if (!entry) { qa_error_set(error, QA_ERROR_MEMORY, 0, "model caster identity allocation failed"); return false; }
    *entry = (scene_model_shadow_identity){entity, qa_scene_identity(), model->shadow_identities};
    model->shadow_identities = entry; *out = entry->identity;
    return true;
}

bool qa_scene_model_shadow_caster(qa_scene_model *model, const qa_scene_model_input *input,
                                  qa_scene_frame *frame, qa_scene_shadow_caster *out,
                                  qa_error *error) {
    if (!model || !input || !frame || !out) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "model shadow caster requires model, input, frame and output"); return false;
    }
    qa_scene_model_input shadow = *input;
    shadow.shadow_only = true; shadow.no_cull = true;
    size_t first = frame->command_count, groups = frame->group_count;
    if (!qa_scene_model_submit(model, &shadow, frame, error)) return false;
    size_t count = 0;
    for (size_t i = first; i < frame->command_count; ++i)
        if (frame->commands[i].kind == QA_SCENE_COMMAND_DRAW && frame->commands[i].data.draw.mesh.index_count) ++count;
    qa_scene_shadow_caster caster = {.bounds = model_bounds_empty(), .mesh_count = count};
    qa_scene_matrix_identity(&caster.transform);
    if (!count) {
        caster.bounds = (qa_bounds){0}; *out = caster;
        frame->command_count = first; frame->group_count = groups;
        return true;
    }
    if (count > SIZE_MAX / sizeof(qa_scene_mesh)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "model shadow mesh list exceeds addressable storage"); goto fail;
    }
    qa_scene_mesh *meshes = qa_arena_alloc(&frame->storage, count * sizeof(*meshes), _Alignof(qa_scene_mesh), error);
    if (!meshes || !caster_identity(model, input->entity, &caster.identity, error)) goto fail;
    uint64_t hash = UINT64_C(14695981039346656037);
    size_t mesh_index = 0;
    for (size_t i = first; i < frame->command_count; ++i) {
        if (frame->commands[i].kind != QA_SCENE_COMMAND_DRAW) continue;
        const qa_scene_draw *draw = &frame->commands[i].data.draw;
        if (!draw->mesh.index_count) continue;
        if (draw->mesh.vertex_count > SIZE_MAX / sizeof(qa_scene_vertex)) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "model shadow vertex span exceeds addressable storage"); goto fail;
        }
        qa_scene_mesh *mesh = &meshes[mesh_index++];
        *mesh = draw->mesh; mesh->identity = mesh->revision = 0; mesh->bounds = model_bounds_empty();
        qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage,
            mesh->vertex_count * sizeof(*vertices), _Alignof(qa_scene_vertex), error);
        if (!vertices) goto fail;
        memcpy(vertices, mesh->vertices, mesh->vertex_count * sizeof(*vertices));
        mesh->vertices = vertices;
        hash = hash_word(hash, (uint32_t)mesh->vertex_count);
        hash = hash_word(hash, (uint32_t)mesh->index_count);
        for (size_t vertex = 0; vertex < mesh->vertex_count; ++vertex) {
            qa_scene_vec4 point = qa_scene_matrix_point(draw->model, vertices[vertex].position);
            vertices[vertex].position = qa_v3(point.x, point.y, point.z);
            qa_vec3 normal = vertices[vertex].normal;
            const float *matrix = draw->model.m;
            vertices[vertex].normal = qa_vec_normalize(qa_v3(
                matrix[0] * normal.x + matrix[4] * normal.y + matrix[8] * normal.z,
                matrix[1] * normal.x + matrix[5] * normal.y + matrix[9] * normal.z,
                matrix[2] * normal.x + matrix[6] * normal.y + matrix[10] * normal.z));
            if (!qa_vec_finite(vertices[vertex].position)) {
                qa_error_set(error, QA_ERROR_FORMAT, vertex, "model shadow transform is nonfinite"); goto fail;
            }
            hash = hash_position(hash, vertices[vertex].position);
            model_bounds_add(&mesh->bounds, vertices[vertex].position);
        }
        for (size_t index = 0; index < mesh->index_count; ++index) hash = hash_word(hash, mesh->indices[index]);
        caster.bounds = qa_bounds_union(caster.bounds, mesh->bounds);
    }
    caster.meshes = meshes; caster.revision = hash; *out = caster;
    frame->command_count = first; frame->group_count = groups;
    return true;
fail:
    frame->command_count = first; frame->group_count = groups;
    return false;
}

#include "internal.h"
#include <stdio.h>

typedef struct corner_entry {
    uint32_t vertex, texcoord, index;
    bool seam, occupied;
} corner_entry;

static size_t corner_hash(uint32_t vertex, uint32_t texcoord, bool seam, size_t mask) {
    uint64_t key = ((uint64_t)vertex << 32) | texcoord;
    key ^= seam ? UINT64_C(0xd6e8feb86659fd93) : 0;
    key ^= key >> 32;
    key *= UINT64_C(0xd6e8feb86659fd93);
    key ^= key >> 32;
    return (size_t)key & mask;
}

bool scene_model_topology(qa_scene_model *model, uint32_t mesh_index, qa_error *error) {
    const qa_model_mesh *source = &model->source->meshes[mesh_index];
    scene_model_mesh *mesh = &model->meshes[mesh_index];
    if (source->triangle_count > UINT32_MAX / 3) goto too_large;
    size_t corners = (size_t)source->triangle_count * 3;
    if (corners > SIZE_MAX / sizeof(*mesh->vertices) || corners > SIZE_MAX / sizeof(*mesh->indices)) goto too_large;
    mesh->vertices = calloc(corners ? corners : 1, sizeof(*mesh->vertices));
    mesh->sources = calloc(corners ? corners : 1, sizeof(*mesh->sources));
    mesh->indices = calloc(corners ? corners : 1, sizeof(*mesh->indices));
    mesh->shaders = calloc(source->shader_count ? source->shader_count : 1, sizeof(*mesh->shaders));
    if (!mesh->vertices || !mesh->sources || !mesh->indices || !mesh->shaders) goto memory;
    size_t capacity = 16;
    if (corners > SIZE_MAX / 2) goto too_large;
    while (capacity < corners * 2) {
        if (capacity > SIZE_MAX / 2) goto too_large;
        capacity *= 2;
    }
    if (capacity > SIZE_MAX / sizeof(corner_entry)) goto too_large;
    corner_entry *table = calloc(capacity, sizeof(*table));
    if (!table) goto memory;
    size_t vertex_count = 0;
    qa_bounds bounds = model_bounds_empty();
    for (uint32_t triangle = 0; triangle < source->triangle_count; ++triangle) {
        const qa_model_triangle *face = &source->triangles[triangle];
        for (uint32_t corner = 0; corner < 3; ++corner) {
            uint32_t vertex = face->vertex[corner], uv = face->texcoord[corner];
            if (vertex >= source->vertex_count || uv >= source->texcoord_count) {
                free(table); qa_error_set(error, QA_ERROR_FORMAT, triangle, "model topology index exceeds retained arrays"); return false;
            }
            bool seam = model->source->format == QA_MODEL_MDL && !face->front && source->texcoords[uv].on_seam;
            size_t slot = corner_hash(vertex, uv, seam, capacity - 1);
            while (table[slot].occupied && (table[slot].vertex != vertex || table[slot].texcoord != uv || table[slot].seam != seam))
                slot = (slot + 1) & (capacity - 1);
            if (!table[slot].occupied) {
                uint32_t index = (uint32_t)vertex_count++;
                table[slot] = (corner_entry){vertex, uv, index, seam, true};
                mesh->sources[index] = vertex;
                qa_scene_vertex *output = &mesh->vertices[index];
                output->position = model_vec(source->vertices[vertex].position);
                output->normal = model_vec(source->vertices[vertex].normal);
                float coordinate[2];
                if (!qa_model_corner_uv(model->source, mesh_index, triangle, corner, coordinate)) {
                    free(table); qa_error_set(error, QA_ERROR_FORMAT, triangle, "model corner has no texture coordinate"); return false;
                }
                output->texcoord = (qa_scene_vec2){coordinate[0], coordinate[1]};
                output->color = (qa_scene_vec4){1, 1, 1, 1};
                model_bounds_add(&bounds, output->position);
            }
            mesh->indices[(size_t)triangle * 3 + corner] = table[slot].index;
        }
    }
    free(table);
    if (model->source->format == QA_MODEL_MDL || model->source->format == QA_MODEL_MD2) {
        size_t count = source->vertex_count;
        if (source->frame_count && count > SIZE_MAX / source->frame_count) goto too_large;
        count *= source->frame_count;
        mesh->normal_indices = malloc(count ? count : 1);
        if (!mesh->normal_indices) goto memory;
        for (size_t i = 0; i < count; ++i) mesh->normal_indices[i] = scene_model_normal_index(source->vertices[i].normal);
    }
    for (uint32_t i = 0; i < source->shader_count; ++i) {
        qa_bytes view = qa_model_shader_name(&source->shaders[i]);
        if (view.size == SIZE_MAX || (view.size && (!view.data || memchr(view.data, 0, view.size)))) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "invalid embedded model shader name"); return false;
        }
        if (!view.size) continue;
        char *name = malloc(view.size + 1);
        if (!name) goto memory;
        memcpy(name, view.data, view.size); name[view.size] = 0;
        bool ok = scene_model_external(model, name, &mesh->shaders[i], error);
        free(name);
        if (!ok) return false;
    }
    qa_scene_geometry *geometry = qa_scene_geometry_adopt(mesh->vertices, mesh->indices, error);
    if (!geometry) return false;
    mesh->retained = (qa_scene_mesh){.identity = qa_scene_identity(),
        .revision = 1, .vertices = mesh->vertices, .indices = mesh->indices,
        .vertex_count = vertex_count, .index_count = corners,
        .bounds = vertex_count ? bounds : (qa_bounds){0}, .primitive = QA_SCENE_TRIANGLES,
        .geometry = geometry};
    return true;
too_large:
    qa_error_set(error, QA_ERROR_MEMORY, mesh_index, "model topology exceeds addressable storage"); return false;
memory:
    qa_error_set(error, QA_ERROR_MEMORY, mesh_index, "model topology allocation failed"); return false;
}

void scene_model_topology_destroy(qa_scene_model *model) {
    if (!model->meshes) return;
    for (uint32_t i = 0; i < model->source->mesh_count; ++i) {
        scene_model_mesh *mesh = &model->meshes[i];
        if (mesh->retained.geometry) qa_scene_geometry_release(mesh->retained.geometry);
        else { free(mesh->vertices); free(mesh->indices); }
        free(mesh->sources);
        free(mesh->normal_indices); free(mesh->shaders);
    }
    free(model->meshes);
}

#include "internal.h"
#include <float.h>
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

static void influence_bounds(const qa_model *model, uint32_t index, scene_model_mesh *mesh)
{
    const qa_model_mesh *source = &model->meshes[index];
    if (!source->vertex_weights || !source->weights || !source->vertex_count) return;
    mesh->min_bias_sum = INFINITY;
    for (uint32_t vertex = 0; vertex < source->vertex_count; ++vertex) {
        qa_model_weight_range range = source->vertex_weights[vertex];
        if (range.first > source->weight_count || range.count > source->weight_count - range.first) return;
        qa_vec3 normal = model_vec(source->vertices[vertex].normal);
        if (!qa_vec_finite(normal)) return;
        double sum = 0;
        for (uint32_t i = 0; i < range.count; ++i) {
            const qa_model_weight *weight = &source->weights[range.first + i];
            if (weight->bone >= model->bone_count || !isfinite(weight->bias) ||
                weight->bias < 0 || weight->bias > 1) return;
            qa_vec3 offset = model_vec(weight->offset);
            if (!qa_vec_finite(offset)) return;
            scene_model_influence *influence = &mesh->influences[weight->bone];
            if (!influence->used) {
                influence->offsets = influence->normals = model_bounds_empty();
                influence->used = true;
            }
            model_bounds_add(&influence->offsets, offset);
            model_bounds_add(&influence->normals, normal);
            sum += weight->bias;
        }
        double error = sum * ((double)range.count * DBL_EPSILON) /
            (1 - (double)range.count * DBL_EPSILON) + (double)range.count * DBL_TRUE_MIN;
        mesh->min_bias_sum = fmin(mesh->min_bias_sum, fmax(0, nextafter(sum - error, -INFINITY)));
        mesh->max_bias_sum = fmax(mesh->max_bias_sum, nextafter(sum + error, INFINITY));
        if (range.count > mesh->max_weights) mesh->max_weights = range.count;
    }
    mesh->influence_bounds_ready = true;
}

bool scene_model_topology(qa_scene_model *model, uint32_t mesh_index, qa_error *error) {
    const qa_model_mesh *source = &model->source->meshes[mesh_index];
    scene_model_mesh *mesh = &model->meshes[mesh_index];
    if (source->triangle_count > UINT32_MAX / 3) goto too_large;
    size_t corners = (size_t)source->triangle_count * 3;
    if (mesh_index == 0) model->source_topology = qa_material_library_has_source_profile(model->materials) &&
        (model->source->format == QA_MODEL_MD3 || model->source->format == QA_MODEL_MD4);
    size_t allocated_vertices = model->source_topology ? source->vertex_count : corners;
    size_t source_vertices = source->vertex_count;
    if (allocated_vertices > SIZE_MAX / sizeof(*mesh->vertices) ||
        allocated_vertices > SIZE_MAX / sizeof(*mesh->sources) || corners > SIZE_MAX / sizeof(*mesh->indices) ||
        source_vertices > SIZE_MAX / sizeof(*mesh->sampled)) goto too_large;
    mesh->vertices = calloc(allocated_vertices ? allocated_vertices : 1, sizeof(*mesh->vertices));
    mesh->sources = calloc(allocated_vertices ? allocated_vertices : 1, sizeof(*mesh->sources));
    mesh->indices = calloc(corners ? corners : 1, sizeof(*mesh->indices));
    mesh->shaders = calloc(source->shader_count ? source->shader_count : 1, sizeof(*mesh->shaders));
    size_t sample_count = model->source->format == QA_MODEL_MD5 ? SCENE_MODEL_POSE_VARIANTS + 2 : 1;
    if (source_vertices > SIZE_MAX / sample_count / sizeof(*mesh->sampled)) goto too_large;
    mesh->sampled = calloc(source_vertices ? source_vertices * sample_count : 1, sizeof(*mesh->sampled));
    if (!mesh->vertices || !mesh->sources || !mesh->indices || !mesh->shaders || !mesh->sampled) goto memory;
    for (size_t i = 0; i < sample_count; ++i) {
        mesh->samples[i].skin = (qa_scene_skin_sample){.vertices = mesh->sampled + i * source_vertices,
            .count = source_vertices, .view = {.vertices = source->vertices,
                .vertex_stride = sizeof(qa_model_vertex), .normal_offset = offsetof(qa_model_vertex, normal),
                .weights = source->weights, .ranges = source->vertex_weights, .vertex_count = source_vertices,
                .weight_count = source->weight_count, .bone_count = model->source->bone_count}};
    }
    if (model->source->format == QA_MODEL_MD5) {
        size_t joints = model->source->bone_count;
        if (joints > SIZE_MAX / sizeof(*mesh->influences)) goto too_large;
        mesh->influences = calloc(joints ? joints : 1, sizeof(*mesh->influences));
        if (!mesh->influences) goto memory;
        influence_bounds(model->source, mesh_index, mesh);
    }
    size_t capacity = 0;
    corner_entry *table = NULL;
    if (!model->source_topology) {
        capacity = 16;
        if (corners > SIZE_MAX / 2) goto too_large;
        while (capacity < corners * 2) {
            if (capacity > SIZE_MAX / 2) goto too_large;
            capacity *= 2;
        }
        if (capacity > SIZE_MAX / sizeof(corner_entry)) goto too_large;
        table = calloc(capacity, sizeof(*table));
        if (!table) goto memory;
    }
    size_t vertex_count = 0;
    qa_bounds bounds = model_bounds_empty();
    if (model->source_topology) {
        if (source->texcoord_count != source->vertex_count) {
            free(table); qa_error_set(error, QA_ERROR_FORMAT, mesh_index, "Source model has no physical per-vertex UV array"); return false;
        }
        vertex_count = source->vertex_count;
        for (uint32_t i = 0; i < source->vertex_count; ++i) {
            mesh->sources[i] = i;
            mesh->vertices[i] = (qa_scene_vertex){.position = model_vec(source->vertices[i].position),
                .normal = model_vec(source->vertices[i].normal),
                .texcoord = {source->texcoords[i].uv[0], source->texcoords[i].uv[1]}, .color = {1, 1, 1, 1}};
            model_bounds_add(&bounds, mesh->vertices[i].position);
        }
    }
    for (uint32_t triangle = 0; triangle < source->triangle_count; ++triangle) {
        const qa_model_triangle *face = &source->triangles[triangle];
        for (uint32_t corner = 0; corner < 3; ++corner) {
            uint32_t vertex = face->vertex[corner], uv = face->texcoord[corner];
            if (vertex >= source->vertex_count || uv >= source->texcoord_count) {
                free(table); qa_error_set(error, QA_ERROR_FORMAT, triangle, "model topology index exceeds retained arrays"); return false;
            }
            if (model->source_topology) {
                mesh->indices[(size_t)triangle * 3 + corner] = vertex;
                continue;
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
    size_t allocated_corners = corners ? corners : 1;
    qa_scene_skeletal_input skeletal = {.weights = source->weights, .ranges = source->vertex_weights,
        .sources = mesh->sources, .vertex_count = vertex_count, .source_vertex_count = source_vertices,
        .weight_count = source->weight_count, .bone_count = model->source->bone_count};
    qa_scene_geometry_input geometry_input = {.vertices = mesh->vertices,
        .vertex_count = allocated_vertices ? allocated_vertices : 1,
        .indices = mesh->indices, .index_count = allocated_corners,
        .skeletal = model->source->format == QA_MODEL_MD5 ? &skeletal : NULL};
    qa_scene_geometry *geometry = qa_scene_geometry_adopt(&geometry_input, error);
    if (!geometry) return false;
    mesh->retained = (qa_scene_mesh){.identity = qa_scene_identity(),
        .revision = 1, .vertices = mesh->vertices, .indices = mesh->indices,
        .vertex_count = vertex_count, .index_count = corners,
        .bounds = vertex_count ? bounds : (qa_bounds){.mins = {0, 0, 0}, .maxs = {0, 0, 0}}, .primitive = QA_SCENE_TRIANGLES,
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
        free(mesh->sampled);
        free(mesh->influences);
    }
    free(model->meshes);
}

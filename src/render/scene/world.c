#include "world/internal.h"
#include "qa/scene_effects.h"
#include "qa/scene_world_save.h"
#include "qa/material_library_save.h"
#include "qa/binary.h"

#include <float.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool world_error(qa_error *error, qa_status status, const char *message)
{
    qa_error_set(error, status, 0, "%s", message);
    return false;
}
uint64_t qa_scene_world_identity(const qa_scene_world *world)
{ return world ? world->identity : 0; }
bool qa_scene_world_idle(const qa_scene_world *world)
{ return world && !world->transaction_depth && !world->admission_change_count; }
size_t qa_scene_world_material_binding_count(const qa_scene_world *world)
{ return world ? world->surface_count : 0; }
bool qa_scene_world_material_binding_at(const qa_scene_world *world, size_t index, qa_scene_world_material_binding *out)
{
    if (!world || !out || index >= world->surface_count) return false;
    const qaw_surface *surface = &world->surfaces[index];
    *out = (qa_scene_world_material_binding){.current = surface->material, .base_current = surface->base_material};
    return true;
}
static bool world_material_member(const qa_material_library *library, const qa_material *material, uint64_t world)
{
    if (!material) return true;
    qa_material_library_record_view record;
    return qa_material_library_record_read(library, material->sorted_index, &record) && record.material == material &&
        material->order_entry && (!record.world_identity || record.world_identity == world);
}
bool qa_scene_world_materials_rebind_ready(const qa_scene_world *world, const qa_material_library *current,
    const qa_material_library *destination, const qa_scene_world_material_binding *bindings, size_t count, qa_error *error)
{
    if (!qa_scene_world_idle(world) || !current || !destination || world->materials != current ||
        !qa_material_library_order_ready(current) || !qa_material_library_order_ready(destination) ||
        qa_material_library_resource_owner(destination) != world->resources ||
        count != world->surface_count || (count && !bindings))
        return world_error(error, QA_ERROR_ARGUMENT, "world material publication requires idle qualified owners");
    for (size_t i = 0; i < count; ++i) {
        const qaw_surface *surface = &world->surfaces[i]; const qa_scene_world_material_binding *binding = &bindings[i];
        if (binding->current != surface->material || binding->base_current != surface->base_material ||
            (binding->current != NULL) != (binding->destination != NULL) ||
            (binding->base_current != NULL) != (binding->base_destination != NULL) ||
            !world_material_member(current, binding->current, world->identity) ||
            !world_material_member(current, binding->base_current, world->identity) ||
            !world_material_member(destination, binding->destination, world->identity) ||
            !world_material_member(destination, binding->base_destination, world->identity))
            return world_error(error, QA_ERROR_ARGUMENT, "world material binding differs from its actual surface owners");
    }
    return true;
}
void qa_scene_world_materials_rebind(qa_scene_world *world, qa_material_library *destination,
    const qa_scene_world_material_binding *bindings)
{
    for (size_t i = 0; i < world->surface_count; ++i) {
        world->surfaces[i].material = bindings[i].destination;
        world->surfaces[i].base_material = bindings[i].base_destination;
    }
    world->materials = destination;
}

static void *world_array(size_t count, size_t stride, qa_error *error)
{
    if (count == 0) return NULL;
    if (count > SIZE_MAX / stride) {
        world_error(error, QA_ERROR_MEMORY, "world array exceeds address space");
        return NULL;
    }
    void *result = calloc(count, stride);
    if (result == NULL) world_error(error, QA_ERROR_MEMORY, "cannot allocate world array");
    return result;
}

static bool world_copy(qa_bytes source, qa_buffer *out, qa_error *error)
{
    if (source.size == 0) return true;
    if (source.data == NULL) return world_error(error, QA_ERROR_ARGUMENT, "missing world byte span");
    out->data = world_array(source.size, 1, error);
    if (out->data == NULL) return false;
    out->size = source.size;
    memcpy(out->data, source.data, source.size);
    return true;
}

char *qaw_string(qa_bytes bytes, qa_error *error)
{
    if (bytes.size == SIZE_MAX || (bytes.size != 0 && bytes.data == NULL)) {
        world_error(error, QA_ERROR_ARGUMENT, "invalid world string");
        return NULL;
    }
    char *result = world_array(bytes.size + 1, 1, error);
    if (result != NULL && bytes.size != 0) memcpy(result, bytes.data, bytes.size);
    return result;
}

bool qaw_mesh_allocate(qa_scene_world *world, qaw_surface *surface,
                       size_t vertices, size_t indices, qa_error *error)
{
    if (vertices > UINT32_MAX || indices > UINT32_MAX)
        return world_error(error, QA_ERROR_FORMAT, "world surface exceeds mesh index range");
    surface->vertices = world_array(vertices, sizeof(*surface->vertices), error);
    if (vertices != 0 && surface->vertices == NULL) return false;
    surface->indices = world_array(indices, sizeof(*surface->indices), error);
    if (indices != 0 && surface->indices == NULL) return false;
    qa_scene_geometry *geometry = qa_scene_geometry_adopt(surface->vertices, vertices, surface->indices, indices, error);
    if (geometry == NULL) return false;
    surface->mesh = (qa_scene_mesh){
        .identity = qa_scene_identity(),
        .revision = 1, .vertices = surface->vertices, .indices = surface->indices,
        .vertex_count = vertices, .index_count = indices, .primitive = QA_SCENE_TRIANGLES,
        .geometry = geometry
    };
    (void)world;
    return true;
}

void qaw_mesh_bounds(qaw_surface *surface)
{
    qa_bounds bounds = {qa_v3(FLT_MAX, FLT_MAX, FLT_MAX), qa_v3(-FLT_MAX, -FLT_MAX, -FLT_MAX)};
    for (size_t i = 0; i < surface->mesh.vertex_count; ++i) {
        qa_vec3 point = surface->vertices[i].position;
        bounds.mins = qa_v3(fminf(bounds.mins.x, point.x), fminf(bounds.mins.y, point.y), fminf(bounds.mins.z, point.z));
        bounds.maxs = qa_v3(fmaxf(bounds.maxs.x, point.x), fmaxf(bounds.maxs.y, point.y), fmaxf(bounds.maxs.z, point.z));
    }
    surface->mesh.bounds = surface->mesh.vertex_count == 0 ? (qa_bounds){0} : bounds;
}

qa_vec3 qaw_local_point(const qa_model_transform *transform, qa_vec3 point)
{
    qa_model_transform inverse;
    if (!qa_model_transform_inverse(transform, &inverse)) return qa_v3(0, 0, 0);
    float source[3] = {point.x, point.y, point.z}, result[3];
    qa_model_transform_point(&inverse, source, result);
    return qa_v3(result[0], result[1], result[2]);
}

qa_vec3 qaw_local_vector(const qa_model_transform *transform, qa_vec3 vector)
{
    qa_model_transform inverse;
    if (!qa_model_transform_inverse(transform, &inverse)) return qa_v3(0, 0, 0);
    float source[3] = {vector.x, vector.y, vector.z}, result[3];
    qa_model_transform_direction(&inverse, source, result);
    return qa_v3(result[0], result[1], result[2]);
}

qa_bounds qaw_transformed_bounds(qa_bounds bounds, const qa_model_transform *transform)
{
    qa_bounds result = {qa_v3(FLT_MAX, FLT_MAX, FLT_MAX), qa_v3(-FLT_MAX, -FLT_MAX, -FLT_MAX)};
    for (unsigned corner = 0; corner < 8; ++corner) {
        float local[3] = {corner & 1 ? bounds.maxs.x : bounds.mins.x,
                          corner & 2 ? bounds.maxs.y : bounds.mins.y,
                          corner & 4 ? bounds.maxs.z : bounds.mins.z}, point[3];
        qa_model_transform_point(transform, local, point);
        result.mins = qa_v3(fminf(result.mins.x, point[0]), fminf(result.mins.y, point[1]), fminf(result.mins.z, point[2]));
        result.maxs = qa_v3(fmaxf(result.maxs.x, point[0]), fmaxf(result.maxs.y, point[1]), fmaxf(result.maxs.z, point[2]));
    }
    return result;
}

static qa_bounds bsp_bounds(qa_bsp_bounds bounds)
{
    return (qa_bounds){bounds.min, bounds.max};
}

static double precise_dot(qa_vec3 a, qa_vec3 b)
{
    return (double)a.x * b.x + (double)a.y * b.y + (double)a.z * b.z;
}

static bool world_topology(qa_scene_world *world, qa_error *error)
{
    world->plane_count = qa_bsp_record_count(&world->bsp, QA_BSP_PLANES);
    world->node_count = qa_bsp_record_count(&world->bsp, QA_BSP_NODES);
    world->leaf_count = qa_bsp_record_count(&world->bsp, QA_BSP_LEAVES);
    world->leaf_surface_count = qa_bsp_record_count(&world->bsp, QA_BSP_LEAF_FACES);
    world->model_count = qa_bsp_record_count(&world->bsp, QA_BSP_MODELS);
    world->surface_count = qa_bsp_record_count(&world->bsp,
        world->bsp.family == QA_BSP_Q3 ? QA_BSP_SURFACES : QA_BSP_FACES);
    if (world->node_count > INT32_MAX || world->leaf_count > INT32_MAX || world->surface_count >= UINT32_MAX)
        return world_error(error, QA_ERROR_FORMAT, "world topology exceeds signed BSP indices");
#define WORLD_ALLOC(member, count) do { \
    world->member = world_array((count), sizeof(*world->member), error); \
    if ((count) != 0 && world->member == NULL) return false; \
} while (0)
    WORLD_ALLOC(planes, world->plane_count);
    WORLD_ALLOC(nodes, world->node_count);
    WORLD_ALLOC(leaves, world->leaf_count);
    WORLD_ALLOC(leaf_surfaces, world->leaf_surface_count);
    WORLD_ALLOC(models, world->model_count);
    WORLD_ALLOC(surfaces, world->surface_count);
    WORLD_ALLOC(surface_marks, world->surface_count);
    WORLD_ALLOC(visible_surfaces, world->surface_count);
    WORLD_ALLOC(surface_lights, world->surface_count);
    WORLD_ALLOC(admitted_surfaces, world->surface_count);
    WORLD_ALLOC(admission_changes, world->surface_count);
    world->admission_change_capacity = world->surface_count;
    world->pending_capacity = world->node_count + 1;
    WORLD_ALLOC(pending, world->pending_capacity);
#undef WORLD_ALLOC
    for (size_t i = 0; i < world->plane_count; ++i)
        if (!qa_bsp_read_plane(&world->bsp, i, &world->planes[i], error)) return false;
    for (size_t i = 0; i < world->node_count; ++i)
        if (!qa_bsp_read_node(&world->bsp, i, &world->nodes[i], error)) return false;
    for (size_t i = 0; i < world->leaf_count; ++i) {
        if (!qa_bsp_read_leaf(&world->bsp, i, &world->leaves[i], error)) return false;
        int64_t cluster = world->leaves[i].cluster;
        if (cluster > INT32_MAX) return world_error(error, QA_ERROR_FORMAT, "world cluster exceeds signed visibility index");
        if (cluster >= 0 && (uint64_t)cluster >= world->cluster_count) world->cluster_count = (uint32_t)cluster + 1;
    }
    for (size_t i = 0; i < world->leaf_surface_count; ++i) {
        int64_t index;
        if (!qa_bsp_read_index(&world->bsp, QA_BSP_LEAF_FACES, i, &index, error)) return false;
        world->leaf_surfaces[i] = (uint32_t)index;
    }
    for (size_t i = 0; i < world->surface_count; ++i) world->surfaces[i].source_index = (uint32_t)i;
    for (size_t i = 0; i < world->model_count; ++i) {
        qaw_model *model = &world->models[i];
        model->identity = qa_scene_identity();
        if (!qa_bsp_read_model(&world->bsp, i, &model->source, error)) return false;
        size_t capacity = model->source.membership_from_tree ? world->surface_count : model->source.faces.count;
        model->surfaces = world_array(capacity, sizeof(*model->surfaces), error);
        if (capacity != 0 && model->surfaces == NULL) return false;
        if (world->bsp.family == QA_BSP_Q3) {
            if (!qa_bsp_model_members(&world->bsp, i, false, model->surfaces, capacity, &model->surface_count, error)) return false;
        } else {
            model->surface_count = capacity;
            for (size_t j = 0; j < capacity; ++j) model->surfaces[j] = model->source.faces.first + (uint32_t)j;
        }
    }
    world->bounds = world->model_count != 0 ? bsp_bounds(world->models[0].source.bounds) : (qa_bounds){0};
    size_t capacity = world->bsp.family == QA_BSP_Q1 ? (world->leaf_count + 7) / 8
        : ((size_t)world->cluster_count + 7) / 8;
    qa_bytes vis = world->bsp.lumps[QA_BSP_VISIBILITY].bytes;
    if (vis.size != 0 && world->bsp.family != QA_BSP_Q1) {
        size_t row = world->bsp.family == QA_BSP_Q3 ? qa_load_u32le(vis.data + 4)
            : ((size_t)qa_load_u32le(vis.data) + 7) / 8;
        if (row > capacity) capacity = row;
    }
    world->pvs_capacity = capacity;
    world->pvs = world_array(capacity, 1, error);
    if (capacity != 0 && world->pvs == NULL) return false;
    world->secondary_pvs = world_array(capacity, 1, error);
    return capacity == 0 || world->secondary_pvs != NULL;
}

bool qa_scene_world_create(const qa_bsp_view *bsp, qa_scene_resources *resources,
                           qa_material_library *materials, const qa_scene_world_options *options,
                           qa_scene_world **out, qa_error *error)
{
    if (out != NULL) *out = NULL;
    if (bsp == NULL || resources == NULL || materials == NULL || out == NULL)
        return world_error(error, QA_ERROR_ARGUMENT, "world creation requires BSP, resources, materials and output");
    qa_scene_world *world = calloc(1, sizeof(*world));
    if (world == NULL) return world_error(error, QA_ERROR_MEMORY, "cannot allocate scene world");
    world->resources = resources;
    world->materials = materials;
    world->options = options != NULL ? *options : (qa_scene_world_options){
        .subdivisions = 4, .q1_water_alpha = 1, .q2_light_modulate = 1, .q3_overbright = 2,
        .images = {.mipmap = true, .transparent_index = -1, .filter = QA_SCENE_LINEAR_MIPMAP_NEAREST}
    };
    world->options.images.family = bsp->family == QA_BSP_Q1 ? QA_SCENE_Q1
        : bsp->family == QA_BSP_Q2 ? QA_SCENE_Q2 : QA_SCENE_Q3;
    world->identity = qa_scene_identity();
    world->revision = 1;
    if (!world_copy(bsp->source, &world->bytes, error)
        || !qa_bsp_open((qa_bytes){world->bytes.data, world->bytes.size}, &world->bsp, error)
        || !qa_bsp_validate(&world->bsp, error)
        || !world_copy(world->options.external_lit, &world->lit_bytes, error)
        || !world_copy(world->options.images.palette_rgb, &world->palette_bytes, error)
        || !world_copy(world->options.images.translation, &world->translation_bytes, error)) goto fail;
    world->options.external_lit = (qa_bytes){world->lit_bytes.data, world->lit_bytes.size};
    world->options.images.palette_rgb = (qa_bytes){world->palette_bytes.data, world->palette_bytes.size};
    world->options.images.translation = (qa_bytes){world->translation_bytes.data, world->translation_bytes.size};
    if (world->options.q2_sky != NULL) {
        world->sky_name = qaw_string((qa_bytes){(const uint8_t *)world->options.q2_sky, strlen(world->options.q2_sky)}, error);
        if (world->sky_name == NULL) goto fail;
        world->options.q2_sky = world->sky_name;
    }
    if (!world_topology(world, error)) goto fail;
    if (world->bsp.family != QA_BSP_Q3) {
        if (!qa_bsp_select_lighting(&world->bsp, world->options.external_lit, &world->lighting, error)) goto fail;
        qa_bsp_extension extension;
        if (qa_bsp_find_extension(&world->bsp, "LIGHTGRID_OCTREE", &extension, NULL)
            && !qa_bsp_read_lightgrid(&world->bsp, &world->lightgrid, error)) goto fail;
    }
    if (world->bsp.family == QA_BSP_Q3 ? !qaw_build_q3(world, error) : !qaw_build_legacy(world, error)) goto fail;
    *out = world;
    return true;
fail:
    qa_scene_world_destroy(world);
    return false;
}

void qa_scene_world_destroy(qa_scene_world *world)
{
    if (world == NULL) return;
    if (world->bsp.family == QA_BSP_Q3) qaw_destroy_q3(world);
    else qaw_destroy_legacy(world);
    for (size_t i = 0; i < world->surface_count && world->surfaces != NULL; ++i) {
        if (world->surfaces[i].mesh.geometry != NULL)
            qa_scene_geometry_release(world->surfaces[i].mesh.geometry);
        else {
            free(world->surfaces[i].vertices);
            free(world->surfaces[i].indices);
        }
        qa_scene_image_release(world->surfaces[i].lightmap);
    }
    for (size_t i = 0; i < world->model_count && world->models != NULL; ++i) free(world->models[i].surfaces);
    qa_bsp_lightgrid_free(&world->lightgrid);
    free(world->planes); free(world->nodes); free(world->leaves); free(world->leaf_surfaces);
    free(world->models); free(world->surfaces); free(world->surface_marks);
    free(world->visible_surfaces); free(world->surface_lights); free(world->admitted_surfaces);
    free(world->admission_changes); free(world->pending);
    free(world->pvs); free(world->secondary_pvs); free(world->sky_name);
    qa_buffer_free(&world->bytes); qa_buffer_free(&world->lit_bytes);
    qa_buffer_free(&world->palette_bytes); qa_buffer_free(&world->translation_bytes);
    free(world);
}

int32_t qa_scene_world_leaf(const qa_scene_world *world, qa_vec3 point)
{
    if (world == NULL || !qa_vec_finite(point) || world->leaf_count == 0) return -1;
    if (world->node_count == 0) return 0;
    int32_t child = 0;
    while (child >= 0) {
        const qa_bsp_node *node = &world->nodes[child];
        const qa_bsp_plane *plane = &world->planes[node->plane];
        child = node->children[precise_dot(point, plane->normal) > plane->distance ? 0 : 1];
    }
    return (int32_t)(-1 - (int64_t)child);
}

static bool update_pvs(qa_scene_world *world, int32_t eye, qa_vec3 origin,
                        const qa_scene_world_input *input, qa_error *error)
{
    int32_t selector = world->bsp.family == QA_BSP_Q1 ? eye : (int32_t)world->leaves[eye].cluster;
    int32_t second = -1;
    if (world->bsp.family == QA_BSP_Q2) {
        if (input->use_secondary_cluster) second = input->secondary_cluster;
        else {
            origin.z += world->leaves[eye].contents == 0 ? -16.0f : 16.0f;
            int32_t probe = qa_scene_world_leaf(world, origin);
            if (probe >= 0 && (world->leaves[probe].contents & 1) == 0
                && world->leaves[probe].cluster != selector) second = (int32_t)world->leaves[probe].cluster;
        }
    }
    if (world->pvs_cached && selector == world->pvs_selector && second == world->pvs_secondary) return true;
    world->pvs_cached = false;
    world->pvs_all = selector < 0 || world->bsp.lumps[QA_BSP_VISIBILITY].bytes.size == 0;
    qa_bytes vis = world->bsp.lumps[QA_BSP_VISIBILITY].bytes;
    if (!world->pvs_all && world->bsp.family == QA_BSP_Q2) {
        uint32_t clusters = qa_load_u32le(vis.data);
        if ((uint32_t)selector >= clusters || (second >= 0 && (uint32_t)second >= clusters))
            return world_error(error, QA_ERROR_ARGUMENT, "scene PVS cluster is outside map visibility");
        world->pvs_all = qa_load_i32le(vis.data + 4 + (size_t)selector * 8) < 0
            || (second >= 0 && qa_load_i32le(vis.data + 4 + (size_t)second * 8) < 0);
    }
    if (!world->pvs_all && world->bsp.family == QA_BSP_Q3
        && (uint32_t)selector >= qa_load_u32le(vis.data)) world->pvs_all = true;
    if (!world->pvs_all) {
        if (!qa_bsp_visibility(&world->bsp, selector, false, world->cluster_count,
            world->pvs, world->pvs_capacity, &world->pvs_size, error)) return false;
        if (world->bsp.family == QA_BSP_Q2 && second >= 0) {
            size_t bytes;
            if (!qa_bsp_visibility(&world->bsp, second, false, world->cluster_count,
                world->secondary_pvs, world->pvs_capacity, &bytes, error)) return false;
            if (bytes != world->pvs_size) return world_error(error, QA_ERROR_FORMAT, "secondary PVS width differs");
            for (size_t i = 0; i < bytes; ++i) world->pvs[i] |= world->secondary_pvs[i];
        }
    }
    world->pvs_selector = selector;
    world->pvs_secondary = second;
    world->pvs_cached = true;
    return true;
}

static bool remaining_planes(qa_bounds bounds, const qa_scene_plane *planes,
                              size_t count, uint32_t *bits)
{
    for (size_t i = 0; i < count; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        if ((*bits & bit) == 0) continue;
        qa_vec3 n = planes[i].normal;
        qa_vec3 front = qa_v3(n.x < 0 ? bounds.mins.x : bounds.maxs.x,
            n.y < 0 ? bounds.mins.y : bounds.maxs.y, n.z < 0 ? bounds.mins.z : bounds.maxs.z);
        if ((float)(precise_dot(front, n) - planes[i].distance) < 0) return false;
        qa_vec3 back = qa_v3(n.x < 0 ? bounds.maxs.x : bounds.mins.x,
            n.y < 0 ? bounds.maxs.y : bounds.mins.y, n.z < 0 ? bounds.maxs.z : bounds.mins.z);
        if ((float)(precise_dot(back, n) - planes[i].distance) >= 0) *bits &= ~bit;
    }
    return true;
}

static uint32_t all_lights(size_t count)
{
    return count >= 32 ? UINT32_MAX : (UINT32_C(1) << count) - 1;
}

static const qa_scene_light *projected_lights(const qa_scene_world *world,
                                              const qa_scene_world_input *input, size_t *count)
{
    if (input->use_projected_lights) {
        *count = input->projected_light_count;
        return input->projected_lights;
    }
    *count = world->bsp.family == QA_BSP_Q3 ? input->light_count : 0;
    return *count != 0 ? input->lights : NULL;
}

static bool world_visible(qa_scene_world *world, const qa_scene_world_input *input, qa_error *error)
{
    world->visible_count = 0;
    qa_vec3 origin = input->use_pvs_origin ? input->pvs_origin : input->view.origin;
    int32_t eye = qa_scene_world_leaf(world, origin);
    if (eye < 0) return true;
    if (!input->no_vis && !update_pvs(world, eye, origin, input, error)) return false;
    if (++world->visibility_generation == 0) {
        memset(world->surface_marks, 0, world->surface_count * sizeof(*world->surface_marks));
        world->visibility_generation = 1;
    }
    qa_scene_plane planes[6];
    size_t plane_count = input->no_cull ? 0 : qa_scene_frustum(&input->view, planes);
    if (world->bsp.family == QA_BSP_Q3 && plane_count > 4) plane_count = 4;
    size_t pending = 1;
    size_t light_count;
    const qa_scene_light *lights = projected_lights(world, input, &light_count);
    world->pending[0] = (qaw_pending){world->node_count == 0 ? -1 : 0,
        all_lights(light_count), (UINT32_C(1) << plane_count) - 1};
    while (pending != 0) {
        qaw_pending item = world->pending[--pending];
        if (item.child >= 0) {
            const qa_bsp_node *node = &world->nodes[item.child];
            if (!remaining_planes(bsp_bounds(node->bounds), planes, plane_count, &item.planes)) continue;
            uint32_t masks[2] = {item.lights, item.lights};
            unsigned front = 0;
            const qa_bsp_plane *plane = &world->planes[node->plane];
            if (world->bsp.family == QA_BSP_Q3) {
                masks[0] = masks[1] = 0;
                for (size_t i = 0; i < light_count; ++i) {
                    uint32_t bit = UINT32_C(1) << i;
                    if ((item.lights & bit) == 0) continue;
                    float distance = (float)(precise_dot(lights[i].origin, plane->normal) - plane->distance);
                    if (distance > -lights[i].radius) masks[0] |= bit;
                    if (distance < lights[i].radius) masks[1] |= bit;
                }
            } else front = precise_dot(input->view.origin, plane->normal) >= plane->distance ? 0u : 1u;
            world->pending[pending++] = (qaw_pending){node->children[front ^ 1], masks[front ^ 1], item.planes};
            world->pending[pending++] = (qaw_pending){node->children[front], masks[front], item.planes};
            continue;
        }
        size_t index = (size_t)(-1 - (int64_t)item.child);
        const qa_bsp_leaf *leaf = &world->leaves[index];
        if (world->bsp.family == QA_BSP_Q1 && index == 0) continue;
        if (world->bsp.family != QA_BSP_Q1 && input->visible_areas != NULL
            && (leaf->area < 0 || (uint64_t)leaf->area / 8 >= input->visible_area_bytes
                || (input->visible_areas[(size_t)leaf->area / 8] & (1u << ((unsigned)leaf->area & 7))) == 0)) continue;
        if (world->bsp.family == QA_BSP_Q3 && (input->no_vis || world->leaves[eye].cluster < 0) && leaf->cluster == -1) continue;
        if (!input->no_vis && !world->pvs_all) {
            int64_t bit = world->bsp.family == QA_BSP_Q1 ? (int64_t)index - 1 : leaf->cluster;
            if (bit < 0 || (uint64_t)bit / 8 >= world->pvs_size
                || (world->pvs[(size_t)bit / 8] & (1u << ((unsigned)bit & 7))) == 0) continue;
        }
        if (!remaining_planes(bsp_bounds(leaf->bounds), planes, plane_count, &item.planes)) continue;
        for (size_t i = leaf->faces.first; i < (size_t)leaf->faces.first + leaf->faces.count; ++i) {
            uint32_t surface = world->leaf_surfaces[i];
            if (world->surface_marks[surface] == world->visibility_generation) continue;
            world->surface_marks[surface] = world->visibility_generation;
            world->surface_lights[surface] = item.lights;
            world->visible_surfaces[world->visible_count++] = surface;
        }
    }
    return true;
}

qa_bounds qa_scene_world_bounds(const qa_scene_world *world)
{
    return world != NULL ? world->bounds : (qa_bounds){0};
}

bool qa_scene_world_sample_light(const qa_scene_world *world, qa_vec3 point,
                                 qa_vec3 *ambient, qa_vec3 *directed, qa_vec3 *direction)
{
    if (world == NULL || ambient == NULL || directed == NULL || direction == NULL || !qa_vec_finite(point)) return false;
    *ambient = qa_v3(1, 1, 1);
    *directed = qa_v3(0, 0, 0);
    *direction = qa_v3(0, 0, 1);
    return world->bsp.family == QA_BSP_Q3 ? qaw_sample_q3_light(world, point, ambient, directed, direction)
        : qaw_sample_legacy_light(world, point, ambient, directed, direction);
}

static bool valid_input(const qa_scene_world *world, const qa_scene_world_input *input, qa_error *error)
{
    if (world == NULL || input == NULL)
        return world_error(error, QA_ERROR_ARGUMENT, "world submission requires world and input");
    if (!qa_vec_finite(input->view.origin) || !isfinite(input->seconds)
        || (input->use_pvs_origin && !qa_vec_finite(input->pvs_origin))
        || (input->light_count != 0 && input->lights == NULL)
        || (input->shadow_light_count != 0 && input->shadow_lights == NULL)
        || (input->render_text_count != 0 && input->render_texts == NULL))
        return world_error(error, QA_ERROR_ARGUMENT, "invalid world view or light inputs");
    size_t projected_count;
    const qa_scene_light *projected = projected_lights(world, input, &projected_count);
    if (projected_count > 32 || (projected_count != 0 && projected == NULL))
        return world_error(error, QA_ERROR_ARGUMENT, "projected world lights exceed source 32-light mask or are absent");
    for (size_t i = 0; i < projected_count; ++i)
        if (!qa_vec_finite(projected[i].origin) || !qa_vec_finite(projected[i].color)
            || !isfinite(projected[i].radius) || projected[i].radius < 0)
            return world_error(error, QA_ERROR_ARGUMENT, "invalid projected world light");
    for (size_t i = 0; i < input->light_count; ++i) {
        const qa_scene_light *light = &input->lights[i];
        if (!qa_vec_finite(light->origin) || !qa_vec_finite(light->color) || !isfinite(light->radius)
            || light->radius < 0 || !isfinite(light->minimum))
            return world_error(error, QA_ERROR_ARGUMENT, "invalid world dynamic light");
    }
    return true;
}

static bool begin_admission(qa_scene_world *world, qa_scene_frame *frame, qa_error *error)
{
    size_t view = frame->command_count;
    while (view != 0 && frame->commands[view - 1].kind != QA_SCENE_COMMAND_VIEW) --view;
    if (world->admission_frame == frame && world->admission_sequence == frame->sequence
        && world->admission_view == view && world->admission_generation != 0) return true;
    if (world->admission_generation == UINT64_MAX)
        return world_error(error, QA_ERROR_MEMORY, "world view admission identity space exhausted");
    world->admission_frame = frame;
    world->admission_sequence = frame->sequence;
    world->admission_view = view;
    ++world->admission_generation;
    return true;
}

static bool admit_surface(qa_scene_world *world, uint32_t index, bool *admitted, qa_error *error)
{
    *admitted = false;
    if (world->admitted_surfaces[index] == world->admission_generation) return true;
    if (world->admission_change_count == world->admission_change_capacity) {
        size_t capacity = world->admission_change_capacity;
        if (capacity > SIZE_MAX / 2 / sizeof(*world->admission_changes))
            return world_error(error, QA_ERROR_MEMORY, "surface admission journal exceeds address space");
        capacity = capacity == 0 ? 1 : capacity * 2;
        qaw_admission_change *replacement = realloc(world->admission_changes, capacity * sizeof(*replacement));
        if (replacement == NULL) return world_error(error, QA_ERROR_MEMORY, "cannot grow surface admission journal");
        world->admission_changes = replacement;
        world->admission_change_capacity = capacity;
    }
    world->admission_changes[world->admission_change_count++] = (qaw_admission_change){index, world->admitted_surfaces[index]};
    world->admitted_surfaces[index] = world->admission_generation;
    *admitted = true;
    return true;
}

static const qa_material *effective_material(const qa_material *material)
{
    for (unsigned hops = 0; material != NULL && material->remapped != NULL && hops < 16384; ++hops)
        material = material->remapped;
    return material;
}

static bool local_bounds_visible(qa_bounds bounds, const qa_model_transform *transform,
                                  const qa_scene_plane *planes, size_t count)
{
    for (size_t plane = 0; plane < count; ++plane) {
        bool visible = false;
        for (unsigned corner = 0; corner < 8; ++corner) {
            float local[3] = {corner & 1 ? bounds.maxs.x : bounds.mins.x,
                corner & 2 ? bounds.maxs.y : bounds.mins.y, corner & 4 ? bounds.maxs.z : bounds.mins.z};
            float point[3] = {local[0], local[1], local[2]};
            if (transform != NULL) qa_model_transform_point(transform, local, point);
            if (precise_dot(qa_v3(point[0], point[1], point[2]), planes[plane].normal) > planes[plane].distance) {
                visible = true;
                break;
            }
        }
        if (!visible) return false;
    }
    return true;
}

static bool surface_culled(const qa_scene_world *world, const qaw_surface *surface,
                           const qa_scene_world_input *input, const qa_material_context *context,
                           const qa_model_transform *transform, const qa_scene_plane *planes, size_t count)
{
    if (input->no_cull) return false;
    if (world->bsp.family != QA_BSP_Q3)
        return transform == NULL && !qa_scene_bounds_visible(surface->mesh.bounds, planes, count);
    if (surface->skip || surface->flare) return false;
    if (surface->type == QA_BSP_SURFACE_TRIANGLES)
        return !local_bounds_visible(surface->mesh.bounds, transform, planes, count);
    if (surface->type == QA_BSP_SURFACE_PATCH) {
        qa_vec3 center = qa_vec_scale(qa_vec_add(surface->mesh.bounds.mins, surface->mesh.bounds.maxs), 0.5f);
        float radius = qa_vec_length(qa_vec_sub(surface->mesh.bounds.mins, center));
        if (transform != NULL) {
            float source[3] = {center.x, center.y, center.z}, result[3];
            qa_model_transform_point(transform, source, result);
            center = qa_v3(result[0], result[1], result[2]);
            radius *= fmaxf(fabsf(transform->scale[0]), fmaxf(fabsf(transform->scale[1]), fabsf(transform->scale[2])));
        }
        bool clipped = false;
        for (size_t i = 0; i < count; ++i) {
            float distance = (float)(precise_dot(center, planes[i].normal) - planes[i].distance);
            if (distance < -radius) return true;
            if (distance <= radius) clipped = true;
        }
        return clipped && !local_bounds_visible(surface->mesh.bounds, transform, planes, count);
    }
    const qa_material *material = effective_material(surface->material);
    if (!surface->has_plane || material == NULL || material->cull == QA_CULL_NONE) return false;
    double viewer = precise_dot(context->local_view_origin, surface->plane.normal);
    return material->cull == QA_CULL_FRONT ? viewer < (float)(surface->plane.distance - 8.0f)
        : viewer > (float)(surface->plane.distance + 8.0f);
}

static uint32_t surface_light_mask(const qaw_surface *surface, uint32_t mask,
                                    const qa_scene_light *lights, size_t count)
{
    if (surface->skip || surface->flare) return 0;
    if (surface->type == QA_BSP_SURFACE_TRIANGLES) return mask;
    for (size_t i = 0; i < count && i < 32; ++i) {
        uint32_t bit = UINT32_C(1) << i;
        if ((mask & bit) == 0) continue;
        const qa_scene_light *light = &lights[i];
        if (surface->has_plane && surface->type != QA_BSP_SURFACE_PATCH) {
            float distance = (float)(precise_dot(light->origin, surface->plane.normal) - surface->plane.distance);
            if (distance < -light->radius || distance > light->radius) mask &= ~bit;
        } else {
            qa_vec3 o = light->origin, min = surface->mesh.bounds.mins, max = surface->mesh.bounds.maxs;
            float r = light->radius;
            if (o.x - r > max.x || o.x + r < min.x || o.y - r > max.y || o.y + r < min.y
                || o.z - r > max.z || o.z + r < min.z) mask &= ~bit;
        }
    }
    return mask;
}

static qa_material_context world_context(const qa_scene_world *world, const qa_scene_world_input *input)
{
    qa_material_context context = {
        .view = input->view, .entity_color = {1, 1, 1, 1}, .local_view_origin = input->view.origin,
        .identity_light = input->identity_light, .seconds = input->seconds, .milliseconds = input->milliseconds,
        .fog = input->fog,
        .entity = 1022, .mirror = input->view.mirror, .texts = input->render_texts,
        .text_count = input->render_text_count, .video_frame = input->video_frame,
        .video_context = input->video_context
    };
    context.lights = projected_lights(world, input, &context.light_count);
    qa_scene_matrix_identity(&context.model);
    return context;
}

static bool fragment_context(const qa_scene_world *world, const qa_scene_world_input *input,
                               qa_scene_frame *frame, qa_material_context *context, qa_error *error)
{
    context->fragment_lighting = input->shadow_lights != NULL;
    context->fragment_light_count = input->shadow_light_count;
    context->shadow_atlas = input->shadow_atlas;
    context->shadow_near = 4;
    if (input->shadow_light_count == 0) return true;
    if (input->shadow_light_count > SIZE_MAX / sizeof(qa_scene_shadow_light))
        return world_error(error, QA_ERROR_MEMORY, "fragment lights exceed address space");
    qa_scene_shadow_light *lights = qa_arena_alloc(&frame->storage,
        input->shadow_light_count * sizeof(*lights), _Alignof(qa_scene_shadow_light), error);
    if (lights == NULL) return false;
    for (size_t i = 0; i < input->shadow_light_count; ++i) {
        lights[i] = input->shadow_lights[i];
        lights[i].light.scale *= world->options.q2_light_modulate;
    }
    context->fragment_lights = lights;
    return true;
}

typedef struct surface_order { uint32_t surface; size_t ordinal; float sort; } surface_order;

static int compare_surface_priority(const void *a, const void *b)
{
    const surface_order *first = a, *second = b;
    if (first->sort < second->sort) return -1;
    if (first->sort > second->sort) return 1;
    return first->ordinal < second->ordinal ? -1 : first->ordinal > second->ordinal;
}

static bool submit_surface(qa_scene_world *world, qaw_surface *surface, qa_material_context *context,
                            const qa_scene_world_input *input, qa_scene_frame *frame, qa_error *error)
{
    context->lightmap = surface->lightmap;
    context->fog_index = surface->fog_index;
    context->time_offset = surface->material_time_offset +
        (input->entity_material ? input->entity_material->shader_time : 0.0f);
    size_t first = frame->command_count;
    bool result = world->bsp.family == QA_BSP_Q3 ? qaw_submit_q3(world, surface, context, input, frame, error)
        : qaw_submit_legacy(world, surface, context, input, frame, error);
    if (!result) return false;
    float priority = surface->material != NULL ? surface->material->sort : surface->sort;
    if (surface->material == NULL && !surface->sky && context->entity_color.w < 1) priority = 9;
    const qa_material *effective = effective_material(surface->material);
    if (frame->command_count != first && (effective != NULL ? effective->sky : surface->sky)) world->sky_drawn = true;
    return qa_scene_frame_group(frame, first,
        surface->material == NULL ? QA_SCENE_GROUP_SEQUENCE
        : input->source_order ? QA_SCENE_GROUP_SOURCE : QA_SCENE_GROUP_COMPILED,
        surface->material, priority, context->entity, context->fog_index, context->light_mask != 0, error);
}

static bool world_submit(qa_scene_world *world, const qa_scene_world_input *input,
                           qa_scene_frame *frame, qa_error *error)
{
    if (frame == NULL) return world_error(error, QA_ERROR_ARGUMENT, "world submission requires frame");
    if (!valid_input(world, input, error)) return false;
    if (world->bsp.family != QA_BSP_Q3 && !qawl_light_styles(world, input, error)) return false;
    world->sky_drawn = false;
    qa_scene_command view = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = input->view};
    if (!qa_scene_frame_emit(frame, &view, error)) return false;
    world->admission_frame = NULL;
    if (!begin_admission(world, frame, error)) return false;
    if (input->no_world) return true;
    if (!world_visible(world, input, error)) return false;
    if (world->visible_count == 0) return true;
    surface_order *order = qa_arena_alloc(&frame->storage, world->visible_count * sizeof(*order),
        _Alignof(surface_order), error);
    if (order == NULL) return false;
    size_t count = 0;
    bool raw_surfaces = false;
    for (size_t i = 0; i < world->visible_count; ++i) {
        uint32_t index = world->visible_surfaces[i];
        if (world->bsp.family != QA_BSP_Q3 && world->model_count != 0) {
            qa_bsp_range faces = world->models[0].source.faces;
            if (index < faces.first || index - faces.first >= faces.count) continue;
        }
        const qaw_surface *surface = &world->surfaces[index];
        const qa_material *material = effective_material(surface->material);
        raw_surfaces |= world->bsp.family != QA_BSP_Q3 && surface->base_material == NULL;
        order[count++] = (surface_order){index, i, material != NULL ? material->sort : surface->sort};
    }
    if (raw_surfaces) qsort(order, count, sizeof(*order), compare_surface_priority);
    qa_scene_plane planes[6];
    size_t plane_count = input->no_cull ? 0 : qa_scene_frustum(&input->view, planes);
    if (world->bsp.family == QA_BSP_Q3 && plane_count > 4) plane_count = 4;
    qa_material_context context = world_context(world, input);
    if (!fragment_context(world, input, frame, &context, error)) return false;
    for (size_t i = 0; i < count; ++i) {
        qaw_surface *surface = &world->surfaces[order[i].surface];
        bool admitted = true;
        if (input->source_order && !admit_surface(world, surface->source_index, &admitted, error)) return false;
        if (!admitted || surface_culled(world, surface, input, &context, NULL, planes, plane_count)) continue;
        context.light_mask = surface_light_mask(surface, world->surface_lights[surface->source_index], context.lights, context.light_count);
        if (!submit_surface(world, surface, &context, input, frame, error)) return false;
    }
    return true;
}

static bool world_submit_model(qa_scene_world *world, uint32_t model_index,
                                 const qa_model_transform *transform, const qa_scene_world_input *input,
                                 uint32_t entity, qa_scene_vec4 color, qa_scene_frame *frame, qa_error *error)
{
    if (frame == NULL) return world_error(error, QA_ERROR_ARGUMENT, "inline model submission requires frame");
    if (!valid_input(world, input, error)) return false;
    if (transform == NULL || model_index >= world->model_count || (input->source_order && entity >= 1022)
        || !isfinite(color.x) || !isfinite(color.y) || !isfinite(color.z) || !isfinite(color.w))
        return world_error(error, QA_ERROR_ARGUMENT, "invalid inline model or source entity");
    for (unsigned axis = 0; axis < 3; ++axis)
        if (!isfinite(transform->origin[axis]))
            return world_error(error, QA_ERROR_ARGUMENT, "nonfinite inline model origin");
    qa_model_transform inverse;
    if (!qa_model_transform_inverse(transform, &inverse))
        return world_error(error, QA_ERROR_ARGUMENT, "inline model transform is singular");
    if (world->bsp.family != QA_BSP_Q3 && !qawl_light_styles(world, input, error)) return false;
    if (!begin_admission(world, frame, error)) return false;
    const qaw_model *model = &world->models[model_index];
    qa_scene_plane planes[6];
    size_t plane_count = input->no_cull ? 0 : qa_scene_frustum(&input->view, planes);
    if (world->bsp.family == QA_BSP_Q3 && plane_count > 4) plane_count = 4;
    if (world->bsp.family == QA_BSP_Q3
        && !local_bounds_visible(bsp_bounds(model->source.bounds), transform, planes, plane_count)) return true;
    qa_material_context context = world_context(world, input);
    if (!fragment_context(world, input, frame, &context, error)) return false;
    context.entity = entity;
    context.entity_color = color;
    if (input->entity_material) {
        const qa_scene_world_entity *material = input->entity_material;
        context.ambient = material->ambient;
        context.ambient_alpha = 1.0f;
        context.directed = material->directed;
        context.light_direction = material->light_direction;
        context.entity_texcoord = material->shader_texcoord;
        context.shadow_plane = material->shadow_plane;
        context.projection_shadow = material->projection_shadow;
    }
    context.model = qa_scene_model_matrix(transform);
    context.local_view_origin = qaw_local_point(transform, input->view.origin);
    context.non_normalized_axis = transform->scale[0] != 1 || transform->scale[1] != 1 ||
        transform->scale[2] != 1 ||
        (input->entity_material && input->entity_material->non_normalized_axis);
    float scale = fminf(fabsf(transform->scale[0]), fminf(fabsf(transform->scale[1]), fabsf(transform->scale[2])));
    if (!(scale > 0) || !isfinite(scale)) return world_error(error, QA_ERROR_ARGUMENT, "invalid inline model scale");
    qa_scene_world_input local_input = *input;
    qa_scene_light *legacy_lights = NULL;
    if (input->light_count != 0) {
        if (input->light_count > SIZE_MAX / sizeof(*legacy_lights)) return world_error(error, QA_ERROR_MEMORY, "inline lights exceed address space");
        legacy_lights = qa_arena_alloc(&frame->storage, input->light_count * sizeof(*legacy_lights), _Alignof(qa_scene_light), error);
        if (legacy_lights == NULL) return false;
        for (size_t i = 0; i < input->light_count; ++i) {
            legacy_lights[i] = input->lights[i];
            legacy_lights[i].origin = qaw_local_point(transform, legacy_lights[i].origin);
            legacy_lights[i].direction = qa_vec_normalize(qaw_local_vector(transform, legacy_lights[i].direction));
            legacy_lights[i].radius /= scale;
            legacy_lights[i].minimum /= scale;
            legacy_lights[i].color = qa_vec_scale(legacy_lights[i].color, scale);
        }
    }
    local_input.lights = legacy_lights;
    const qa_scene_light *source_lights = context.lights;
    qa_scene_light *lights = NULL;
    if (context.light_count != 0) {
        lights = qa_arena_alloc(&frame->storage, context.light_count * sizeof(*lights), _Alignof(qa_scene_light), error);
        if (lights == NULL) return false;
        for (size_t i = 0; i < context.light_count; ++i) {
            lights[i] = source_lights[i];
            lights[i].origin = qaw_local_point(transform, lights[i].origin);
            lights[i].direction = qa_vec_normalize(qaw_local_vector(transform, lights[i].direction));
            lights[i].radius /= scale;
            lights[i].minimum /= scale;
        }
    }
    context.lights = lights;
    uint32_t incoming = all_lights(context.light_count);
    bool source_inline = input->source_order && world->bsp.family == QA_BSP_Q3 && !context.non_normalized_axis;
    if (source_inline) {
        incoming = 0;
        qa_bounds bounds = bsp_bounds(model->source.bounds);
        for (size_t i = 0; i < context.light_count; ++i) {
            qa_vec3 o = lights[i].origin;
            float r = lights[i].radius;
            if (o.x - bounds.maxs.x > r || bounds.mins.x - o.x > r
                || o.y - bounds.maxs.y > r || bounds.mins.y - o.y > r
                || o.z - bounds.maxs.z > r || bounds.mins.z - o.z > r) continue;
            incoming = 1;
            break;
        }
    }
    for (size_t i = 0; i < model->surface_count; ++i) {
        qaw_surface *surface = &world->surfaces[model->surfaces[i]];
        bool admitted = true;
        if (input->source_order && !admit_surface(world, surface->source_index, &admitted, error)) return false;
        if (!admitted || surface_culled(world, surface, input, &context, transform, planes, plane_count)) continue;
        context.light_mask = surface_light_mask(surface, incoming, source_inline ? source_lights : lights, context.light_count);
        if (!submit_surface(world, surface, &context, &local_input, frame, error)) return false;
    }
    return true;
}

typedef struct world_transaction {
    size_t commands, groups, changes, view;
    const qa_scene_frame *admission_frame;
    uint64_t sequence, generation;
    bool sky_drawn;
} world_transaction;

static world_transaction transaction_begin(qa_scene_world *world, const qa_scene_frame *frame)
{
    ++world->transaction_depth;
    return (world_transaction){frame->command_count, frame->group_count, world->admission_change_count,
        world->admission_view, world->admission_frame, world->admission_sequence,
        world->admission_generation, world->sky_drawn};
}

static bool transaction_end(qa_scene_world *world, qa_scene_frame *frame,
                             const world_transaction *start, bool success)
{
    if (!success) {
        frame->command_count = start->commands;
        frame->group_count = start->groups;
        while (world->admission_change_count > start->changes) {
            qaw_admission_change change = world->admission_changes[--world->admission_change_count];
            world->admitted_surfaces[change.surface] = change.previous;
        }
        world->admission_view = start->view;
        world->admission_frame = start->admission_frame;
        world->admission_sequence = start->sequence;
        world->admission_generation = start->generation;
        world->sky_drawn = start->sky_drawn;
    }
    if (--world->transaction_depth == 0) world->admission_change_count = 0;
    return success;
}

bool qa_scene_world_submit(qa_scene_world *world, const qa_scene_world_input *input,
                           qa_scene_frame *frame, qa_error *error)
{
    if (world == NULL || frame == NULL)
        return world_error(error, QA_ERROR_ARGUMENT, "world submission requires world and frame");
    world_transaction start = transaction_begin(world, frame);
    return transaction_end(world, frame, &start, world_submit(world, input, frame, error));
}

bool qa_scene_world_submit_model(qa_scene_world *world, uint32_t model,
                                 const qa_model_transform *transform, const qa_scene_world_input *input,
                                 uint32_t entity, qa_scene_vec4 color, qa_scene_frame *frame, qa_error *error)
{
    if (world == NULL || frame == NULL)
        return world_error(error, QA_ERROR_ARGUMENT, "inline model submission requires world and frame");
    world_transaction start = transaction_begin(world, frame);
    return transaction_end(world, frame, &start,
        world_submit_model(world, model, transform, input, entity, color, frame, error));
}

bool qa_scene_world_sky_drawn(const qa_scene_world *world)
{
    return world != NULL && world->sky_drawn;
}

bool qa_scene_world_shadow_caster(qa_scene_world *world, uint32_t model_index,
                                  const qa_model_transform *transform,
                                  const qa_scene_world_input *input, qa_scene_frame *frame,
                                  qa_scene_shadow_caster *out, qa_error *error)
{
    if (frame == NULL) return world_error(error, QA_ERROR_ARGUMENT, "shadow caster requires frame");
    if (!valid_input(world, input, error)) return false;
    if (out == NULL || model_index >= world->model_count)
        return world_error(error, QA_ERROR_ARGUMENT, "invalid brush shadow caster request");
    qa_material_context context = world_context(world, input);
    if (transform != NULL) {
        for (unsigned axis = 0; axis < 3; ++axis)
            if (!isfinite(transform->origin[axis]))
                return world_error(error, QA_ERROR_ARGUMENT, "nonfinite brush shadow origin");
        qa_model_transform inverse;
        if (!qa_model_transform_inverse(transform, &inverse))
            return world_error(error, QA_ERROR_ARGUMENT, "brush shadow transform is singular");
        context.model = qa_scene_model_matrix(transform);
        context.local_view_origin = qaw_local_point(transform, input->view.origin);
        context.non_normalized_axis = transform->scale[0] != 1 || transform->scale[1] != 1 || transform->scale[2] != 1;
    }
    const qaw_model *model = &world->models[model_index];
    qa_scene_shadow_caster caster = {
        .transform = context.model, .identity = model->identity, .revision = world->revision,
        .world_geometry = model_index == 0 && transform == NULL
    };
    qa_scene_mesh *meshes = NULL;
    if (model->surface_count != 0) {
        if (model->surface_count > SIZE_MAX / sizeof(*meshes))
            return world_error(error, QA_ERROR_MEMORY, "brush shadow mesh list exceeds address space");
        meshes = qa_arena_alloc(&frame->storage, model->surface_count * sizeof(*meshes), _Alignof(qa_scene_mesh), error);
        if (meshes == NULL) return false;
    }
    for (size_t i = 0; i < model->surface_count; ++i) {
        const qaw_surface *surface = &world->surfaces[model->surfaces[i]];
        context.time_offset = surface->material_time_offset;
        qa_scene_mesh mesh = surface->mesh;
        if (surface->flare) continue;
        if (surface->material != NULL) {
            if (!qa_material_shadow_mesh(surface->material, &mesh, &context, frame, &mesh, error)) return false;
        } else if (!qaw_legacy_casts_shadow(world, surface, model_index != 0 || transform != NULL)) continue;
        if (mesh.index_count == 0 || mesh.vertex_count == 0) continue;
        if (!qa_scene_frame_geometry(frame, mesh.geometry, error)) return false;
        caster.bounds = caster.mesh_count == 0 ? mesh.bounds : qa_bounds_union(caster.bounds, mesh.bounds);
        meshes[caster.mesh_count++] = mesh;
    }
    caster.meshes = meshes;
    *out = caster;
    return true;
}

bool qa_scene_world_portal_view(qa_scene_world *world, const qa_scene_world_input *input,
                                const qa_scene_portal *portals, size_t portal_count,
                                qa_scene_view *view, qa_vec3 *pvs_origin, bool *found, qa_error *error)
{
    if (found != NULL) *found = false;
    if (!valid_input(world, input, error)) return false;
    if (view == NULL || pvs_origin == NULL || found == NULL || (portal_count != 0 && portals == NULL))
        return world_error(error, QA_ERROR_ARGUMENT, "invalid world portal-view outputs or entities");
    *view = input->view;
    *pvs_origin = input->view.origin;
    if (input->no_world || input->view.clip_enabled || portal_count == 0) return true;
    if (!world_visible(world, input, error)) return false;
    for (size_t i = 0; i < world->visible_count; ++i) {
        const qaw_surface *surface = &world->surfaces[world->visible_surfaces[i]];
        const qa_material *material = effective_material(surface->material);
        if (!surface->has_plane || material == NULL || material->sort != 1) continue;
        const qa_scene_portal *portal = NULL;
        for (size_t j = 0; j < portal_count; ++j) {
            float distance = (float)(precise_dot(portals[j].origin, surface->plane.normal) - surface->plane.distance);
            if (fabsf(distance) <= 64) { portal = &portals[j]; break; }
        }
        if (portal == NULL) continue;
        qa_scene_view candidate;
        qa_vec3 candidate_pvs;
        if (!qa_scene_portal_view(&input->view, surface->plane, portal, input->seconds, &candidate, &candidate_pvs)) continue;
        if (!qa_scene_portal_surface_visible(&surface->mesh, &input->view, material->portal_range, candidate.mirror)) continue;
        *view = candidate;
        *pvs_origin = candidate_pvs;
        *found = true;
        break;
    }
    return true;
}

bool qa_scene_world_fog_for_sphere(const qa_scene_world *world, qa_vec3 origin, float radius,
                                  qa_scene_fog_volume *out)
{
    if (out == NULL) return false;
    *out = (qa_scene_fog_volume){0};
    if (world == NULL || world->bsp.family != QA_BSP_Q3 || !qa_vec_finite(origin)
        || !isfinite(radius) || radius < 0) return false;
    return qaw_q3_fog_for_sphere(world, origin, radius, out);
}

bool qa_scene_world_fog_for_bounds(const qa_scene_world *world, qa_bounds bounds,
                                  qa_scene_fog_volume *out)
{
    if (out == NULL) return false;
    *out = (qa_scene_fog_volume){0};
    if (world == NULL || world->bsp.family != QA_BSP_Q3 ||
        !qa_vec_finite(bounds.mins) || !qa_vec_finite(bounds.maxs) ||
        bounds.mins.x > bounds.maxs.x || bounds.mins.y > bounds.maxs.y ||
        bounds.mins.z > bounds.maxs.z) return false;
    return qaw_q3_fog_for_bounds(world, bounds, out);
}

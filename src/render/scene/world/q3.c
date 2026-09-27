#include "internal.h"
#include "q3/patch.h"
#include "qa/scene_effects.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { Q3_LIGHTMAP_EDGE = 128, Q3_LIGHTMAP_BYTES = 128 * 128 * 3,
       Q3_LIGHTMAP_PIXELS = 128 * 128, Q3_LIGHTMAP_IMAGE_BYTES = 128 * 128 * 4 };

typedef struct q3_fog {
    qa_scene_fog fog;
    qa_bounds bounds;
    qa_scene_plane surface;
    float tc_scale;
    bool active, has_surface;
} q3_fog;

typedef struct q3_data {
    qa_scene_image **lightmaps;
    size_t lightmap_count;
    q3_fog *fogs;
    size_t fog_count;
    qa_bsp_grid_point *grid;
    size_t grid_count, grid_bounds[3];
    qa_vec3 grid_origin, grid_inverse;
} q3_data;

static void shift_color(const uint8_t input[3], uint32_t shift, uint8_t output[3]) {
    uint32_t r = (uint32_t)input[0] << shift, g = (uint32_t)input[1] << shift;
    uint32_t b = (uint32_t)input[2] << shift;
    uint32_t maximum = r > g ? r : g;
    if (b > maximum) maximum = b;
    if (maximum > 255) { r = r * 255 / maximum; g = g * 255 / maximum; b = b * 255 / maximum; }
    output[0] = (uint8_t)r; output[1] = (uint8_t)g; output[2] = (uint8_t)b;
}

static qa_scene_vertex vertex(const qa_bsp_vertex *source, uint32_t shift) {
    uint8_t color[3]; shift_color(source->color, shift, color);
    return (qa_scene_vertex){source->position, source->normal,
        {source->texcoord[0], source->texcoord[1]},
        {source->lightmap_coord[0], source->lightmap_coord[1]},
        {(float)color[0] / 255, (float)color[1] / 255, (float)color[2] / 255, (float)source->color[3] / 255}};
}

static float fog_byte(float value) {
    float scaled = value * 255;
    if (!isfinite(scaled)) return 0;
    int reduced = (int)fmodf(truncf(scaled), 256);
    return (float)((unsigned)reduced & 255u) / 255;
}

static bool load_lightmaps(qa_scene_world *world, q3_data *data, qa_error *error) {
    qa_bytes bytes = world->bsp.lumps[QA_BSP_LIGHTING].bytes;
    if (bytes.size % Q3_LIGHTMAP_BYTES) {
        qa_error_set(error, QA_ERROR_FORMAT, 0, "Q3 lightmap lump is not a sequence of 128x128 RGB images");
        return false;
    }
    data->lightmap_count = bytes.size / Q3_LIGHTMAP_BYTES;
    if (!data->lightmap_count) return true;
    data->lightmaps = calloc(data->lightmap_count, sizeof(*data->lightmaps));
    uint8_t *pixels = malloc(Q3_LIGHTMAP_IMAGE_BYTES);
    if (!data->lightmaps || !pixels) {
        free(pixels); qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 lightmaps"); return false;
    }
    for (size_t i = 0; i < data->lightmap_count; ++i) {
        for (size_t p = 0; p < Q3_LIGHTMAP_PIXELS; ++p) {
            shift_color(bytes.data + i * Q3_LIGHTMAP_BYTES + p * 3, world->options.q3_overbright, pixels + p * 4);
            pixels[p * 4 + 3] = 255;
        }
        char name[96];
        (void)snprintf(name, sizeof(name), "*world-%" PRIu64 "-q3-lightmap-%zu", world->identity, i);
        qa_scene_image_level level = {Q3_LIGHTMAP_EDGE, Q3_LIGHTMAP_EDGE, pixels, Q3_LIGHTMAP_IMAGE_BYTES};
        if (!qa_scene_image_create(world->resources, name, QA_SCENE_RGB8, &level, 1,
                                  QA_SCENE_CLAMP, QA_SCENE_LINEAR, (qa_scene_vec4){0},
                                  data->lightmaps + i, error)) { free(pixels); return false; }
    }
    free(pixels); return true;
}

static bool brush_plane(const qa_scene_world *world, const qa_bsp_brush *brush, uint32_t side,
                         qa_bsp_plane *plane, qa_error *error) {
    qa_bsp_brush_side source;
    if (side >= brush->sides.count) {
        qa_error_set(error, QA_ERROR_FORMAT, brush->sides.first, "Q3 fog side exceeds its brush"); return false;
    }
    return qa_bsp_read_brush_side(&world->bsp, (size_t)brush->sides.first + side, &source, error) &&
           qa_bsp_read_plane(&world->bsp, source.plane, plane, error);
}

static bool load_fogs(qa_scene_world *world, q3_data *data, qa_error *error) {
    data->fog_count = qa_bsp_record_count(&world->bsp, QA_BSP_FOGS);
    if (!data->fog_count) return true;
    data->fogs = calloc(data->fog_count, sizeof(*data->fogs));
    if (!data->fogs) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 fog volumes"); return false; }
    for (size_t i = 0; i < data->fog_count; ++i) {
        qa_bsp_fog source;
        if (!qa_bsp_read_fog(&world->bsp, i, &source, error)) return false;
        char *name = qaw_string(source.name, error);
        if (!name) return false;
        const qa_material *material = NULL;
        bool registered = qa_material_register_kind(world->materials, name, &world->options.images,
                                                     QA_MATERIAL_DYNAMIC, &material, error);
        free(name);
        if (!registered) return false;
        if (source.brush < 0 || material->fog.kind == QA_FOG_NONE) continue;
        qa_bsp_brush brush;
        if (!qa_bsp_read_brush(&world->bsp, (size_t)source.brush, &brush, error)) return false;
        if (brush.sides.count < 6) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Q3 fog brush has fewer than six axial sides"); return false;
        }
        qa_bsp_plane planes[6];
        for (unsigned side = 0; side < 6; ++side)
            if (!brush_plane(world, &brush, side, planes + side, error)) return false;
        q3_fog *fog = data->fogs + i;
        fog->bounds = (qa_bounds){qa_v3(-planes[0].distance, -planes[2].distance, -planes[4].distance),
                                  qa_v3(planes[1].distance, planes[3].distance, planes[5].distance)};
        fog->fog = material->fog;
        fog->fog.color.x = fog_byte(material->fog.color.x);
        fog->fog.color.y = fog_byte(material->fog.color.y);
        fog->fog.color.z = fog_byte(material->fog.color.z);
        fog->tc_scale = 1 / (fmaxf(1, material->fog.amount) * 8);
        fog->active = true;
        if (source.visible_side >= 0) {
            qa_bsp_plane plane;
            if (!brush_plane(world, &brush, (uint32_t)source.visible_side, &plane, error)) return false;
            fog->surface = (qa_scene_plane){qa_vec_scale(plane.normal, -1), -plane.distance};
            fog->has_surface = true;
        }
    }
    return true;
}

static bool equal_key(qa_bytes key, const char *expected) {
    size_t length = strlen(expected);
    if (key.size != length) return false;
    for (size_t i = 0; i < length; ++i)
        if (tolower((unsigned char)key.data[i]) != (unsigned char)expected[i]) return false;
    return true;
}

static bool grid_size(qa_scene_world *world, qa_error *error) {
    world->grid_size = qa_v3(64, 64, 128);
    qa_entities entities = {0};
    if (!qa_entities_parse(world->bsp.lumps[QA_BSP_ENTITIES].bytes, QA_ENTITY_Q3, &entities, error)) return false;
    if (entities.count) {
        const qa_entity_record *record = entities.records;
        for (size_t i = 0; i < record->property_count; ++i) {
            const qa_entity_property *property = entities.properties + record->first_property + i;
            if (!equal_key(property->key, "gridsize")) continue;
            char *text = qaw_string(property->value, error);
            if (!text) { qa_entities_free(&entities); return false; }
            float *axis[3] = {&world->grid_size.x, &world->grid_size.y, &world->grid_size.z};
            char *cursor = text;
            for (unsigned j = 0; j < 3; ++j) {
                char *end;
                float value = strtof(cursor, &end);
                if (end == cursor) break;
                *axis[j] = value; cursor = end;
            }
            free(text);
        }
    }
    qa_entities_free(&entities); return true;
}

static bool load_grid(qa_scene_world *world, q3_data *data, qa_error *error) {
    if (!grid_size(world, error)) return false;
    qa_vec3 size = world->grid_size;
    if (!qa_vec_finite(size) || size.x <= 0 || size.y <= 0 || size.z <= 0 || !world->model_count) return true;
    qa_bsp_bounds bounds = world->models[0].source.bounds;
    float minimum[3] = {bounds.min.x, bounds.min.y, bounds.min.z};
    float maximum[3] = {bounds.max.x, bounds.max.y, bounds.max.z};
    float spacing[3] = {size.x, size.y, size.z}, origin[3];
    size_t total = 1, count = qa_bsp_record_count(&world->bsp, QA_BSP_LIGHTGRID);
    if (!count) return true;
    for (unsigned axis = 0; axis < 3; ++axis) {
        origin[axis] = spacing[axis] * ceilf(minimum[axis] / spacing[axis]);
        float end = spacing[axis] * floorf(maximum[axis] / spacing[axis]);
        float samples = (end - origin[axis]) / spacing[axis] + 1;
        if (!isfinite(origin[axis]) || !isfinite(samples) || samples < 1 || (double)samples > (double)count) return true;
        data->grid_bounds[axis] = (size_t)samples;
        if (data->grid_bounds[axis] > count / total) return true;
        total *= data->grid_bounds[axis];
    }
    if (total != count) return true;
    data->grid = calloc(count, sizeof(*data->grid));
    if (!data->grid) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 light grid"); return false; }
    data->grid_count = count; data->grid_origin = qa_v3(origin[0], origin[1], origin[2]);
    data->grid_inverse = qa_v3(1 / size.x, 1 / size.y, 1 / size.z);
    for (size_t i = 0; i < count; ++i) {
        qa_bsp_grid_point point;
        if (!qa_bsp_read_grid_point(&world->bsp, i, &point, error)) return false;
        shift_color(point.ambient, world->options.q3_overbright, data->grid[i].ambient);
        shift_color(point.directed, world->options.q3_overbright, data->grid[i].directed);
        memcpy(data->grid[i].lat_long, point.lat_long, sizeof(point.lat_long));
    }
    return true;
}

static bool load_surface(qa_scene_world *world, q3_data *data, const qa_bsp_materials *materials,
                          size_t index, qa_error *error) {
    qa_bsp_surface source;
    if (!qa_bsp_read_surface(&world->bsp, index, &source, error)) return false;
    qaw_surface *surface = world->surfaces + index;
    surface->source_index = (uint32_t)index; surface->type = source.type;
    if (index >= materials->surface_count || materials->surfaces[index] >= materials->shader_count) {
        qa_error_set(error, QA_ERROR_FORMAT, index, "Q3 surface shader is outside its material table"); return false;
    }
    const qa_bsp_shader *shader = materials->shaders + materials->surfaces[index];
    surface->skip = source.type == QA_BSP_SURFACE_PATCH && ((uint32_t)shader->surface_flags & 0x80u) != 0;
    surface->flare = source.type == QA_BSP_SURFACE_FLARE;
    int32_t lightmap = source.type == QA_BSP_SURFACE_PLANAR || source.type == QA_BSP_SURFACE_PATCH ? source.lightmap : -3;
    if (lightmap >= 0 && (size_t)lightmap < data->lightmap_count) {
        surface->lightmap = data->lightmaps[lightmap];
        qa_scene_image_retain(surface->lightmap);
    }
    char *name = qaw_string(shader->name, error);
    if (!name) return false;
    qa_material_registration_kind kind = surface->lightmap ? QA_MATERIAL_LIGHTMAP :
        lightmap == -3 ? QA_MATERIAL_VERTEX : lightmap == -2 ? QA_MATERIAL_WHITE :
        lightmap == -4 ? QA_MATERIAL_PICTURE : QA_MATERIAL_DYNAMIC;
    bool registered = qa_material_register_world(world->materials, name, &world->options.images,
        world->identity, lightmap, kind, NULL, NULL, &surface->material, error);
    free(name);
    if (!registered) return false;
    if (surface->material->default_shader) {
        surface->material = qa_material_find(world->materials, "*default");
        if (!surface->material) {
            qa_error_set(error, QA_ERROR_FORMAT, index, "Q3 material library has no source default shader");
            return false;
        }
    }
    surface->sky = surface->material->sky;
    surface->sort = surface->material->sort;
    if (source.fog >= 0) {
        surface->fog_index = (uint32_t)source.fog + 1;
        if ((size_t)source.fog < data->fog_count && data->fogs[source.fog].active)
            surface->fog = data->fogs[source.fog].fog;
    }
    if (surface->flare) {
        surface->mesh.bounds = (qa_bounds){source.lightmap_origin, source.lightmap_origin};
        surface->plane = (qa_scene_plane){source.lightmap_vectors[2], qa_vec_dot(source.lightmap_origin, source.lightmap_vectors[2])};
        return true;
    }
    size_t triangles = qa_bsp_surface_triangle_count(&source);
    if (triangles > SIZE_MAX / 3 || !qaw_mesh_allocate(world, surface, source.vertices.count, triangles * 3, error)) return false;
    for (size_t i = 0; i < source.vertices.count; ++i) {
        qa_bsp_vertex point;
        if (!qa_bsp_read_vertex(&world->bsp, (size_t)source.vertices.first + i, &point, error)) return false;
        surface->vertices[i] = vertex(&point, world->options.q3_overbright);
    }
    for (size_t i = 0; i < triangles; ++i)
        if (!qa_bsp_surface_triangle(&world->bsp, &source, i, surface->indices + i * 3, error)) return false;
    if (source.type == QA_BSP_SURFACE_PATCH && !surface->skip) {
        if (!qaw_patch_build(surface, &source, world->options.subdivisions, error)) return false;
    } else {
        qaw_mesh_bounds(surface);
        if (source.type == QA_BSP_SURFACE_PLANAR && surface->mesh.vertex_count) {
            surface->plane = (qa_scene_plane){source.lightmap_vectors[2],
                qa_vec_dot(surface->vertices[0].position, source.lightmap_vectors[2])};
            surface->has_plane = true;
        } else if (source.type != QA_BSP_SURFACE_PATCH && surface->mesh.vertex_count >= 3) {
            qa_vec3 origin = surface->vertices[0].position;
            qa_vec3 normal = qa_vec_normalize(qa_vec_cross(qa_vec_sub(surface->vertices[1].position, origin),
                                                          qa_vec_sub(surface->vertices[2].position, origin)));
            surface->plane = (qa_scene_plane){normal, qa_vec_dot(origin, normal)};
            surface->has_plane = true;
        }
    }
    return true;
}

bool qaw_build_q3(qa_scene_world *world, qa_error *error) {
    if (world->options.q3_overbright > 15) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Q3 overbright shift must be in 0..15"); return false;
    }
    q3_data *data = calloc(1, sizeof(*data));
    if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "allocating Q3 world data"); return false; }
    world->q3_data = data;
    if (!load_lightmaps(world, data, error) || !load_fogs(world, data, error) || !load_grid(world, data, error)) return false;
    qa_bsp_materials materials = {0};
    if (!qa_bsp_build_materials(&world->bsp, &materials, error)) return false;
    bool ok = true;
    for (size_t i = 0; i < world->surface_count && ok; ++i) ok = load_surface(world, data, &materials, i, error);
    qa_bsp_materials_free(&materials);
    return ok && qaw_patch_prepare(world, error);
}

void qaw_destroy_q3(qa_scene_world *world) {
    q3_data *data = world->q3_data;
    if (!data) return;
    for (size_t i = 0; i < world->surface_count; ++i) { free(world->surfaces[i].patch); world->surfaces[i].patch = NULL; }
    if (data->lightmaps) for (size_t i = 0; i < data->lightmap_count; ++i) qa_scene_image_release(data->lightmaps[i]);
    free(data->lightmaps); free(data->fogs); free(data->grid); free(data); world->q3_data = NULL;
}

static qa_scene_fog_volume fog_volume(const q3_fog *fog, size_t index)
{
    return (qa_scene_fog_volume){(uint32_t)index + 1, fog->fog, fog->tc_scale,
        fog->has_surface, fog->surface};
}

bool qaw_q3_fog_for_sphere(const qa_scene_world *world, qa_vec3 origin, float radius,
                           qa_scene_fog_volume *out) {
    *out = (qa_scene_fog_volume){0};
    const q3_data *data = world->q3_data;
    if (!data) return false;
    for (size_t i = 0; i < data->fog_count; ++i) {
        const q3_fog *fog = data->fogs + i;
        if (!fog->active || origin.x - radius >= fog->bounds.maxs.x || origin.x + radius <= fog->bounds.mins.x ||
            origin.y - radius >= fog->bounds.maxs.y || origin.y + radius <= fog->bounds.mins.y ||
            origin.z - radius >= fog->bounds.maxs.z || origin.z + radius <= fog->bounds.mins.z) continue;
        *out = fog_volume(fog, i);
        return true;
    }
    return false;
}

bool qaw_q3_fog_for_bounds(const qa_scene_world *world, qa_bounds bounds,
                           qa_scene_fog_volume *out)
{
    *out = (qa_scene_fog_volume){0};
    const q3_data *data = world->q3_data;
    if (!data) return false;
    for (size_t i = 0; i < data->fog_count; ++i) {
        const q3_fog *fog = data->fogs + i;
        if (!fog->active || bounds.maxs.x < fog->bounds.mins.x || bounds.mins.x > fog->bounds.maxs.x ||
            bounds.maxs.y < fog->bounds.mins.y || bounds.mins.y > fog->bounds.maxs.y ||
            bounds.maxs.z < fog->bounds.mins.z || bounds.mins.z > fog->bounds.maxs.z) continue;
        *out = fog_volume(fog, i);
        return true;
    }
    return false;
}

static qa_vec3 matrix_vector(qa_scene_matrix matrix, qa_vec3 p) {
    return qa_v3(matrix.m[0] * p.x + matrix.m[4] * p.y + matrix.m[8] * p.z,
                 matrix.m[1] * p.x + matrix.m[5] * p.y + matrix.m[9] * p.z,
                 matrix.m[2] * p.x + matrix.m[6] * p.y + matrix.m[10] * p.z);
}

bool qaw_submit_material_sky(const qa_scene_world *world, const qa_material *original,
                             const qa_material *material, const qa_scene_mesh *mesh,
                             const qa_material_context *context, const qa_scene_world_input *input,
                             qa_scene_frame *frame, qa_error *error) {
    qa_scene_mesh transformed = *mesh;
    if (mesh->vertex_count) {
        qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage, mesh->vertex_count * sizeof(*vertices),
                                                  _Alignof(qa_scene_vertex), error);
        if (!vertices) return false;
        for (size_t i = 0; i < mesh->vertex_count; ++i) {
            vertices[i] = mesh->vertices[i];
            qa_scene_vec4 p = qa_scene_matrix_point(context->model, vertices[i].position);
            vertices[i].position = qa_v3(p.x, p.y, p.z);
        }
        transformed.vertices = vertices;
    }
    qa_scene_sky_bounds bounds[6]; qa_scene_sky_bounds_reset(bounds);
    if (!qa_scene_sky_clip(&transformed, 1, context->view.origin, bounds, error)) return false;
    if (input->override_sky) {
        float rotation = input->sky_rotation * (input->sky_auto_rotate ? (float)input->seconds : 1);
        return qa_scene_q2_sky(frame, &input->view, input->sky_images, bounds, rotation, input->sky_axis,
                               input->sky_rotation != 0, (qa_scene_vec4){1,1,1,1}, error);
    }
    float far_clip = 2048;
    for (unsigned i = 0; i < 8; ++i) {
        qa_vec3 point = qa_v3(i & 1 ? world->bounds.maxs.x : world->bounds.mins.x,
                              i & 2 ? world->bounds.maxs.y : world->bounds.mins.y,
                              i & 4 ? world->bounds.maxs.z : world->bounds.mins.z);
        far_clip = fmaxf(far_clip, qa_vec_length(qa_vec_sub(point, context->view.origin)));
    }
    qa_scene_sky_geometry geometry;
    if (!qa_scene_q3_sky_geometry(frame, context->view.origin, far_clip,
        qa_material_library_cloud_height(world->materials), bounds, &geometry, error)) return false;
    qa_material_context sky_context = *context;
    qa_scene_matrix_identity(&sky_context.model);
    sky_context.local_view_origin = context->view.origin;
    sky_context.light_mask = 0;
    static const unsigned sky_image_order[6] = {0, 2, 1, 3, 4, 5};
    for (unsigned i = 0; i < 6; ++i) {
        const qa_scene_image *image = material->sky_outer_images[sky_image_order[i]];
        if (!geometry.visible[i] || !image) continue;
        qa_scene_draw draw = {0};
        draw.mesh = geometry.faces[i]; draw.model = sky_context.model;
        draw.mvp = qa_scene_matrix_multiply(context->view.projection, qa_scene_view_matrix(&context->view));
        draw.textures[0] = image; draw.texture_count = 1;
        draw.entity = context->entity; draw.fog_index = context->fog_index;
        draw.sort_key = ((uint64_t)original->sorted_index << 17) | ((uint64_t)context->entity << 7) |
                        ((uint64_t)context->fog_index << 2) | (context->light_mask != 0 ? 1u : 0u);
        draw.environment = QA_TEXTURE_MODULATE;
        qa_scene_state_default(&draw.state);
        draw.state.cull = QA_CULL_NONE; draw.state.depth_near = draw.state.depth_far = 1;
        qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage, draw.mesh.vertex_count * sizeof(*vertices),
                                                  _Alignof(qa_scene_vertex), error);
        if (!vertices) return false;
        memcpy(vertices, draw.mesh.vertices, draw.mesh.vertex_count * sizeof(*vertices));
        for (size_t v = 0; v < draw.mesh.vertex_count; ++v)
            vertices[v].color = (qa_scene_vec4){context->identity_light, context->identity_light, context->identity_light, 1};
        draw.mesh.vertices = vertices;
        if (!qa_scene_frame_draw(frame, &draw, error)) return false;
    }
    if (geometry.clouds.index_count) {
        size_t first = frame->command_count;
        if (!qa_material_submit(original, &geometry.clouds, &sky_context, frame, error)) return false;
        for (size_t i = first; i < frame->command_count; ++i) if (frame->commands[i].kind == QA_SCENE_COMMAND_DRAW)
            frame->commands[i].data.draw.state.depth_near = frame->commands[i].data.draw.state.depth_far = 1;
    }
    return true;
}

bool qaw_submit_q3(qa_scene_world *world, qaw_surface *surface, const qa_material_context *context,
                   const qa_scene_world_input *input, qa_scene_frame *frame, qa_error *error) {
    if (surface->skip) return true;
    if (surface->flare) {
        if (!input->flare) return true;
        qa_bsp_surface source;
        if (!qa_bsp_read_surface(&world->bsp, surface->source_index, &source, error)) return false;
        qa_scene_vec4 point = qa_scene_matrix_point(context->model, source.lightmap_origin);
        qa_vec3 normal = qa_vec_normalize(matrix_vector(context->model, source.lightmap_vectors[2]));
        return input->flare(input->flare_context, surface->source_index, qa_v3(point.x, point.y, point.z),
                            source.lightmap_vectors[0], normal, &context->view, frame, error);
    }
    qa_material_context local = *context;
    local.lightmap = surface->lightmap;
    const q3_data *data = world->q3_data;
    if (surface->fog_index && surface->fog_index - 1 < data->fog_count && data->fogs[surface->fog_index - 1].active) {
        const q3_fog *fog = data->fogs + surface->fog_index - 1;
        local.fog_volume_color = fog->fog.color; local.fog_tc_scale = fog->tc_scale;
        local.fog_has_surface = fog->has_surface; local.fog_surface = fog->surface;
    }
    qa_scene_mesh mesh = surface->mesh;
    if (surface->patch && !qaw_patch_lod(surface, &local, input->curve_error, frame, &mesh, error)) return false;
    const qa_material *material = surface->material;
    for (size_t hop = 0; material->remapped; ++hop) {
        if (hop >= 16384) {
            qa_error_set(error, QA_ERROR_FORMAT, surface->source_index, "Q3 material remap chain contains a cycle");
            return false;
        }
        material = material->remapped;
    }
    if (material->surface_flags & 0x80u) return true;
    if (material->sky) return qaw_submit_material_sky(world, surface->material, material, &mesh, &local, input, frame, error);
    return qa_material_submit(surface->material, &mesh, &local, frame, error);
}

bool qaw_sample_q3_light(const qa_scene_world *world, qa_vec3 point, qa_vec3 *ambient,
                         qa_vec3 *directed, qa_vec3 *direction) {
    const q3_data *data = world->q3_data;
    if (!data || !data->grid || !qa_vec_finite(point)) return false;
    qa_vec3 delta = qa_vec_sub(point, data->grid_origin);
    float position[3] = {delta.x * data->grid_inverse.x, delta.y * data->grid_inverse.y, delta.z * data->grid_inverse.z};
    size_t lower[3]; float fraction[3];
    for (unsigned axis = 0; axis < 3; ++axis) {
        if (!isfinite(position[axis])) return false;
        float integer = floorf(position[axis]);
        fraction[axis] = position[axis] - integer;
        lower[axis] = integer < 0 ? 0 : (double)integer >= (double)data->grid_bounds[axis] ?
            data->grid_bounds[axis] - 1 : (size_t)integer;
    }
    qa_vec3 a = {0}, d = {0}, n = {0}; float total = 0;
    for (unsigned corner = 0; corner < 8; ++corner) {
        float weight = (corner & 1) ? fraction[0] : 1 - fraction[0];
        weight *= (corner & 2) ? fraction[1] : 1 - fraction[1];
        weight *= (corner & 4) ? fraction[2] : 1 - fraction[2];
        size_t x = lower[0] + (corner & 1), y = lower[1] + ((corner >> 1) & 1), z = lower[2] + ((corner >> 2) & 1);
        size_t index = x + data->grid_bounds[0] * (y + data->grid_bounds[1] * z);
        /* Preserve source row aliases; only its out-of-allocation reads use a replicated edge. */
        if (index >= data->grid_count) {
            if (x >= data->grid_bounds[0]) x = data->grid_bounds[0] - 1;
            if (y >= data->grid_bounds[1]) y = data->grid_bounds[1] - 1;
            if (z >= data->grid_bounds[2]) z = data->grid_bounds[2] - 1;
            index = x + data->grid_bounds[0] * (y + data->grid_bounds[1] * z);
        }
        const qa_bsp_grid_point *sample = data->grid + index;
        if ((unsigned)sample->ambient[0] + sample->ambient[1] + sample->ambient[2] == 0) continue;
        total += weight;
        a = qa_vec_add(a, qa_vec_scale(qa_v3(sample->ambient[0], sample->ambient[1], sample->ambient[2]), weight));
        d = qa_vec_add(d, qa_vec_scale(qa_v3(sample->directed[0], sample->directed[1], sample->directed[2]), weight));
        unsigned latitude = (unsigned)sample->lat_long[1] * 4, longitude = (unsigned)sample->lat_long[0] * 4;
        qa_vec3 normal = qa_v3(qa_material_sine(latitude + 256) * qa_material_sine(longitude),
                               qa_material_sine(latitude) * qa_material_sine(longitude),
                               qa_material_sine(longitude + 256));
        n = qa_vec_add(n, qa_vec_scale(normal, weight));
    }
    if (total > 0 && total < 0.99f) {
        a = qa_vec_scale(a, 1 / total);
        d = qa_vec_scale(d, 1 / total);
    }
    if (ambient) *ambient = qa_vec_scale(a, 1.0f / 255);
    if (directed) *directed = qa_vec_scale(d, 1.0f / 255);
    if (direction) *direction = qa_vec_normalize(n);
    return true;
}

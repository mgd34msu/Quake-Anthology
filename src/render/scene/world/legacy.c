#include "legacy/internal.h"
#include "qa/scene_effects.h"
#include "qa/text.h"
#include "q1_sky.h"
#include "boxed_sky.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool q2_frames(qawl_world *data, qaw_legacy *legacy, qa_error *error)
{
    bool *visited = calloc(data->texture_count ? data->texture_count : 1, sizeof(*visited));
    if (!visited) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate texture animation traversal"); return false; }
    int64_t current = (int64_t)legacy->texture;
    while (current >= 0) {
        if ((uint64_t)current >= data->texture_count) {
            free(visited); qa_error_set(error, QA_ERROR_FORMAT, (size_t)current, "Q2 texture animation index outside table"); return false;
        }
        if (visited[current]) break;
        visited[current] = true;
        ++legacy->frame_count;
        current = data->textures[current].next;
    }
    free(visited);
    if (legacy->frame_count > SIZE_MAX / sizeof(*legacy->frames)) {
        qa_error_set(error, QA_ERROR_FORMAT, legacy->frame_count, "Q2 texture animation is too long"); return false;
    }
    legacy->frames = malloc(legacy->frame_count * sizeof(*legacy->frames));
    if (!legacy->frames) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate Q2 texture animation"); return false; }
    current = (int64_t)legacy->texture;
    for (size_t i = 0; i < legacy->frame_count; ++i) {
        legacy->frames[i] = (size_t)current;
        current = data->textures[current].next;
    }
    return true;
}

bool qaw_build_legacy(qa_scene_world *world, qa_error *error)
{
    if (!isfinite(world->options.q1_water_alpha) || !isfinite(world->options.q2_light_modulate)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Nonfinite legacy world material option"); return false;
    }
    qawl_world *data = calloc(1, sizeof(*data));
    if (!data) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate legacy world"); return false; }
    world->legacy_data = data;
    if (world->leaf_count > SIZE_MAX / sizeof(*data->point_lights)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Point lighting leaf table exceeds address space"); return false;
    }
    if (world->leaf_count) {
        data->point_lights = calloc(world->leaf_count, sizeof(*data->point_lights));
        if (!data->point_lights) {
            qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate point lighting leaf table"); return false;
        }
        data->point_light_count = world->leaf_count;
    }
    bool q1 = world->bsp.family == QA_BSP_Q1;
    if (q1 && !qa_bsp_read_q1_metadata(&world->bsp, &data->metadata, error)) return false;
    if (!qawl_textures_build(world, error)) return false;
    for (size_t i = 0; i < world->surface_count; ++i) {
        qaw_surface *surface = &world->surfaces[i];
        qa_bsp_face face;
        qa_bsp_texinfo info;
        if (!qa_bsp_read_face(&world->bsp, i, &face, error) ||
            !qa_bsp_read_texinfo(&world->bsp, face.texinfo, &info, error)) return false;
        if (face.plane >= world->plane_count || (q1 && info.texture < 0)) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Invalid brush surface references"); return false;
        }
        size_t texture_index = q1 ? (size_t)info.texture : face.texinfo;
        if (texture_index >= data->texture_count) {
            qa_error_set(error, QA_ERROR_FORMAT, i, "Brush face texture is outside table"); return false;
        }
        surface->legacy = calloc(1, sizeof(*surface->legacy));
        if (!surface->legacy) { qa_error_set(error, QA_ERROR_MEMORY, i, "Cannot allocate brush surface state"); return false; }
        qaw_legacy *legacy = surface->legacy;
        legacy->texture = texture_index;
        const qawl_texture *texture = &data->textures[texture_index];
        const char *name = texture->name;
        legacy->warp = q1 ? name[0] == '*' && strcmp(name, "*missing") != 0 : (info.flags & 8) != 0;
        legacy->flowing = !q1 && (info.flags & 64) != 0;
        legacy->fence = q1 ? name[0] == '{' :
            ((uint32_t)info.flags & 0x02000000u) != 0 && ((uint32_t)info.flags & (16u | 32u)) == 0;
        legacy->alpha = q1 ? legacy->warp ? world->options.q1_water_alpha : 1 :
            (info.flags & 16) ? 0.33f : (info.flags & 32) ? 0.66f : 1;
        surface->sky = q1 ? !strncmp(name, "sky", 3) : (info.flags & 4) != 0;
        surface->skip = !q1 && (info.flags & 128) != 0 && !surface->sky;
        surface->source_index = (uint32_t)i;
        surface->type = QA_BSP_SURFACE_PLANAR;
        surface->has_plane = true;
        surface->plane = (qa_scene_plane){world->planes[face.plane].normal, world->planes[face.plane].distance};
        if (face.draw_flags & 1) {
            surface->plane.normal = qa_vec_scale(surface->plane.normal, -1);
            surface->plane.distance = -surface->plane.distance;
        }
        if (!q1 && !q2_frames(data, legacy, error)) return false;
        if (!qawl_geometry_build(world, surface, &face, &info, error)) return false;
        size_t size = strlen(name) + 10;
        char *shader_name = malloc(size);
        if (!shader_name) { qa_error_set(error, QA_ERROR_MEMORY, i, "Cannot allocate brush shader name"); return false; }
        snprintf(shader_name, size, "textures/%s", name);
        if (qa_material_has_authored(world->materials, shader_name)) {
            qa_scene_image_options options = world->options.images;
            options.family = q1 ? QA_GAME_Q1 : QA_GAME_Q2;
            if (legacy->lightmapped && i > INT32_MAX) {
                free(shader_name); qa_error_set(error, QA_ERROR_FORMAT, i, "Authored brush lightmap index exceeds material range"); return false;
            }
            if (!qa_material_register_world(world->materials, shader_name, &options, world->identity,
                legacy->lightmapped ? (int32_t)i : -1, legacy->lightmapped ? QA_MATERIAL_LIGHTMAP : QA_MATERIAL_DYNAMIC,
                texture->name, texture->image, &surface->material, error)) {
                free(shader_name); return false;
            }
            surface->skip = false;
        }
        free(shader_name);
        surface->base_material = surface->material;
        surface->sort = surface->material ? surface->material->sort : surface->sky ? 2 : legacy->alpha < 1 ? 9 : 3;
    }
    if (q1) for (size_t i = 0; i < world->leaf_count; ++i) {
        const qa_bsp_leaf *leaf = &world->leaves[i];
        if (leaf->contents == -1) continue;
        for (size_t j = leaf->faces.first; j < (size_t)leaf->faces.first + leaf->faces.count; ++j)
            world->surfaces[world->leaf_surfaces[j]].legacy->underwater = true;
    }
    return true;
}

static bool material_name(const char *source, char output[1024], qa_error *error)
{
    if (!source) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "World material name is absent"); return false; }
    size_t count = strcspn(source, ".");
    if (count == 0 || count >= 1024) {
        qa_error_set(error, QA_ERROR_ARGUMENT, count, "Invalid world material name length"); return false;
    }
    for (size_t i = 0; i < count; ++i) {
        unsigned char c = (unsigned char)source[i];
        output[i] = (char)(c >= 'A' && c <= 'Z' ? c + ('a' - 'A') : c);
    }
    output[count] = '\0';
    return true;
}

bool qa_scene_world_remap(qa_scene_world *world, const char *original, const char *replacement,
                           float time_offset, qa_error *error)
{
    if (!world || !isfinite(time_offset)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Invalid world material remap"); return false;
    }
    if (world->revision == UINT64_MAX) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "World material revision space exhausted"); return false;
    }
    char from[1024], to[1024];
    if (!material_name(original, from, error) || !material_name(replacement, to, error)) return false;
    if (world->bsp.family == QA_BSP_Q3) {
        if (!qa_material_remap(world->materials, original, replacement, time_offset, error)) return false;
        ++world->revision;
        return true;
    }
    bool reset = !strcmp(from, to);
    qawl_world *data = world->legacy_data;
    typedef struct prepared_remap { qaw_surface *surface; const qa_material *material; } prepared_remap;
    if (world->surface_count > SIZE_MAX / sizeof(prepared_remap)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "World material remap list exceeds address space"); return false;
    }
    prepared_remap *pending = world->surface_count ? malloc(world->surface_count * sizeof(*pending)) : NULL;
    if (world->surface_count && !pending) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "Cannot allocate world material remap list"); return false;
    }
    size_t count = 0;
    for (size_t i = 0; i < world->surface_count; ++i) {
        qaw_surface *surface = &world->surfaces[i];
        if (surface->base_material) continue;
        const char *texture = data->textures[surface->legacy->texture].name;
        char path[1024], key[1024];
        int written = snprintf(path, sizeof(path), "textures/%s", texture);
        if (written < 0 || (size_t)written >= sizeof(path) || !material_name(path, key, error)) {
            free(pending); return false;
        }
        if (strcmp(from, key)) continue;
        const qa_material *material = NULL;
        if (!reset) {
            bool lightmapped = surface->legacy->lightmapped;
            if (lightmapped && i > INT32_MAX) {
                free(pending); qa_error_set(error, QA_ERROR_FORMAT, i, "Remapped brush lightmap index exceeds material range"); return false;
            }
            qa_scene_image_options options = world->options.images;
            options.family = world->bsp.family == QA_BSP_Q1 ? QA_GAME_Q1 : QA_GAME_Q2;
            if (!qa_material_register_world(world->materials, replacement, &options, world->identity,
                lightmapped ? (int32_t)i : -1, lightmapped ? QA_MATERIAL_LIGHTMAP : QA_MATERIAL_DYNAMIC,
                texture, data->textures[surface->legacy->texture].image, &material, error)) { free(pending); return false; }
            if (material->default_shader) {
                free(pending); qa_error_set(error, QA_ERROR_NOT_FOUND, i, "World remap target defaulted"); return false;
            }
        }
        pending[count++] = (prepared_remap){surface, material};
    }
    if (!qa_material_remap(world->materials, original, replacement, time_offset, error)) { free(pending); return false; }
    for (size_t i = 0; i < count; ++i) {
        qaw_surface *surface = pending[i].surface;
        surface->material = pending[i].material;
        surface->material_time_offset = reset ? 0 : time_offset;
        surface->sort = surface->material ? surface->material->sort : surface->sky ? 2 : surface->legacy->alpha < 1 ? 9 : 3;
    }
    free(pending);
    ++world->revision;
    return true;
}

void qaw_destroy_legacy(qa_scene_world *world)
{
    if (!world) return;
    for (size_t i = 0; world->surfaces && i < world->surface_count; ++i) {
        qaw_legacy *legacy = world->surfaces[i].legacy;
        if (!legacy) continue;
        qawl_light_destroy(legacy);
        free(legacy->frames);
        free(legacy);
        world->surfaces[i].legacy = NULL;
    }
    qawl_world *data = world->legacy_data;
    if (data) {
        qawl_light_atlases_destroy(data);
        qawl_textures_destroy(data);
        free(data->q1_styles); free(data->q2_styles);
        free(data->point_lights);
        free(data);
    }
    world->legacy_data = NULL;
}

bool qaw_legacy_casts_shadow(const qa_scene_world *world, const qaw_surface *surface, bool entity)
{
    if (world->bsp.family == QA_BSP_Q1)
        return !surface->sky && !surface->legacy->warp && surface->legacy->alpha >= 1;
    qa_bsp_face face;
    qa_bsp_texinfo info;
    if (!qa_bsp_read_face(&world->bsp, surface->source_index, &face, NULL) ||
        !qa_bsp_read_texinfo(&world->bsp, face.texinfo, &info, NULL)) return false;
    int32_t excluded = 4 | 8 | 128;
    if (entity) excluded |= 16 | 32;
    return (info.flags & excluded) == 0;
}

static int32_t legacy_integer(double value)
{
    return value >= INT32_MIN && value < 2147483648.0 ? (int32_t)value : INT32_MIN;
}

static qawl_texture *animated_texture(qa_scene_world *world, const qaw_legacy *legacy,
                                      const qa_scene_world_input *input)
{
    qawl_world *data = world->legacy_data;
    qawl_texture *texture = &data->textures[legacy->texture];
    if (world->bsp.family == QA_BSP_Q2) {
        int32_t frame;
        if (input->use_animation_frame) memcpy(&frame,&input->animation_frame,sizeof(frame));
        else frame = qa_source_float_to_i32((float)input->seconds * 2.0f);
        int64_t phase = (int64_t)frame % (int64_t)legacy->frame_count;
        if (phase < 0) phase += (int64_t)legacy->frame_count;
        return &data->textures[legacy->frames[(size_t)phase]];
    }
    unsigned cycle = texture->name[0] == '+' && toupper((unsigned char)texture->name[1]) >= 'A' ? 1u : 0u;
    if (input->alternate_animation && texture->animation_count[cycle ^ 1]) cycle ^= 1;
    size_t count = texture->animation_count[cycle];
    if (!count) return texture;
    int32_t phase = legacy_integer(input->seconds * 10) % (int32_t)(count * 2);
    if (phase < 0) phase += (int32_t)(count * 2);
    return &data->textures[texture->animation[cycle][(size_t)phase / 2]];
}

static float turbulence(double phase, bool q1)
{
    static const float quarter[65] = {
        0, .19633f, .392541f, .588517f, .784137f, .979285f, 1.17384f, 1.3677f,
        1.56072f, 1.75281f, 1.94384f, 2.1337f, 2.32228f, 2.50945f, 2.69512f, 2.87916f,
        3.06147f, 3.24193f, 3.42044f, 3.59689f, 3.77117f, 3.94319f, 4.11282f, 4.27998f,
        4.44456f, 4.60647f, 4.76559f, 4.92185f, 5.07515f, 5.22538f, 5.37247f, 5.51632f,
        5.65685f, 5.79398f, 5.92761f, 6.05767f, 6.18408f, 6.30677f, 6.42566f, 6.54068f,
        6.65176f, 6.75883f, 6.86183f, 6.9607f, 7.05537f, 7.14579f, 7.23191f, 7.31368f,
        7.39104f, 7.46394f, 7.53235f, 7.59623f, 7.65552f, 7.71021f, 7.76025f, 7.80562f,
        7.84628f, 7.88222f, 7.91341f, 7.93984f, 7.96148f, 7.97832f, 7.99036f, 7.99759f, 8
    };
    unsigned index = (unsigned)(uint32_t)legacy_integer(phase * (256.0 / (2.0 * 3.14159265358979323846))) & 255u;
    unsigned quadrant = index / 64, offset = index % 64;
    float value = quarter[quadrant & 1 ? 64 - offset : offset];
    if (quadrant >= 2) value = -value;
    if (index == 128) value = 9.79717e-16f;
    return q1 ? value : value * 0.5f;
}

static bool transient_mesh(const qaw_surface *surface, qa_scene_frame *frame,
                            qa_scene_mesh *mesh, qa_scene_vertex **vertices, qa_error *error)
{
    *mesh = surface->mesh;
    *vertices = qa_arena_alloc(&frame->storage, mesh->vertex_count * sizeof(**vertices), _Alignof(qa_scene_vertex), error);
    if (!*vertices) return false;
    memcpy(*vertices, mesh->vertices, mesh->vertex_count * sizeof(**vertices));
    mesh->vertices = *vertices;
    mesh->identity = 0;
    mesh->revision = 0;
    return true;
}

static void draw_state(qa_scene_draw *draw, const qa_material_context *context, const qa_scene_mesh *mesh)
{
    *draw = (qa_scene_draw){.mesh = *mesh, .model = context->model, .texture_count = 1,
        .environment = QA_TEXTURE_MODULATE, .lighting = QA_LIGHT_VERTEX,
        .entity = context->entity, .light_mask = context->light_mask, .fog = context->fog};
    draw->mvp = qa_scene_matrix_multiply(context->view.projection,
        qa_scene_matrix_multiply(qa_scene_view_matrix(&context->view), context->model));
    qa_scene_state_default(&draw->state);
    const float *m = context->model.m;
    qa_vec3 x = qa_v3(m[0], m[1], m[2]), y = qa_v3(m[4], m[5], m[6]), z = qa_v3(m[8], m[9], m[10]);
    bool reflected = qa_vec_dot(x, qa_vec_cross(y, z)) < 0;
    draw->state.cull = context->mirror != reflected ? QA_CULL_BACK : QA_CULL_FRONT;
}

static bool recipient_image(const qa_material_context *context, const qa_scene_image **image,
    bool mipmap, qa_error *error)
{
    if (!context->source_recipient_image) return true;
    const qa_scene_image *mapped = NULL;
    if (!*image || !context->source_recipient_image(context->source_recipient_context, *image,
        mipmap, mipmap, &mapped, error)) return false;
    if (!mapped) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Source world recipient lost its reached texture");
        return false;
    }
    *image = mapped;
    return true;
}

static bool sky_submit(qa_scene_world *world, const qaw_surface *surface,
                        const qa_material_context *context, const qa_scene_world_input *input,
                        qa_scene_frame *frame, qa_error *error)
{
    qawl_world *data = world->legacy_data;
    const qawl_texture *texture = &data->textures[surface->legacy->texture];
    if (world->bsp.family == QA_BSP_Q1 && input->q1_sky)
        return qaw_q1_sky_collect(input->q1_sky, &surface->mesh, context, frame, error);
    if (world->bsp.family == QA_BSP_Q2 || input->override_sky || world->options.q2_sky) {
        if (input->boxed_sky && !context->source_primitives &&
            !context->source_scratch && !frame->source_pending)
            return qaw_boxed_sky_collect(input->boxed_sky, &surface->mesh, context, frame, error);
        qa_scene_mesh mesh;
        qa_scene_vertex *vertices;
        if (!transient_mesh(surface, frame, &mesh, &vertices, error)) return false;
        for (size_t i = 0; i < mesh.vertex_count; ++i) {
            qa_vec4 p = qa_scene_matrix_point(context->model, vertices[i].position);
            vertices[i].position = qa_v3(p.x, p.y, p.z);
        }
        qa_scene_sky_bounds bounds[6];
        qa_scene_sky_bounds_reset(bounds);
        if (!qa_scene_sky_clip(&mesh, 1, input->view.origin, bounds, error)) return false;
        const qa_scene_image *images[6];
        for (size_t i = 0; i < 6; ++i) {
            images[i] = input->override_sky ? input->sky_images[i] : data->sky[i];
            if (images[i] && !recipient_image(context, images + i, false, error)) return false;
        }
        float angle = input->sky_auto_rotate ? (float)(input->seconds * input->sky_rotation) : input->sky_rotation;
        size_t first = frame->command_count;
        if (!qa_scene_q2_sky(frame, &input->view, images, bounds, angle, input->sky_axis,
                             input->sky_rotation != 0, (qa_vec4){1,1,1,1}, error)) return false;
        if (input->fog.kind == QA_FOG_EXP2 && input->fog.density > 0)
            for (size_t i = first; i < frame->command_count; ++i) if (frame->commands[i].kind == QA_SCENE_COMMAND_DRAW)
                frame->commands[i].data.draw.fog = (qa_scene_fog){.kind = QA_FOG_CONSTANT, .effect = QA_FOG_COLOR,
                    .color = input->fog.color, .amount = input->fog.sky_factor};
        return true;
    }
    for (size_t layer = 0; layer < 2; ++layer) {
        qa_scene_mesh mesh;
        qa_scene_vertex *vertices;
        if (!transient_mesh(surface, frame, &mesh, &vertices, error)) return false;
        double scroll = input->seconds * (layer ? 16 : 8);
        scroll -= trunc(scroll / 128) * 128;
        for (size_t i = 0; i < mesh.vertex_count; ++i) {
            qa_vec3 p = qa_vec_sub(vertices[i].position, context->local_view_origin);
            p.z *= 3;
            float length = qa_vec_length(p), scale = length > 0 ? 378 / length : 0;
            vertices[i].texcoord = (qa_vec2){(float)(scroll + p.x * scale) / 128,
                                                  (float)(scroll + p.y * scale) / 128};
            vertices[i].color = (qa_vec4){1,1,1,1};
        }
        qa_scene_draw draw;
        draw_state(&draw, context, &mesh);
        draw.textures[0] = texture->sky[layer] ? texture->sky[layer] : texture->image;
        if (!recipient_image(context, &draw.textures[0], false, error)) return false;
        draw.state.cull = QA_CULL_NONE;
        draw.state.depth_write = true;
        draw.state.blend_source = layer ? QA_BLEND_SRC_ALPHA : QA_BLEND_ONE;
        draw.state.blend_destination = layer ? QA_BLEND_ONE_MINUS_SRC_ALPHA : QA_BLEND_ZERO;
        draw.fog = input->fog.kind == QA_FOG_EXP2 && input->fog.density > 0
            ? (qa_scene_fog){.kind = QA_FOG_CONSTANT, .effect = QA_FOG_COLOR,
                .color = input->fog.color, .amount = input->fog.sky_factor}
            : (qa_scene_fog){0};
        if (!qa_scene_frame_draw(frame, &draw, error)) return false;
    }
    return true;
}

static bool fragment_lights(qa_scene_world *world, const qa_scene_world_input *input,
                             qa_scene_frame *frame, qa_scene_draw *draw, qa_error *error)
{
    if (input->legacy_flashblend || !input->shadow_lights ||
        (input->legacy_policy.present && (input->legacy_policy.fullbright || !input->legacy_policy.dynamic))) return true;
    qa_scene_shadow_light *lights = NULL;
    if (input->shadow_light_count) {
        if (input->shadow_light_count > SIZE_MAX / sizeof(*lights)) {
            qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Fragment light count exceeds address space"); return false;
        }
        lights = qa_arena_alloc(&frame->storage, input->shadow_light_count * sizeof(*lights), _Alignof(qa_scene_shadow_light), error);
        if (!lights) return false;
    }
    size_t count = 0;
    for (size_t i = 0; i < input->shadow_light_count; ++i) {
        const qa_scene_shadow_light *source = &input->shadow_lights[i];
        if (world->bsp.family == QA_BSP_Q1 && !source->light.spot && !source->light.casts_shadow) continue;
        lights[count] = *source;
        lights[count++].light.scale *= input->legacy_policy.present ?
            input->legacy_policy.modulate : world->options.q2_light_modulate;
    }
    draw->lighting = QA_LIGHT_Q2_WORLD;
    draw->lights = lights;
    draw->light_count = count;
    draw->shadow_atlas = input->shadow_atlas;
    draw->shadow_near = 4;
    return true;
}

bool qaw_submit_legacy(qa_scene_world *world, qaw_surface *surface, const qa_material_context *context,
                       const qa_scene_world_input *input, qa_scene_frame *frame, qa_error *error)
{
    if (!isfinite(input->seconds * 64)) {
        qa_error_set(error, QA_ERROR_ARGUMENT, 0, "Legacy texture animation time overflows"); return false;
    }
    qaw_legacy *legacy = surface->legacy;
    if (!surface->material) {
        if (surface->skip) return true;
        if (!legacy->underwater &&
            qa_vec_dot(context->local_view_origin, surface->plane.normal) - surface->plane.distance < -0.01f) return true;
        if (surface->sky) return sky_submit(world, surface, context, input, frame, error);
    }
    if (surface->material) {
        qa_material_context selected = *context;
        if (input->legacy_flashblend || (input->legacy_policy.present &&
            (input->legacy_policy.fullbright || !input->legacy_policy.dynamic))) {
            selected.light_count = 0;
            selected.fragment_lighting = false;
            selected.fragment_light_count = 0;
        }
        selected.lightmap = (world->bsp.family == QA_BSP_Q2 && legacy->alpha < 1) ||
            (input->legacy_policy.present && input->legacy_policy.fullbright) ? NULL : surface->lightmap;
        const qa_material *effective = surface->material->remapped ?
            surface->material->remapped : surface->material;
        if ((effective->surface_flags & 128u) != 0) return true;
        if (effective->sky)
            return qaw_submit_material_sky(world, surface->material, effective, &surface->mesh, &selected, input, frame, error);
        return qa_material_submit(surface->material, &surface->mesh, &selected, frame, error);
    }
    bool q1 = world->bsp.family == QA_BSP_Q1;
    qawl_texture *texture = animated_texture(world, legacy, input);
    const qa_scene_image *base_image = qa_scene_image_at_time(texture->image, input->seconds);
    if (!recipient_image(context, &base_image, world->options.images.mipmap, error)) return false;
    float alpha = legacy->alpha * context->entity_color.w;
    bool lightmapped = surface->lightmap && (q1 || legacy->alpha >= 1) &&
        !(input->legacy_policy.present && input->legacy_policy.fullbright);
    bool diagnostic = lightmapped && input->legacy_policy.present && input->legacy_policy.lightmap;
    bool blended = alpha < 1, paired = lightmapped && !diagnostic &&
        !(input->legacy_policy.present && (input->legacy_policy.saturate ||
            (!q1 && input->legacy_policy.monolightmap != '0'))) &&
        (blended || (input->fog.kind == QA_FOG_EXP2 && input->fog.density > 0));
    float intensity = !q1 && (legacy->warp || blended) ? 0.5f : 1;
    qa_scene_mesh mesh = surface->mesh;
    qa_scene_vertex *vertices = NULL;
    if (!diagnostic && (legacy->warp || legacy->flowing) &&
        !transient_mesh(surface, frame, &mesh, &vertices, error)) return false;
    double seconds = q1 ? input->seconds : (double)(float)input->seconds;
    for (size_t i = 0; vertices && i < mesh.vertex_count; ++i) {
        qa_vec2 uv = vertices[i].texcoord;
        if (legacy->warp) {
            float scroll = !q1 && legacy->flowing ?
                (float)(-64 * (seconds * 0.5 - legacy_integer(seconds * 0.5))) : 0;
            vertices[i].texcoord = (qa_vec2){(float)((uv.x + turbulence(uv.y * 0.125 + seconds, q1) + scroll) / 64),
                                                  (float)((uv.y + turbulence(uv.x * 0.125 + seconds, q1)) / 64)};
        } else if (legacy->flowing) {
            float scroll = (float)(-64 * (seconds / 40 - legacy_integer(seconds / 40)));
            vertices[i].texcoord.x += scroll == 0 ? -64 : scroll;
        }
    }
    qa_scene_draw draw;
    draw_state(&draw, context, &mesh);
    draw.single_coverage = !legacy->warp && !legacy->flowing;
    draw.vertex_inputs = (qa_scene_vertex_inputs){.constant_color = true,
        .color = {context->entity_color.x * intensity, context->entity_color.y * intensity,
                  context->entity_color.z * intensity, alpha}};
    draw.textures[0] = base_image;
    draw.state.depth_write = !blended;
    draw.state.alpha_test = legacy->fence ? QA_ALPHA_GT666 : QA_ALPHA_NONE;
    draw.state.blend_source = blended ? QA_BLEND_SRC_ALPHA : QA_BLEND_ONE;
    draw.state.blend_destination = blended ? QA_BLEND_ONE_MINUS_SRC_ALPHA : QA_BLEND_ZERO;
    if (diagnostic) {
        draw.textures[0] = surface->lightmap;
        draw.state.blend_source = QA_BLEND_ONE;
        draw.state.blend_destination = QA_BLEND_ZERO;
        draw.vertex_inputs.color = (qa_vec4){1, 1, 1, 1};
        draw.vertex_inputs.swap_uv = true;
    }
    if ((!lightmapped || paired) && !diagnostic)
        if (!fragment_lights(world, input, frame, &draw, error)) return false;
    if (paired) {
        draw.textures[1] = legacy->direct_lightmap;
        draw.texture_count = 2;
        if (input->shadow_lights) {
            draw.textures[0] = legacy->direct_lightmap;
            draw.textures[1] = base_image;
            draw.vertex_inputs.swap_uv = true;
        }
    }
    if (legacy->brush.polygon_vertices && !texture->fullbright && !diagnostic && !blended &&
        (!paired || draw.lighting == QA_LIGHT_VERTEX) && !draw.light_count &&
        !draw.shadow_atlas && !draw.vertex_inputs.swap_uv) {
        draw.brush = legacy->brush;
        draw.brush.present = !lightmapped || paired;
    }
    if (!qa_scene_frame_draw(frame, &draw, error)) return false;
    if (lightmapped && !paired && !diagnostic) {
        qa_scene_draw light_draw = draw;
        light_draw.brush = (qa_scene_brush_surface){0};
        light_draw.mesh = surface->mesh;
        light_draw.vertex_inputs = (qa_scene_vertex_inputs){.constant_color = true,
            .swap_uv = true, .color = {1, 1, 1, 1}};
        light_draw.textures[0] = q1 && input->shadow_lights ? legacy->direct_lightmap : surface->lightmap;
        bool inverted = q1 && !input->shadow_lights && world->options.q1_lightmap_encoding != QA_Q1_LIGHTMAP_RGB;
        light_draw.state.blend_source = inverted ? QA_BLEND_ZERO : QA_BLEND_DST_COLOR;
        light_draw.state.blend_destination = !inverted ? QA_BLEND_ZERO :
            world->options.q1_lightmap_encoding == QA_Q1_LIGHTMAP_INVERTED_ALPHA ? QA_BLEND_ONE_MINUS_SRC_ALPHA : QA_BLEND_ONE_MINUS_SRC_COLOR;
        if (!q1 && input->legacy_policy.present) {
            uint8_t mono = input->legacy_policy.monolightmap;
            if (mono >= 'a' && mono <= 'z') mono -= 'a' - 'A';
            if (input->legacy_policy.saturate) {
                light_draw.state.blend_source = QA_BLEND_ONE;
                light_draw.state.blend_destination = QA_BLEND_ONE;
            } else if (mono != '0' && mono != 'L' && mono != 'I') {
                light_draw.state.blend_source = QA_BLEND_SRC_ALPHA;
                light_draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
            }
        }
        light_draw.state.depth_test = QA_DEPTH_EQUAL;
        light_draw.state.depth_write = false;
        light_draw.state.alpha_test = QA_ALPHA_NONE;
        light_draw.fog = (qa_scene_fog){0};
        light_draw.light_pass = QA_LIGHT_PASS_LIGHTMAP;
        if (!fragment_lights(world, input, frame, &light_draw, error) || !qa_scene_frame_draw(frame, &light_draw, error)) return false;
    }
    if (texture->fullbright && !diagnostic) {
        qa_scene_draw bright_draw = draw;
        bright_draw.brush = (qa_scene_brush_surface){0};
        bright_draw.mesh = surface->mesh;
        bright_draw.vertex_inputs = (qa_scene_vertex_inputs){.constant_color = true,
            .color = {context->entity_color.x, context->entity_color.y, context->entity_color.z, alpha}};
        bright_draw.textures[0] = qa_scene_image_at_time(texture->fullbright, input->seconds);
        if (!recipient_image(context, &bright_draw.textures[0], world->options.images.mipmap, error)) return false;
        bright_draw.textures[1] = NULL;
        bright_draw.texture_count = 1;
        bright_draw.lighting = QA_LIGHT_VERTEX;
        bright_draw.lights = NULL; bright_draw.light_count = 0; bright_draw.shadow_atlas = NULL;
        bright_draw.state.blend_source = QA_BLEND_SRC_ALPHA;
        bright_draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
        bright_draw.state.depth_test = blended ? QA_DEPTH_LEQUAL : QA_DEPTH_EQUAL;
        bright_draw.state.depth_write = false;
        bright_draw.state.alpha_test = legacy->fence ? QA_ALPHA_GT666 : QA_ALPHA_GT0;
        if (!qa_scene_frame_draw(frame, &bright_draw, error)) return false;
    }
    return true;
}

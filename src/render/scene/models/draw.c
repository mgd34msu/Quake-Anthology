#include "internal.h"
#include "qa/scene_effects.h"

static qa_scene_cull model_cull(const qa_scene_model *model, const qa_scene_model_input *input) {
    qa_model_format format = model->source->format;
    if (format == QA_MODEL_SPR || format == QA_MODEL_SP2) return QA_CULL_NONE;
    return format == QA_MODEL_MDL || format == QA_MODEL_MD2 ||
        (format == QA_MODEL_MD5 && input->replacement) ? QA_CULL_FRONT : QA_CULL_BACK;
}

static void depth_and_mirror(qa_scene_draw *draw, const qa_scene_model_input *input,
                             bool material_mirror) {
    if (!material_mirror && input->view.mirror && input->family != QA_SCENE_Q3) {
        if (draw->state.cull == QA_CULL_FRONT) draw->state.cull = QA_CULL_BACK;
        else if (draw->state.cull == QA_CULL_BACK) draw->state.cull = QA_CULL_FRONT;
    }
    bool hack = input->family == QA_SCENE_Q1 ? input->view_model :
        (input->flags & (input->family == QA_SCENE_Q2 ? 16u : 8u)) != 0;
    if (hack) { draw->state.depth_near = 0; draw->state.depth_far = 0.3f; }
    if (input->family == QA_SCENE_Q2 && (input->flags & 4) && input->left_hand == 1) {
        for (unsigned column = 0; column < 4; ++column) draw->mvp.m[column * 4] = -draw->mvp.m[column * 4];
        if (draw->state.cull == QA_CULL_FRONT) draw->state.cull = QA_CULL_BACK;
        else if (draw->state.cull == QA_CULL_BACK) draw->state.cull = QA_CULL_FRONT;
    }
}

static qa_material_context material_context(const qa_scene_model_input *input, bool world) {
    qa_material_context context = {0};
    context.view = input->view;
    context.source_primitives = input->source_order;
    context.source_scratch = input->source_scratch;
    context.source_depth_hack = input->family == QA_SCENE_Q3 && (input->flags & 8u) != 0;
    if (world) qa_scene_matrix_identity(&context.model);
    else context.model = qa_scene_model_matrix(&input->transform);
    context.entity_color = input->color;
    context.ambient = input->ambient;
    context.ambient_alpha = 1;
    context.directed = input->directed;
    context.light_direction = input->light_direction;
    context.identity_light = input->identity_light;
    context.seconds = input->seconds;
    double milliseconds = input->seconds * 1000;
    context.milliseconds = input->has_milliseconds ? input->milliseconds :
        milliseconds <= (double)INT64_MIN ? INT64_MIN :
        milliseconds >= (double)INT64_MAX ? INT64_MAX : (int64_t)milliseconds;
    if (context.source_primitives) context.seconds = (float)context.milliseconds * .001f;
    context.time_offset = input->shader_time;
    context.source_diagnostics = input->source_diagnostics;
    context.source_diagnostics_read = input->source_diagnostics_read;
    context.source_diagnostics_context = input->source_diagnostics_context;
    context.entity_texcoord = input->shader_texcoord;
    context.texts = input->render_texts; context.text_count = input->render_text_count;
    context.video_frame = input->video_frame; context.video_context = input->video_context;
    context.shadow_plane = input->shadow_plane;
    context.entity = input->entity;
    context.fog_index = input->fog_index;
    context.fog_tc_scale = input->fog_tc_scale;
    context.fog_has_surface = input->fog_has_surface;
    context.fog_surface = input->fog_surface;
    context.mirror = input->view.mirror;
    context.non_normalized_axis = input->family == QA_SCENE_Q3 && input->non_normalized_axis;
    context.projection_shadow = input->family == QA_SCENE_Q3 && (input->flags & 256) != 0;
    context.fog = input->fog;
    context.fog_volume_color = input->fog.color;
    qa_vec3 delta = qa_vec_sub(input->view.origin, model_origin(input));
    if (world) context.local_view_origin = input->view.origin;
    else {
        qa_vec3 axes[3];
        for (unsigned i = 0; i < 3; ++i)
            axes[i] = qa_vec_scale(model_vec(input->transform.axes[i]), input->transform.scale[i]);
        float length = qa_vec_length(axes[0]);
        float scale = input->family == QA_SCENE_Q3 && input->non_normalized_axis ? (length ? 1 / length : 0) : 1;
        context.local_view_origin = qa_v3(qa_vec_dot(delta, axes[0]) * scale,
            qa_vec_dot(delta, axes[1]) * scale, qa_vec_dot(delta, axes[2]) * scale);
    }
    return context;
}

static bool q3_model_shadow(qa_scene_model *model, const qa_scene_model_input *input,
                             const qa_scene_mesh *mesh, const qa_material *material,
                             qa_material_context *context, qa_scene_frame *frame, qa_error *error) {
    if (input->family != QA_SCENE_Q3 || input->shadow_only || input->fog_index != 0 ||
        (input->shadow_mode != 2 && input->shadow_mode != 3)) return true;
    if (material->sort != 3) return true;
    size_t first = frame->command_count;
    const qa_material *shadow = NULL;
    qa_material_library *materials = input->material_library ? input->material_library : model->materials;
    if (input->shadow_mode == 2) {
        if (((input->flags & 2u) && !input->view.clip_enabled) || (input->flags & (8u | 64u))) return true;
        shadow = qa_material_find(materials, "<stencil shadow>");
        if (!shadow) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "model library has no canonical stencil shadow material"); return false; }
        if (context->source_scratch) return qa_material_submit(shadow, mesh, context, frame, error);
        if (!qa_scene_stencil_shadow(frame, &input->view, mesh, context->model,
                                      input->light_direction, qa_scene_white(model->resources), error)) return false;
    } else {
        if (!(input->flags & 256u)) return true;
        if (!qa_material_register_kind(materials, "projectionShadow", &model->options,
                                         QA_MATERIAL_DYNAMIC, &shadow, error)) return false;
        context->projection_shadow = true;
        if (!qa_material_submit(shadow, mesh, context, frame, error)) return false;
    }
    return qa_scene_frame_group(frame, first, input->source_order ? QA_SCENE_GROUP_SOURCE : QA_SCENE_GROUP_COMPILED, shadow, shadow->sort,
                                 input->entity, 0, 0, error);
}

static float light_fraction(float value, float channel) {
    return channel > 0 && value > 0 ? fminf(1, value / channel) : 0;
}

static bool apply_shadow_lights(qa_scene_draw *draw, const qa_scene_model_input *input,
                                 qa_scene_frame *frame, qa_error *error) {
    if (!input->shadow_light_count || !input->shadow_lights || input->family != QA_SCENE_Q2 ||
        input->view_model || (input->flags & (4u | 8u | 16u | 1024u | 2048u | 4096u | 65536u | 131072u)) ||
        (input->infrared && (input->flags & 32768u))) return true;
    if (input->shadow_light_count > SIZE_MAX / sizeof(qa_scene_shadow_light)) {
        qa_error_set(error, QA_ERROR_MEMORY, 0, "model shadow light span is too large"); return false;
    }
    qa_scene_shadow_light *lights = qa_arena_alloc(&frame->storage,
        input->shadow_light_count * sizeof(*lights), _Alignof(qa_scene_shadow_light), error);
    if (!lights) return false;
    qa_vec3 shade = scene_model_alias_light(input);
    bool cones = false, shadows = false;
    for (size_t i = 0; i < input->shadow_light_count; ++i) {
        lights[i] = input->shadow_lights[i];
        lights[i].model_fraction = qa_v3(0, 0, 0);
        if (lights[i].light.spot) { cones = true; continue; }
        if (!lights[i].shadow_valid || !input->shadow_atlas) continue;
        float amount = (lights[i].light.radius - qa_vec_length(qa_vec_sub(model_origin(input), lights[i].light.origin))) / 256;
        if (amount <= 0) continue;
        qa_vec3 color = qa_vec_scale(lights[i].light.color, amount);
        if (input->monochrome) {
            float channel = fmaxf(color.x, fmaxf(color.y, color.z)); color = qa_v3(channel, channel, channel);
        }
        lights[i].model_fraction = qa_v3(light_fraction(color.x, shade.x), light_fraction(color.y, shade.y), light_fraction(color.z, shade.z));
        if (lights[i].model_fraction.x > 0 || lights[i].model_fraction.y > 0 || lights[i].model_fraction.z > 0) shadows = true;
    }
    if (!cones && !shadows) return true;
    draw->shade_scale = shadows ? fmaxf(1, fmaxf(shade.x, fmaxf(shade.y, shade.z)) * 2) : 1;
    draw->model_shade_scale = shadows;
    draw->lighting = cones ? QA_LIGHT_Q2_WORLD : QA_LIGHT_Q2_MODEL_SHADOW;
    draw->light_pass = QA_LIGHT_PASS_MODEL;
    draw->lights = lights;
    draw->light_count = input->shadow_light_count;
    draw->shadow_atlas = input->shadow_atlas;
    if (cones) for (size_t i = 0; i < input->shadow_light_count; ++i) if (!lights[i].light.spot) {
        lights[i].light.color = qa_v3(0, 0, 0); lights[i].light.scale = 0;
    }
    if (draw->shade_scale != 1) {
        size_t bytes = draw->mesh.vertex_count * sizeof(qa_scene_vertex);
        qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage, bytes, _Alignof(qa_scene_vertex), error);
        if (!vertices) return false;
        memcpy(vertices, draw->mesh.vertices, bytes);
        for (size_t i = 0; i < draw->mesh.vertex_count; ++i) {
            vertices[i].color.x /= draw->shade_scale;
            vertices[i].color.y /= draw->shade_scale;
            vertices[i].color.z /= draw->shade_scale;
        }
        draw->mesh.vertices = vertices;
    }
    return true;
}

static bool planar_shadow(qa_scene_model *model, const qa_scene_model_input *input,
                           const qa_scene_mesh *mesh, qa_scene_frame *frame, qa_error *error) {
    if (!input->planar_shadow || input->view_model ||
        (input->family != QA_SCENE_Q1 && input->family != QA_SCENE_Q2) ||
        (input->family == QA_SCENE_Q2 && (input->flags & (4u | 32u))) ||
        (input->family == QA_SCENE_Q1 && model->source->format != QA_MODEL_MDL &&
         model->source->format != QA_MODEL_MD5) ||
        (input->family == QA_SCENE_Q2 && model->source->format != QA_MODEL_MD2 &&
         model->source->format != QA_MODEL_MD5)) return true;
    qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage,
        mesh->vertex_count * sizeof(*vertices), _Alignof(qa_scene_vertex), error);
    if (!vertices) return false;
    float yaw = atan2f(input->transform.axes[0][1], input->transform.axes[0][0]);
    qa_vec3 direction = qa_vec_normalize(qa_v3(cosf(-yaw), sinf(-yaw), 1));
    float height = input->transform.origin[2] - input->shadow_plane;
    qa_model_transform transform = input->transform;
    transform.scale[0] = transform.scale[1] = transform.scale[2] = 1;
    qa_scene_draw draw = {0};
    draw.mesh = *mesh;
    draw.mesh.identity = 0;
    draw.mesh.vertices = vertices;
    draw.mesh.bounds = model_bounds_empty();
    for (size_t i = 0; i < mesh->vertex_count; ++i) {
        qa_vec3 point = mesh->vertices[i].position;
        point.x *= input->transform.scale[0]; point.y *= input->transform.scale[1]; point.z *= input->transform.scale[2];
        float elevation = point.z + height;
        vertices[i] = (qa_scene_vertex){.position = qa_v3(point.x - direction.x * elevation,
            point.y - direction.y * elevation, -height + 1), .color = {0, 0, 0, 0.5f * input->color.w}};
        model_bounds_add(&draw.mesh.bounds, vertices[i].position);
    }
    draw.model = qa_scene_model_matrix(&transform);
    draw.mvp = qa_scene_matrix_multiply(input->view.projection,
        qa_scene_matrix_multiply(qa_scene_view_matrix(&input->view), draw.model));
    draw.textures[0] = qa_scene_white(model->resources); draw.texture_count = 1;
    draw.environment = QA_TEXTURE_MODULATE;
    qa_scene_state_default(&draw.state);
    draw.state.cull = model_cull(model, input);
    draw.state.blend_source = QA_BLEND_SRC_ALPHA;
    draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
    draw.entity = input->entity;
    draw.shade_scale = 1;
    depth_and_mirror(&draw, input, false);
    return qa_scene_frame_draw(frame, &draw, error);
}

bool scene_model_emit(qa_scene_model *model, const qa_scene_model_input *input,
                       const qa_scene_mesh *mesh, const scene_model_image *image,
                       bool unlit, bool world, qa_scene_frame *frame, qa_error *error) {
    const qa_scene_model_input *original = input;
    qa_scene_model_input eyes;
    qa_model_format format = model->source->format;
    if (format == QA_MODEL_MDL && input->family == QA_SCENE_Q1 && input->q1_double_eyes &&
        input->source_path && !strcmp(input->source_path, "progs/eyes.mdl")) {
        eyes = *input;
        for (unsigned axis = 0; axis < 3; ++axis) {
            float shift = -model->source->translation[axis] - (axis == 2 ? 30 : 0);
            for (unsigned component = 0; component < 3; ++component)
                eyes.transform.origin[component] += input->transform.axes[axis][component] *
                    input->transform.scale[axis] * shift;
            eyes.transform.scale[axis] *= 2;
        }
        input = &eyes;
    }
    bool shell_image = scene_model_has_shell(input) && (format == QA_MODEL_MD2 || format == QA_MODEL_MD5);
    bool custom_allowed = format != QA_MODEL_MDL && format != QA_MODEL_SPR && format != QA_MODEL_SP2;
    const qa_material *material = custom_allowed && input->custom_material ? input->custom_material : image ? image->material : NULL;
    if (shell_image) material = NULL;
    if (!material && input->family == QA_SCENE_Q3 && !shell_image &&
        format != QA_MODEL_MDL && format != QA_MODEL_SPR) {
        material = qa_material_find(input->material_library ? input->material_library : model->materials, "*default");
        if (!material) { qa_error_set(error, QA_ERROR_ARGUMENT, 0, "model library has no canonical default material"); return false; }
    }
    if (material) {
        qa_material_context context = material_context(input, world);
        context.source_white = qa_scene_white(model->resources);
        if (model->source->format == QA_MODEL_MD3 || model->source->format == QA_MODEL_MD4)
            context.source_writer = QA_SOURCE_WRITE_MODEL;
        if (input->shadow_only) {
            qa_scene_mesh shadow_mesh;
            if (!qa_material_shadow_mesh(material, mesh, &context, frame, &shadow_mesh, error)) return false;
            if (!shadow_mesh.index_count) return true;
            qa_scene_draw draw = {.mesh = shadow_mesh, .model = context.model,
                .texture_count = 1, .environment = QA_TEXTURE_MODULATE, .entity = input->entity, .shade_scale = 1};
            draw.mvp = qa_scene_matrix_multiply(input->view.projection,
                qa_scene_matrix_multiply(qa_scene_view_matrix(&input->view), draw.model));
            draw.textures[0] = qa_scene_white(model->resources);
            qa_scene_state_default(&draw.state);
            draw.state.cull = model_cull(model, input);
            return qa_scene_frame_draw(frame, &draw, error);
        }
        if (!q3_model_shadow(model, input, mesh, material, &context, frame, error)) return false;
        if (input->family == QA_SCENE_Q3 && (input->flags & 2) && !input->view.clip_enabled) return true;
        size_t begin = frame->command_count;
        if (!qa_material_submit(material, mesh, &context, frame, error)) return false;
        for (size_t i = begin; i < frame->command_count; ++i) if (frame->commands[i].kind == QA_SCENE_COMMAND_DRAW) {
            qa_scene_draw *draw = &frame->commands[i].data.draw;
            depth_and_mirror(draw, input, true);
            if (!unlit && !input->shadow_only && !apply_shadow_lights(draw, input, frame, error)) return false;
        }
        if (!unlit && !input->shadow_only && !planar_shadow(model, original, mesh, frame, error)) return false;
        return qa_scene_frame_group(frame, begin,
            input->source_order ? QA_SCENE_GROUP_SOURCE : QA_SCENE_GROUP_COMPILED,
            material, material->sort, input->entity, input->fog_index, 0, error);
    }
    if (!mesh->index_count) return true;
    if (!input->shadow_only && input->family == QA_SCENE_Q3 && (input->flags & 2) && !input->view.clip_enabled) return true;
    size_t begin = frame->command_count;
    qa_scene_draw draw = {0};
    draw.mesh = *mesh;
    if (world) qa_scene_matrix_identity(&draw.model);
    else draw.model = qa_scene_model_matrix(&input->transform);
    draw.mvp = qa_scene_matrix_multiply(input->view.projection,
        qa_scene_matrix_multiply(qa_scene_view_matrix(&input->view), draw.model));
    draw.textures[0] = image && image->base ? qa_scene_image_at_time(image->base, input->seconds) : qa_scene_missing(model->resources);
    if (shell_image || input->shadow_only) draw.textures[0] = qa_scene_white(model->resources);
    draw.texture_count = 1;
    draw.environment = QA_TEXTURE_MODULATE;
    qa_scene_state_default(&draw.state);
    draw.state.cull = model_cull(model, input);
    bool transparent = input->color.w < 1;
    if (transparent) {
        draw.state.depth_write = false;
        draw.state.blend_source = QA_BLEND_SRC_ALPHA;
        draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
    }
    if (model->source->format == QA_MODEL_SPR) draw.state.alpha_test = QA_ALPHA_GT0;
    if (model->source->format == QA_MODEL_SP2 && !transparent) draw.state.alpha_test = QA_ALPHA_GE128;
    draw.fog = input->fog;
    draw.entity = input->entity;
    draw.sort_key = ((uint64_t)(transparent ? 9u : 3u) << 48) | ((uint64_t)input->entity << 16);
    draw.shade_scale = 1;
    depth_and_mirror(&draw, input, false);
    if (!unlit && !input->shadow_only && !apply_shadow_lights(&draw, input, frame, error)) return false;
    if (!qa_scene_frame_draw(frame, &draw, error)) return false;
    if (!unlit && !input->shadow_only && image && image->fullbright) {
        qa_scene_vertex *vertices = qa_arena_alloc(&frame->storage,
            mesh->vertex_count * sizeof(*vertices), _Alignof(qa_scene_vertex), error);
        if (!vertices) return false;
        memcpy(vertices, mesh->vertices, mesh->vertex_count * sizeof(*vertices));
        for (size_t i = 0; i < mesh->vertex_count; ++i) vertices[i].color = (qa_scene_vec4){1, 1, 1, input->color.w};
        draw.mesh.vertices = vertices; draw.mesh.identity = 0;
        draw.textures[0] = qa_scene_image_at_time(image->fullbright, input->seconds);
        draw.state.depth_write = false;
        draw.state.depth_test = transparent ? QA_DEPTH_LEQUAL : QA_DEPTH_EQUAL;
        draw.state.blend_source = QA_BLEND_SRC_ALPHA;
        draw.state.blend_destination = QA_BLEND_ONE_MINUS_SRC_ALPHA;
        draw.state.alpha_test = QA_ALPHA_GT0;
        draw.lighting = QA_LIGHT_VERTEX;
        draw.light_count = 0; draw.lights = NULL; draw.shadow_atlas = NULL; draw.shade_scale = 1;
        if (!qa_scene_frame_draw(frame, &draw, error)) return false;
    }
    if (input->shadow_only) return true;
    if (!planar_shadow(model, original, mesh, frame, error)) return false;
    return qa_scene_frame_group(frame, begin, QA_SCENE_GROUP_SEQUENCE, NULL,
        transparent ? 9 : 3, input->entity, input->fog_index, 0, error);
}

#include "internal.h"

static qa_model_transform entity_transform(const qa_q3_ref_entity *entity)
{
    qa_model_transform transform;
    qa_model_transform_identity(&transform);
    transform.origin[0] = entity->origin.x;
    transform.origin[1] = entity->origin.y;
    transform.origin[2] = entity->origin.z;
    for (unsigned i = 0; i < 3; ++i) {
        transform.axes[i][0] = entity->axis[i].x;
        transform.axes[i][1] = entity->axis[i].y;
        transform.axes[i][2] = entity->axis[i].z;
    }
    return transform;
}

static float bounds_radius(const qa_model_bounds *bounds)
{
    return qa_vec_length(qa_v3(fmaxf(fabsf(bounds->min[0]), fabsf(bounds->max[0])),
        fmaxf(fabsf(bounds->min[1]), fabsf(bounds->max[1])),
        fmaxf(fabsf(bounds->min[2]), fabsf(bounds->max[2]))));
}

static qa_scene_view weapon_view(const qa_scene_view *view, bool split)
{
    qa_scene_view result = *view;
    float aspect = (float)view->viewport.width / (float)view->viewport.height;
    if (split && !view->clip_enabled && aspect > 4.0f / 3.0f) {
        float scale = (4.0f / 3.0f) / aspect;
        for (unsigned i = 0; i < 4; ++i) result.projection.m[i] *= scale;
    }
    return result;
}

static void material_fog(qa_material_context *context, const qa_scene_fog_volume *fog)
{
    context->fog = fog->fog;
    context->fog_volume_color = fog->fog.color;
    context->fog_index = fog->index;
    context->fog_tc_scale = fog->tc_scale;
    context->fog_has_surface = fog->has_surface;
    context->fog_surface = fog->surface;
}

static qa_material_context effect_context(const qa_q3_scene_options *options)
{
    const qa_scene_world_input *world = &options->world;
    qa_material_context context = {.view = world->view, .seconds = world->seconds,
        .milliseconds = world->milliseconds, .identity_light = world->identity_light,
        .entity_color = {1, 1, 1, 1}, .local_view_origin = world->view.origin,
        .mirror = world->view.mirror, .texts = world->render_texts,
        .text_count = world->render_text_count, .video_frame = world->video_frame,
        .video_context = world->video_context};
    qa_scene_matrix_identity(&context.model);
    return context;
}

static bool submit_polygons(qa_q3_presentation *p, const qa_q3_scene_options *options,
                             qa_error *error)
{
    for (size_t i = 0; i < p->polygon_count; ++i) {
        const q3p_polygon *polygon = &p->polygons[i];
        if (!polygon->count) continue;
        const qa_material *material;
        if (!q3p_shader_get(p->options.assets, polygon->shader, &material, error)) return false;
        if (!material) material = q3p_default_material(p);
        if (!material) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 polygon has no default material");
        qa_scene_mesh mesh;
        if (!qa_scene_poly_geometry(p->frame, p->vertices + polygon->first, polygon->count, &mesh, error)) return false;
        qa_material_context context = effect_context(options);
        context.entity = 1022;
        material_fog(&context, &polygon->fog);
        size_t first = p->frame->command_count;
        if (!qa_material_submit(material, &mesh, &context, p->frame, error) ||
            !qa_scene_frame_group(p->frame, first, QA_SCENE_GROUP_SOURCE, material,
                material->sort, 1022, polygon->fog.index, 0, error)) return false;
    }
    return true;
}

static void model_lighting(qa_q3_presentation *p, const qa_q3_scene_options *options,
                            const qa_q3_ref_entity *entity, qa_scene_model_input *input)
{
    qa_vec3 point = entity->flags & 128 ? entity->lighting_origin : entity->origin;
    qa_vec3 ambient, directed, direction;
    float identity = options->world.identity_light;
    bool sample = !options->world.no_world &&
        qa_scene_world_sample_light(p->world, point, &ambient, &directed, &direction);
    if (!sample) {
        ambient = directed = qa_v3(identity * (150.0f / 255.0f), identity * (150.0f / 255.0f), identity * (150.0f / 255.0f));
        direction = qa_vec_normalize(qa_v3(0.45f, 0.3f, 0.9f));
        qa_vec3 sun;
        qa_material_library_sun(p->options.assets->options.provider.materials, &sun, &direction);
    }
    if (options->world_family != QA_SCENE_Q3 && !options->world.no_world) {
        ambient = qa_vec_add(ambient, directed); directed = qa_v3(0, 0, 0);
        for (size_t i = 0; i < options->world.light_count; ++i) {
            const qa_scene_light *light = &options->world.lights[i];
            float amount = (light->radius - qa_vec_length(qa_vec_sub(point, light->origin))) / 256.0f;
            if (amount > 0) ambient = qa_vec_add(ambient, qa_vec_scale(light->color, amount));
        }
        direction = qa_v3(0, 0, 1);
    } else {
        qa_vec3 weighted = qa_vec_scale(direction, qa_vec_length(directed));
        const qa_scene_light *lights = options->world.use_projected_lights ? options->world.projected_lights : options->world.lights;
        size_t count = options->world.use_projected_lights ? options->world.projected_light_count : options->world.light_count;
        for (size_t i = 0; i < count; ++i) {
            const qa_scene_light *light = &lights[i];
            qa_vec3 relative = qa_vec_sub(light->origin, point);
            float distance = fmaxf(qa_vec_length(relative), 16);
            float amount = 16.0f * light->radius * light->radius / (distance * distance * 255.0f);
            directed = qa_vec_add(directed, qa_vec_scale(light->color, amount));
            weighted = qa_vec_add(weighted, qa_vec_scale(qa_vec_normalize(relative), amount));
        }
        direction = qa_vec_normalize(weighted);
    }
    float minimum = identity * (32.0f / 255.0f);
    float maximum = truncf(identity * 255.0f) / 255.0f;
    input->ambient = qa_v3(fminf(ambient.x + minimum, maximum),
        fminf(ambient.y + minimum, maximum), fminf(ambient.z + minimum, maximum));
    input->directed = directed;
    input->light_direction = qa_v3(qa_vec_dot(direction, entity->axis[0]),
        qa_vec_dot(direction, entity->axis[1]), qa_vec_dot(direction, entity->axis[2]));
}

static void model_frames(const qa_model *source, const qa_q3_ref_entity *entity,
                          qa_scene_model_input *input, float *radius)
{
    int64_t frame = entity->frame, old_frame = entity->old_frame;
    uint32_t count = source->frame_group_count ? source->frame_group_count : source->frame_count;
    if ((entity->flags & 512) && count) { frame %= count; old_frame %= count; }
    if (frame < 0 || old_frame < 0 || (uint64_t)frame >= count || (uint64_t)old_frame >= count) frame = old_frame = 0;
    input->frame = (uint32_t)frame; input->old_frame = (uint32_t)old_frame;
    *radius = bounds_radius(source->frames && source->frame_count ?
        &source->frames[input->frame].bounds : &source->bounds);
}

static bool model_input(qa_q3_presentation *p, const qa_q3_presentation_assets *assets,
    const qa_q3_scene_options *options,
    const qa_q3_ref_entity *entity, uint32_t order, qa_model_transform transform,
    qa_scene_model_input *out, qa_error *error)
{
    const qa_material *material; const qa_model_skin_map *skin;
    if (!q3p_shader_get(assets, entity->custom_shader, &material, error) ||
        !q3p_skin_get(assets, entity->custom_skin, &skin, error)) return false;
    *out = (qa_scene_model_input){
        .view = weapon_view(&options->world.view,
            options->split_screen && !options->world.no_world && (entity->flags & 4)),
        .transform = transform, .previous_origin = entity->old_origin,
        .color = q3p_color(entity->color), .family = QA_SCENE_Q3,
        .skin = (uint32_t)entity->skin, .flags = (uint32_t)entity->flags,
        .entity = order, .back_lerp = entity->back_lerp, .radius = entity->radius,
        .rotation = entity->rotation, .shadow_plane = entity->shadow_plane,
        .identity_light = options->world.identity_light, .seconds = options->world.seconds,
        .custom_material = material, .custom_skin = skin, .view_model = (entity->flags & 4) != 0,
        .no_cull = options->world.no_cull, .non_normalized_axis = entity->non_normalized_axes,
        .source_order = true, .shadow_mode = p->options.shadow_mode,
        .shader_time = entity->shader_time, .shader_texcoord = entity->shader_texcoord,
        .render_texts = options->world.render_texts, .render_text_count = options->world.render_text_count,
        .video_frame = options->world.video_frame, .video_context = options->world.video_context,
        .shadow_lights = options->world.shadow_lights, .shadow_light_count = options->world.shadow_light_count,
        .shadow_atlas = options->world.shadow_atlas};
    return true;
}

static void model_fog(qa_q3_presentation *p, const qa_q3_scene_options *options,
    const qa_q3_ref_entity *entity, const qa_model *source,
    qa_scene_model_input *input, float radius)
{
    qa_scene_fog_volume fog = {0};
    qa_vec3 origin = entity->origin;
    if (source->format == QA_MODEL_MD3 && source->frame_count) {
        uint32_t frame = input->frame < source->frame_count ? input->frame : 0;
        const qa_model_frame *pose = &source->frames[frame];
        origin = qa_vec_add(origin, qa_v3(pose->origin[0], pose->origin[1], pose->origin[2]));
        radius = pose->radius;
    } else if (source->format != QA_MODEL_MD4) radius = bounds_radius(&source->bounds);
    if (!options->world.no_world && source->format != QA_MODEL_MD4)
        qa_scene_world_fog_for_sphere(p->world, origin, radius, &fog);
    input->fog = fog.fog; input->fog_index = fog.index; input->fog_tc_scale = fog.tc_scale;
    input->fog_has_surface = fog.has_surface; input->fog_surface = fog.surface;
}

static void selected_lighting(qa_q3_presentation *, const qa_q3_scene_options *,
    qa_scene_family, const qa_q3_ref_entity *, qa_scene_model_input *);

static bool submit_model(qa_q3_presentation *p, const qa_q3_presentation_assets *assets,
    const qa_q3_scene_options *options,
                          const qa_q3_ref_entity *entity, uint32_t order, qa_error *error)
{
    const q3p_model *model;
    if (!q3p_model_get(assets, entity->model, &model, error)) return false;
    qa_model_transform transform = entity_transform(entity);
    qa_scene_view view = weapon_view(&options->world.view, options->split_screen && !options->world.no_world && (entity->flags & 4));
    if (!model) {
        if ((entity->flags & 2) && !view.clip_enabled) return true;
        const qa_material *material = q3p_default_material(p);
        if (!material) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 model has no default material");
        size_t first = p->frame->command_count;
        if (!qa_scene_default_model(p->frame, &view, qa_scene_model_matrix(&transform),
                qa_scene_white(p->options.assets->options.provider.images), &options->state, error)) return false;
        return qa_scene_frame_group(p->frame, first, QA_SCENE_GROUP_SOURCE, material, material->sort, order, 0, 0, error);
    }
    if (model->world) {
        if ((entity->flags & 2) && !view.clip_enabled) return true;
        qa_scene_world_input world = options->world;
        world.view = view; world.use_animation_frame = true; world.animation_frame = (uint32_t)entity->frame;
        qa_scene_model_input lighting = {0};
        model_lighting(p, options, entity, &lighting);
        qa_scene_world_entity material = {.ambient = lighting.ambient, .directed = lighting.directed,
            .light_direction = lighting.light_direction, .shader_texcoord = entity->shader_texcoord,
            .shader_time = entity->shader_time, .shadow_plane = entity->shadow_plane,
            .non_normalized_axis = entity->non_normalized_axes, .projection_shadow = (entity->flags & 256) != 0};
        world.entity_material = &material;
        return qa_scene_world_submit_model(model->world, model->inline_model, &transform,
            &world, order, q3p_color(entity->color), p->frame, error);
    }
    qa_scene_model_input input;
    if (!model_input(p, assets, options, entity, order, transform, &input, error)) return false;
    input.source_path = qa_resource_path(model->resource);
    float radius;
    const qa_model *base = q3p_model_source(model, 0);
    model_frames(base, entity, &input, &radius);
    uint32_t count = model->has_lods ? model->lods.lod_count : base->lod_count ? base->lod_count : 1;
    input.lod = qa_scene_model_select_lod(&input, count, radius, p->options.lod_scale, p->options.lod_bias);
    const qa_model *selected = q3p_model_source(model, input.lod);
    if (!selected || !model->scene[model->has_lods ? input.lod : 0])
        return q3p_fail(error, QA_ERROR_FORMAT, "selected Q3 model LOD is absent");
    model_fog(p, options, entity, selected, &input, radius);
    selected_lighting(p, options, model->provider.family, entity, &input);
    return qa_scene_model_submit(model->scene[model->has_lods ? input.lod : 0], &input, p->frame, error);
}

static void selected_lighting(qa_q3_presentation *p, const qa_q3_scene_options *options,
    qa_scene_family content, const qa_q3_ref_entity *entity, qa_scene_model_input *input)
{
    model_lighting(p, options, entity, input);
    if (content == QA_SCENE_Q3) return;
    if (content == QA_SCENE_Q1 && options->world_family == QA_SCENE_Q3) {
        input->alias_lighting = QA_ALIAS_Q3_DIFFUSE;
        return;
    }
    qa_vec3 ambient = qa_v3(1, 1, 1), directed, direction;
    if (!options->world.no_world &&
        qa_scene_world_sample_light(p->world, entity->origin, &ambient, &directed, &direction))
        ambient = qa_vec_add(ambient, directed);
    qa_vec3 dynamic = qa_v3(0, 0, 0);
    for (size_t i = 0; i < options->world.light_count; ++i) {
        const qa_scene_light *light = &options->world.lights[i];
        float amount = light->radius - qa_vec_length(qa_vec_sub(entity->origin, light->origin));
        if (amount <= 0) continue;
        if (content == QA_SCENE_Q1 && options->world_family != QA_SCENE_Q2)
            dynamic = qa_vec_add(dynamic, qa_v3(amount / 255, amount / 255, amount / 255));
        else dynamic = qa_vec_add(dynamic, qa_vec_scale(light->color, amount / 256));
    }
    input->alias_lighting = QA_ALIAS_PREPARED_LIGHT;
    if (content == QA_SCENE_Q2 || (options->world_family == QA_SCENE_Q2 && !input->view_model)) {
        input->alias_light = qa_vec_add(ambient, dynamic);
        return;
    }
    float sampled[3] = {ambient.x, ambient.y, ambient.z};
    const float added[3] = {dynamic.x, dynamic.y, dynamic.z};
    for (unsigned i = 0; i < 3; ++i) {
        float base = sampled[i] * 255;
        if (input->view_model) base = fmaxf(base, 24);
        float total = base + added[i] * 255;
        float clamped = fminf(total, 128);
        float shade = fminf(total, 192 - clamped);
        if (input->source_path && !strcmp(input->source_path, "progs/player.mdl") && clamped < 8) shade = 8;
        if (input->source_path && (!strcmp(input->source_path, "progs/flame.mdl") ||
                !strcmp(input->source_path, "progs/flame2.mdl"))) shade = 256;
        sampled[i] = shade / 200 * 2;
    }
    input->alias_light = qa_v3(sampled[0], sampled[1], sampled[2]);
}

static bool selected_model(qa_q3_presentation *p, qa_scene_model *scene,
    const qa_model *source, const char *source_path, const qa_model_transform *transform,
    const qa_q3_ref_entity *entity, const qa_q3_scene_options *options,
    const qa_q3_foreign_view_lighting *view_lighting,
    uint32_t order, qa_scene_frame *frame, qa_error *error)
{
    const qa_scene_image_options *images = qa_scene_model_image_options(scene);
    if (!p || !scene || !source || !source_path || !source_path[0] || !transform || !entity || !options || !frame ||
        !p->busy || p->submission != options || p->frame != frame ||
        qa_scene_model_source(scene) != source || !images ||
        images->family < QA_SCENE_Q1 || images->family > QA_SCENE_Q3 ||
        entity->kind != QA_Q3_REF_MODEL || order >= 1022 ||
        (view_lighting && (view_lighting->content != images->family ||
            images->family == QA_SCENE_Q3 || !(entity->flags & 4) ||
            (images->family == QA_SCENE_Q1 && view_lighting->flags) ||
            (images->family == QA_SCENE_Q2 && (view_lighting->flags & ~(1u | 4u | 16u))))))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Selected model requires its actual content and active Q3 view submission");
    qa_scene_model_input input;
    if (!model_input(p, p->options.assets, options, entity, order, *transform, &input, error)) return false;
    input.source_path = source_path;
    float radius;
    model_frames(source, entity, &input, &radius);
    uint32_t count = source->lod_count ? source->lod_count : 1;
    input.lod = qa_scene_model_select_lod(&input, count, radius, p->options.lod_scale, p->options.lod_bias);
    qa_q3_ref_entity placed = *entity;
    placed.origin = qa_v3(transform->origin[0], transform->origin[1], transform->origin[2]);
    for (unsigned i = 0; i < 3; ++i)
        placed.axis[i] = qa_vec_scale(qa_v3(transform->axes[i][0], transform->axes[i][1], transform->axes[i][2]),
            transform->scale[i]);
    model_fog(p, options, &placed, source, &input, radius);
    selected_lighting(p, options, images->family, &placed, &input);
    if (view_lighting && view_lighting->content == QA_SCENE_Q2 && (view_lighting->flags & 1) &&
        input.alias_light.x <= .1f && input.alias_light.y <= .1f && input.alias_light.z <= .1f)
        input.alias_light = qa_v3(.1f, .1f, .1f);
    return qa_scene_model_submit(scene, &input, frame, error);
}

bool qa_q3_presentation_selected_model(qa_q3_presentation *p, qa_scene_model *scene,
    const qa_model *source, const char *source_path, const qa_model_transform *transform,
    const qa_q3_ref_entity *entity, const qa_q3_scene_options *options,
    uint32_t order, qa_scene_frame *frame, qa_error *error)
{
    return selected_model(p, scene, source, source_path, transform, entity, options,
        NULL, order, frame, error);
}

bool qa_q3_presentation_selected_view_model(qa_q3_presentation *p, qa_scene_model *scene,
    const qa_model *source, const char *source_path, const qa_model_transform *transform,
    const qa_q3_ref_entity *entity, const qa_q3_scene_options *options,
    const qa_q3_foreign_view_lighting *lighting, uint32_t order, qa_scene_frame *frame, qa_error *error)
{
    if (!lighting) return q3p_fail(error, QA_ERROR_ARGUMENT, "Foreign view requires its actual content lighting policy");
    return selected_model(p, scene, source, source_path, transform, entity, options,
        lighting, order, frame, error);
}

bool qa_q3_presentation_selected_registered(qa_q3_presentation *p,
    const qa_q3_presentation_assets *assets, const qa_q3_ref_entity *entity,
    const qa_q3_scene_options *options, uint32_t order, qa_scene_frame *frame, qa_error *error)
{
    if (!p || !assets || !entity || !options || !frame || !p->busy ||
        p->submission != options || p->frame != frame || !qa_q3_assets_idle(assets) ||
        entity->kind != QA_Q3_REF_MODEL || entity->model <= 0 || order >= 1022)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Selected registry requires its actual retained assets and active view lease");
    const q3p_model *model;
    if (!q3p_model_get(assets, entity->model, &model, error)) return false;
    if (!model || model->world)
        return q3p_fail(error, QA_ERROR_FORMAT, "Selected equipment registry has no actual non-world model holder");
    return submit_model(p, assets, options, entity, order, error);
}

static bool submit_effect(qa_q3_presentation *p, const qa_q3_scene_options *options,
                           const qa_q3_ref_entity *entity, uint32_t order, qa_error *error)
{
    const qa_material *material;
    if (!q3p_shader_get(p->options.assets, entity->custom_shader, &material, error)) return false;
    if (!material) material = q3p_default_material(p);
    if (!material) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 effect has no default material");
    qa_scene_fog_volume fog = {0};
    if (!options->world.no_world) qa_scene_world_fog_for_sphere(p->world, entity->origin, entity->radius, &fog);
    size_t first = p->frame->command_count;
    if (entity->kind == QA_Q3_REF_BEAM) {
        if (!qa_scene_q3_beam(p->frame, &options->world.view, entity->origin, entity->old_origin,
                qa_scene_white(p->options.assets->options.provider.images), &options->state, error)) return false;
    } else {
        qa_scene_mesh mesh;
        qa_scene_vec4 color = q3p_color(entity->color);
        if (entity->kind == QA_Q3_REF_SPRITE) {
            if (!qa_scene_sprite_geometry(p->frame, &options->world.view, entity->origin,
                    entity->radius, entity->rotation, color, &mesh, error)) return false;
        } else {
            qa_scene_rail_kind kind = entity->kind == QA_Q3_REF_RAIL_CORE ? QA_RAIL_CORE :
                entity->kind == QA_Q3_REF_RAIL_RINGS ? QA_RAIL_RINGS : QA_RAIL_LIGHTNING;
            qa_scene_rail_options rail = {.core_width = p->options.rail_core_width,
                .ring_width = p->options.rail_ring_width, .segment_length = p->options.rail_segment_length};
            if (!qa_scene_rail_geometry(p->frame, &options->world.view, kind,
                    entity->origin, entity->old_origin, color, &rail, &mesh, error)) return false;
        }
        qa_material_context context = effect_context(options);
        context.entity = order; context.entity_color = color;
        context.entity_texcoord = entity->shader_texcoord; context.time_offset = entity->shader_time;
        material_fog(&context, &fog);
        if (!qa_material_submit(material, &mesh, &context, p->frame, error)) return false;
    }
    return qa_scene_frame_group(p->frame, first, QA_SCENE_GROUP_SOURCE, material,
        material->sort, order, fog.index, 0, error);
}

static bool submit_view(qa_q3_presentation *p, const qa_q3_scene_options *options, qa_error *error)
{
    if (p->world) {
        if (!qa_scene_world_submit(p->world, &options->world, p->frame, error)) return false;
    } else {
        qa_scene_command command = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = options->world.view};
        if (!qa_scene_frame_emit(p->frame, &command, error)) return false;
    }
    if (!submit_polygons(p, options, error)) return false;
    for (size_t i = 0; i < p->entity_count; ++i) {
        qa_q3_ref_entity entity = p->entities[i];
        if (entity.kind == QA_Q3_REF_POLY) return q3p_fail(error, QA_ERROR_FORMAT, "R_AddEntitySurfaces: Bad reType");
        if (entity.kind == QA_Q3_REF_PORTAL ||
            ((entity.flags & 4) && (options->world.view.clip_enabled ||
                (!options->world.no_world && options->supplemental_weapon)))) continue;
        if (!options->world.no_world && (entity.flags & 4)) {
            entity.origin = qa_vec_add(entity.origin, options->weapon_offset);
            entity.old_origin = qa_vec_add(entity.old_origin, options->weapon_offset);
            entity.lighting_origin = qa_vec_add(entity.lighting_origin, options->weapon_offset);
            entity.shadow_plane += options->weapon_offset.z;
        }
        uint32_t order = options->first_entity + (uint32_t)i;
        if (entity.kind == QA_Q3_REF_MODEL) {
            if (!submit_model(p, p->options.assets, options, &entity, order, error)) return false;
        } else if (!(entity.flags & 2) || options->world.view.clip_enabled) {
            if (!submit_effect(p, options, &entity, order, error)) return false;
        }
    }
    if (p->options.submit_view) {
        p->submission = options;
        bool ok = p->options.submit_view(p->options.context, options, p->frame, error);
        p->submission = NULL;
        if (!ok) return false;
    }
    if (!qa_scene_frame_finish(p->frame, &options->world.view, &options->world.fog, error)) return false;
    return p->options.shadow_mode != 2 || qa_scene_stencil_finish(p->frame, &options->world.view,
        qa_scene_white(p->options.assets->options.provider.images), error);
}

bool qa_q3_presentation_render(qa_q3_presentation *p, const qa_q3_refdef *refdef, qa_error *error)
{
    if (!refdef || !q3p_begin(p, error)) return false;
    int64_t x = (int64_t)p->options.viewport.x + refdef->x;
    int64_t y = (int64_t)p->options.viewport.y + refdef->y;
    bool ok = p->frame && !p->frame->group_count && refdef->width > 0 && refdef->height > 0 &&
        x >= INT32_MIN && x <= INT32_MAX && y >= INT32_MIN && y <= INT32_MAX &&
        isfinite(refdef->fov_x) && refdef->fov_x > 0 && refdef->fov_x < 180 &&
        isfinite(refdef->fov_y) && refdef->fov_y > 0 && refdef->fov_y < 180 &&
        qa_vec_finite(refdef->origin) && qa_vec_finite(refdef->axis[0]) &&
        qa_vec_finite(refdef->axis[1]) && qa_vec_finite(refdef->axis[2]);
    if (!ok) return q3p_end(p, q3p_fail(error, QA_ERROR_ARGUMENT, "invalid Q3 refdef or unfinished frame view"));
    uint8_t visible[32]; char rows[8][33]; const char *texts[8];
    for (size_t i = 0; i < 32; ++i) visible[i] = (uint8_t)~refdef->area_mask[i];
    for (size_t i = 0; i < 8; ++i) { memcpy(rows[i], refdef->text[i], 32); rows[i][32] = 0; texts[i] = rows[i]; }
    qa_q3_scene_options options = {.world_family = QA_SCENE_Q3, .world = {
        .view = {.viewport = {(int32_t)x, (int32_t)y, (uint32_t)refdef->width, (uint32_t)refdef->height},
            .origin = refdef->origin, .axis = {refdef->axis[0], refdef->axis[1], refdef->axis[2]},
            .projection = qa_scene_projection(refdef->fov_x, refdef->fov_y, p->options.near_clip, p->options.far_clip),
            .clear_depth = true, .clear_stencil = p->options.shadow_mode == 2, .depth = 1, .seat = p->options.seat},
        .seconds = (double)refdef->time / 1000.0, .milliseconds = refdef->time,
        .no_world = (refdef->flags & 1) != 0, .visible_areas = visible, .visible_area_bytes = sizeof(visible),
        .lights = p->lights, .light_count = p->light_count, .use_projected_lights = true,
        .projected_lights = p->lights, .projected_light_count = p->light_count < 32 ? p->light_count : 32, .source_order = true,
        .identity_light = p->options.identity_light, .curve_error = 250,
        .render_texts = texts, .render_text_count = 8, .video_frame = p->options.video_frame,
        .video_context = p->options.video_context}};
    size_t first = p->frame->command_count;
    qa_scene_state_default(&options.state);
    p->render_milliseconds = refdef->time;
    if (p->options.prepare_view) ok = p->options.prepare_view(p->options.context, refdef, &options, error);
    if (ok && (!options.world.no_world && (!p->world || !p->world_loaded)))
        ok = q3p_fail(error, QA_ERROR_ARGUMENT, "RE_RenderScene: NULL worldmodel");
    if (ok && (options.first_entity >= 1022 || p->entity_count > 1022 - options.first_entity ||
        options.world.projected_light_count > 32 || (options.world.light_count && !options.world.lights) ||
        (options.world.projected_light_count && !options.world.projected_lights) ||
        !qa_vec_finite(options.weapon_offset)))
        ok = q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 scene exceeds source entity/light limits or has invalid spans");
    size_t portal_count = 0;
    if (ok) ok = q3p_reserve((void **)&p->portals, &p->portal_capacity, p->entity_count, sizeof(*p->portals), error);
    for (size_t i = 0; ok && i < p->entity_count; ++i) {
        const qa_q3_ref_entity *entity = &p->entities[i];
        if (entity->kind != QA_Q3_REF_PORTAL) continue;
        qa_scene_portal portal = {.origin = entity->origin, .old_origin = entity->old_origin,
            .axis = {entity->axis[0], entity->axis[1], entity->axis[2]},
            .rotation_speed = entity->old_frame && entity->frame ? (float)entity->frame : 0,
            .rotation_offset = (float)entity->skin, .oscillate = entity->old_frame && !entity->frame};
        p->portals[portal_count++] = portal;
    }
    if (ok && p->world && !options.world.no_world && portal_count) {
        qa_scene_view child; qa_vec3 pvs; bool found;
        ok = qa_scene_world_portal_view(p->world, &options.world, p->portals, portal_count, &child, &pvs, &found, error);
        if (ok && found) {
            qa_q3_scene_options child_options = options;
            child_options.world.view = child; child_options.world.pvs_origin = pvs;
            child_options.world.use_pvs_origin = true;
            ok = submit_view(p, &child_options, error);
        }
    }
    if (ok) ok = submit_view(p, &options, error);
    if (ok) { p->material_view = options.world.view; p->material_view_valid = true; }
    if (!ok) { p->frame->command_count = first; p->frame->group_count = 0; }
    return q3p_end(p, ok);
}

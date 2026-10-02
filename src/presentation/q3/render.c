#include "internal.h"
#include "qa/q3_presentation_save.h"
#include "qa/material_library_save.h"
#include "qa/scene_model_save.h"
#include "qa/q3_source_scene_bank.h"

static bool source_scene_snapshot(qa_q3_presentation *p, qa_error *error)
{
    p->source_entity_assets = p->source_polygon_assets = NULL;
    if (!p->options.source_state) return true;
    qa_material_source_scratch *source = p->options.source_state(p->options.context, error);
    qa_q3_source_scene_bank *bank = source ? qa_material_source_scene_bank(source, error) : NULL;
    qa_q3_source_scene_membership membership;
    if (!bank || !qa_q3_source_scene_bank_membership(bank, &membership)) return false;
    size_t entities = membership.entities - membership.first_entity;
    size_t polygons = membership.polygons - membership.first_polygon;
    size_t lights = membership.lights - membership.first_light;
    if (!q3p_reserve((void **)&p->entities, &p->entity_capacity, entities, sizeof(*p->entities), error) ||
        !q3p_reserve((void **)&p->polygons, &p->polygon_capacity, polygons, sizeof(*p->polygons), error) ||
        !q3p_reserve((void **)&p->vertices, &p->vertex_capacity, membership.vertices, sizeof(*p->vertices), error) ||
        !q3p_reserve((void **)&p->lights, &p->light_capacity, lights, sizeof(*p->lights), error)) return false;
    if (entities) {
        p->source_entity_assets = qa_arena_alloc(&p->frame->storage, entities * sizeof(*p->source_entity_assets),
            _Alignof(qa_q3_presentation_assets *), error);
        if (!p->source_entity_assets) return false;
    }
    if (polygons) {
        p->source_polygon_assets = qa_arena_alloc(&p->frame->storage, polygons * sizeof(*p->source_polygon_assets),
            _Alignof(qa_q3_presentation_assets *), error);
        if (!p->source_polygon_assets) return false;
    }
    for (size_t i = 0; i < entities; ++i) {
        qa_q3_source_entity_cell cell;
        if (!qa_q3_source_scene_bank_entity_read(bank, membership.first_entity + (uint32_t)i, &cell)) return false;
        p->entities[i] = cell.value; p->source_entity_assets[i] = cell.assets;
    }
    size_t vertex_count = 0;
    for (size_t i = 0; i < polygons; ++i) {
        qa_q3_source_polygon_cell cell; const qa_q3_poly_vertex *vertices;
        if (!qa_q3_source_scene_bank_poly_read(bank, membership.first_polygon + (uint32_t)i, &cell, &vertices)) return false;
        p->polygons[i] = (q3p_polygon){.shader = cell.shader, .first = vertex_count,
            .count = cell.count, .fog = cell.fog};
        p->source_polygon_assets[i] = cell.assets;
        for (size_t j = 0; j < cell.count; ++j) p->vertices[vertex_count++] = (qa_scene_vertex){
            .position = vertices[j].position, .texcoord = vertices[j].texcoord, .color = q3p_color(vertices[j].color)};
    }
    for (size_t i = 0; i < lights; ++i) {
        qa_q3_source_light_cell cell;
        if (!qa_q3_source_scene_bank_light_read(bank, membership.first_light + (uint32_t)i, &cell)) return false;
        p->lights[i] = cell.value;
    }
    p->source_entity_first = membership.first_entity;
    p->entity_count = entities; p->polygon_count = polygons; p->vertex_count = vertex_count; p->light_count = lights;
    return true;
}

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
    qa_material_context context = {.view = world->view, .seconds = (float)world->milliseconds * .001f,
        .milliseconds = world->milliseconds, .identity_light = world->identity_light,
        .entity_color = {1, 1, 1, 1}, .local_view_origin = world->view.origin,
        .mirror = world->view.mirror, .texts = world->render_texts,
        .text_count = world->render_text_count, .video_frame = world->video_frame,
        .video_context = world->video_context, .source_primitives = true,
        .source_scratch = world->source_scratch, .source_white = world->source_white,
        .source_recipient_image = world->source_recipient_image,
        .source_recipient_context = world->source_recipient_context,
        .source_diagnostics = world->source_diagnostics,
        .source_diagnostics_read = world->source_diagnostics_read,
        .source_diagnostics_context = world->source_diagnostics_context};
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
        const qa_q3_presentation_assets *assets = p->source_polygon_assets ? p->source_polygon_assets[i] : p->options.assets;
        if (!q3p_shader_get(assets, polygon->shader, &material, error)) return false;
        if (!material) material = q3p_default_material(p);
        if (!material) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 polygon has no default material");
        qa_scene_mesh mesh;
        if (!qa_scene_poly_geometry(p->frame, p->vertices + polygon->first, polygon->count, &mesh, error)) return false;
        qa_material_context context = effect_context(options);
        context.entity = 1022;
        context.source_writer = QA_SOURCE_WRITE_POLY;
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
    if (sample && options->world_family == QA_SCENE_Q3) {
        ambient = qa_vec_scale(ambient, options->ambient_scale);
        directed = qa_vec_scale(directed, options->directed_scale);
    }
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
    const qa_model_frame *pose = source->frames && source->frame_count ?
        &source->frames[input->frame] : NULL;
    *radius = source->format == QA_MODEL_MD4 && pose ? pose->radius :
        bounds_radius(pose ? &pose->bounds : &source->bounds);
}

static bool model_input(qa_q3_presentation *p, const qa_q3_presentation_assets *assets,
    const qa_q3_presentation_assets *shader_assets,
    const qa_q3_scene_options *options,
    const qa_q3_ref_entity *entity, uint32_t order, qa_model_transform transform,
    qa_scene_model_input *out, qa_error *error)
{
    const qa_material *material; const qa_model_skin_map *skin;
    const qa_material *const *skin_materials = NULL; size_t skin_material_count = 0;
    if (!q3p_shader_get(shader_assets, entity->custom_shader, &material, error) ||
        !q3p_skin_get(assets, entity->custom_skin, &skin, error) ||
        !q3p_skin_materials(assets, entity->custom_skin, &skin_materials, &skin_material_count, error)) return false;
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
        .custom_skin_materials = skin_materials, .custom_skin_material_count = skin_material_count,
        .no_cull = options->world.no_cull, .non_normalized_axis = entity->non_normalized_axes,
        .source_order = true, .source_scratch = options->world.source_scratch, .shadow_mode = options->shadow_mode,
        .source_entity_cell = options->world.source_entity_cells && !p->submission,
        .source_recipient_image = options->world.source_recipient_image,
        .source_recipient_context = options->world.source_recipient_context,
        .source_diagnostics = options->world.source_diagnostics,
        .source_diagnostics_read = options->world.source_diagnostics_read,
        .source_diagnostics_context = options->world.source_diagnostics_context,
        .milliseconds = options->world.milliseconds, .has_milliseconds = true,
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
    bool source_md4 = source->format == QA_MODEL_MD4 && input->source_order;
    if ((source->format == QA_MODEL_MD3 || source->format == QA_MODEL_MD4) &&
        source->frames && source->frame_count && !source_md4) {
        uint32_t frame = input->frame < source->frame_count ? input->frame : 0;
        const qa_model_frame *pose = &source->frames[frame];
        origin = qa_vec_add(origin, qa_v3(pose->origin[0], pose->origin[1], pose->origin[2]));
        radius = pose->radius;
    } else if (source->format != QA_MODEL_MD4) radius = bounds_radius(&source->bounds);
    if (!options->world.no_world && !source_md4)
        qa_scene_world_fog_for_sphere(p->world, origin, radius, &fog);
    input->fog = fog.fog; input->fog_index = fog.index; input->fog_tc_scale = fog.tc_scale;
    input->fog_has_surface = fog.has_surface; input->fog_surface = fog.surface;
}

static void selected_lighting(qa_q3_presentation *, const qa_q3_scene_options *,
    qa_scene_family, const qa_q3_ref_entity *, qa_scene_model_input *);
static bool source_model_lighting(qa_q3_presentation *p, const qa_q3_scene_options *options,
    const qa_q3_ref_entity *entity, qa_scene_model_input *input, bool calculate, qa_error *error)
{
    if (!input->source_entity_cell || !input->source_scratch) {
        if (calculate) model_lighting(p, options, entity, input);
        return true;
    }
    qa_q3_source_scene_bank *bank = qa_material_source_scene_bank(input->source_scratch, error);
    qa_q3_source_entity_cell cell;
    if (!bank || !qa_q3_source_scene_bank_entity_read(bank, input->entity, &cell)) {
        if (!error || error->code == QA_OK) q3p_fail(error, QA_ERROR_ARGUMENT, "Source lighting lost its actual physical entity");
        return false;
    }
    if (calculate && !cell.lighting_calculated) {
        model_lighting(p, options, entity, input);
        if (!qa_q3_source_scene_bank_entity_lighting(bank, input->entity, input->ambient, input->directed,
            input->light_direction, 1, cell.axis_length, cell.need_lights))
            return q3p_fail(error, QA_ERROR_ARGUMENT, "Source lighting lost its admitted entity cell");
    } else {
        input->ambient = cell.ambient; input->directed = cell.directed; input->light_direction = cell.light_direction;
    }
    return true;
}

static bool submit_model(qa_q3_presentation *p, const qa_q3_presentation_assets *assets,
    const qa_q3_presentation_assets *skin_assets, const qa_q3_presentation_assets *shader_assets,
    const qa_q3_scene_options *options,
                          const qa_q3_ref_entity *entity, uint32_t order,
                          const int32_t *source_time, bool source_order, qa_error *error)
{
    if (options->no_entities) return true;
    const q3p_model *model;
    if (!q3p_model_get(assets, entity->model, &model, error)) return false;
    qa_model_transform transform = entity_transform(entity);
    qa_scene_view view = weapon_view(&options->world.view, options->split_screen && !options->world.no_world && (entity->flags & 4));
    if (!model) {
        if ((entity->flags & 2) && !view.clip_enabled) return true;
        const qa_material *material = qa_material_find(shader_assets->options.provider.materials, "*default");
        if (!material) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 model has no default material");
        size_t first = p->frame->command_count;
        if (!qa_scene_default_model(p->frame, &view, qa_scene_model_matrix(&transform),
                qa_scene_white(assets->options.provider.images), &options->state, error)) return false;
        if (source_order && options->world.source_scratch) {
            for (size_t i = first; i < p->frame->command_count; ++i)
                if (p->frame->commands[i].kind == QA_SCENE_COMMAND_DRAW)
                    p->frame->commands[i].data.draw.source_direct = QA_SOURCE_DIRECT_AXIS;
            qa_material_context context = effect_context(options);
            if (source_time) {
                context.seconds=(float)*source_time*.001f;
                context.milliseconds=*source_time;
            }
            context.view = view; context.entity = order; context.model = qa_scene_model_matrix(&transform);
            context.source_entity_cell = options->world.source_entity_cells && !p->submission;
            context.entity_color = q3p_color(entity->color); context.time_offset = entity->shader_time;
            if (!qa_material_source_commands(material, &context, p->frame, first, error)) return false;
        }
        return qa_scene_frame_group(p->frame, first,
            source_order ? QA_SCENE_GROUP_SOURCE : QA_SCENE_GROUP_COMPILED,
            material, material->sort, order, 0, 0, error);
    }
    if (model->world) {
        if ((entity->flags & 2) && !view.clip_enabled) return true;
        qa_scene_world_input world = options->world;
        world.source_scratch = source_order ? options->world.source_scratch : NULL;
        world.source_entity_cells = options->world.source_entity_cells && !p->submission;
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
    if (!model_input(p, skin_assets, shader_assets, options, entity, order, transform, &input, error)) return false;
    input.source_order = source_order;
    input.source_scratch = source_order ? options->world.source_scratch : NULL;
    if (source_time) {
        input.seconds = (float)*source_time * .001f;
        input.milliseconds = *source_time; input.has_milliseconds = true;
    } else if (!source_order) {
        input.milliseconds = options->world.milliseconds; input.has_milliseconds = true;
    }
    input.source_path = qa_resource_path(model->resource);
    float radius;
    const qa_model *base = q3p_model_source(model, 0);
    model_frames(base, entity, &input, &radius);
    uint32_t count = model->has_lods ? model->lods.lod_count : base->lod_count ? base->lod_count : 1;
    input.lod = qa_scene_model_select_lod(&input, count, radius, options->lod_scale, options->lod_bias);
    const qa_model *selected = q3p_model_source(model, input.lod);
    if (!selected || !model->scene[model->has_lods ? input.lod : 0])
        return q3p_fail(error, QA_ERROR_FORMAT, "selected Q3 model LOD is absent");
    if (model->provider.family == QA_SCENE_Q3 || skin_assets != assets || shader_assets != assets)
        input.material_library = shader_assets->options.provider.materials;
    model_fog(p, options, entity, selected, &input, radius);
    if (source_order && (selected->format == QA_MODEL_MD3 || selected->format == QA_MODEL_MD4)) {
        bool visible;
        if (!qa_scene_model_source_admission(model->scene[model->has_lods ? input.lod : 0], &input, &visible, error)) return false;
        if (!visible) return true;
        bool calculate = selected->format == QA_MODEL_MD3 &&
            (!(entity->flags & 2) || input.view.clip_enabled || options->shadow_mode > 1);
        if (!source_model_lighting(p, options, entity, &input, calculate, error)) return false;
    } else selected_lighting(p, options, model->provider.family, entity, &input);
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
    if (options->no_entities) return true;
    qa_scene_model_input input;
    if (!model_input(p, p->options.assets, p->options.assets, options, entity, order, *transform, &input, error)) return false;
    input.source_path = source_path;
    if (images->family == QA_SCENE_Q3)
        input.material_library = p->options.assets->options.provider.materials;
    float radius;
    model_frames(source, entity, &input, &radius);
    uint32_t count = source->lod_count ? source->lod_count : 1;
    input.lod = qa_scene_model_select_lod(&input, count, radius, options->lod_scale, options->lod_bias);
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
    return submit_model(p, assets, assets, assets, options, entity, order, NULL, true, error);
}

bool qa_q3_presentation_selected_registered_pass(qa_q3_presentation *p,
    const qa_q3_presentation_assets *assets, const qa_q3_ref_entity *entity,
    const qa_q3_presentation_assets *source_assets, const qa_q3_ref_entity *source_pass,
    const qa_q3_scene_options *options, uint32_t order, qa_scene_frame *frame, qa_error *error)
{
    if (!p || !assets || !entity || !source_pass || source_assets != p->options.assets ||
        !options || !frame || !p->busy || p->submission != options || p->frame != frame ||
        !qa_q3_assets_idle(assets) || source_assets->busy != 1 ||
        source_assets->capturing || source_assets->codec_busy || !q3p_assets_children_idle(source_assets) ||
        entity->kind != QA_Q3_REF_MODEL || entity->model <= 0 || order >= 1022 ||
        !isfinite(source_pass->shader_time))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Selected held pass requires its real model and primary shader namespaces");
    const q3p_model *model; const qa_material *material;
    if (!q3p_model_get(assets, entity->model, &model, error) ||
        !q3p_shader_get(source_assets, source_pass->custom_shader, &material, error)) return false;
    if (!model || model->world)
        return q3p_fail(error, QA_ERROR_FORMAT, "Selected held pass has no actual non-world model holder");
    qa_q3_ref_entity styled = *entity;
    styled.custom_shader = source_pass->custom_shader; styled.shader_time = source_pass->shader_time;
    memset(styled.color, 255, sizeof(styled.color));
    if (material) memcpy(styled.color, source_pass->color, sizeof(styled.color));
    return submit_model(p, assets, assets, material ? source_assets : assets, options, &styled, order, NULL, true, error);
}

bool qa_q3_presentation_selected_body_pass(qa_q3_presentation *p,
    const qa_q3_presentation_assets *assets, const qa_q3_ref_entity *entity,
    const qa_q3_presentation_assets *source_assets, const qa_q3_ref_entity *source_pass,
    const qa_q3_scene_options *options, uint32_t order, qa_scene_frame *frame, qa_error *error)
{
    if (!p || !assets || !entity || !source_pass || source_assets != p->options.assets ||
        !options || !frame || !p->busy || p->submission != options || p->frame != frame ||
        !qa_q3_assets_idle(assets) || source_assets->busy != 1 || source_assets->capturing ||
        source_assets->codec_busy || !q3p_assets_children_idle(source_assets) ||
        entity->kind != QA_Q3_REF_MODEL || entity->model <= 0 || order >= 1022 ||
        source_pass->kind != QA_Q3_REF_MODEL || !isfinite(source_pass->shader_time) ||
        !isfinite(source_pass->shader_texcoord.x) || !isfinite(source_pass->shader_texcoord.y) ||
        !qa_vec_finite(source_pass->lighting_origin) || !isfinite(source_pass->shadow_plane))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Selected body pass requires its real posed model and primary material namespaces");
    const q3p_model *model;
    if (!q3p_model_get(assets, entity->model, &model, error)) return false;
    if (!model || model->world)
        return q3p_fail(error, QA_ERROR_FORMAT, "Selected body pass has no actual non-world model holder");
    const qa_material *shader;
    if (!q3p_shader_get(source_assets, source_pass->custom_shader, &shader, error)) return false;
    qa_q3_ref_entity styled = *entity;
    styled.custom_shader = source_pass->custom_shader;
    styled.custom_skin = shader ? 0 : source_pass->custom_skin;
    memcpy(styled.color, source_pass->color, sizeof(styled.color));
    styled.shader_texcoord = source_pass->shader_texcoord;
    styled.shader_time = source_pass->shader_time;
    styled.flags = source_pass->flags;
    styled.lighting_origin = source_pass->lighting_origin;
    styled.shadow_plane = source_pass->shadow_plane;
    styled.non_normalized_axes = source_pass->non_normalized_axes;
    return submit_model(p, assets, source_assets, source_assets, options, &styled, order, NULL, true, error);
}

static bool body_material_equal(const qa_q3_presentation_assets *assets,
    const qa_q3_ref_entity *a, const qa_q3_ref_entity *b, bool *equal, qa_error *error)
{
    *equal = false;
    if (memcmp(a->color, b->color, sizeof(a->color)) ||
        a->shader_texcoord.x != b->shader_texcoord.x || a->shader_texcoord.y != b->shader_texcoord.y ||
        a->shader_time != b->shader_time || a->flags != b->flags ||
        a->lighting_origin.x != b->lighting_origin.x || a->lighting_origin.y != b->lighting_origin.y ||
        a->lighting_origin.z != b->lighting_origin.z || a->shadow_plane != b->shadow_plane ||
        a->non_normalized_axes != b->non_normalized_axes) return true;
    const qa_material *first, *second;
    if (!q3p_shader_get(assets, a->custom_shader, &first, error) ||
        !q3p_shader_get(assets, b->custom_shader, &second, error)) return false;
    if (!!first != !!second) return true;
    if (first) { *equal = !strcmp(first->name, second->name); return true; }
    const qa_model_skin_map *x, *y;
    if (!q3p_skin_get(assets, a->custom_skin, &x, error) ||
        !q3p_skin_get(assets, b->custom_skin, &y, error)) return false;
    if (!!x != !!y || (x && x->count != y->count)) return true;
    if (x) for (size_t i = 0; i < x->count; ++i)
        if (strcmp(x->mappings[i].surface, y->mappings[i].surface) ||
            strcmp(x->mappings[i].shader, y->mappings[i].shader)) return true;
    *equal = true; return true;
}
bool qa_q3_presentation_body_material_equal(qa_q3_presentation *p,
    const qa_q3_presentation_assets *assets, const qa_q3_ref_entity *a,
    const qa_q3_ref_entity *b, bool *equal, qa_error *error)
{
    if (!p || !a || !b || !equal || assets != p->options.assets || !p->busy || !p->submission ||
        !p->frame || assets->busy != 1 || assets->capturing || assets->codec_busy || !q3p_assets_children_idle(assets))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Body material comparison requires its actual primary submission lease");
    return body_material_equal(assets,a,b,equal,error);
}
bool qa_q3_presentation_source_body_material_equal(qa_q3_presentation *p,
    const qa_q3_presentation_assets *materials, const qa_q3_ref_entity *a,
    const qa_q3_ref_entity *b, bool *equal, qa_error *error)
{
    const qa_q3_presentation_assets *primary = p ? p->options.assets : NULL;
    if (!p || !primary || !materials || !a || !b || !equal || !p->busy ||
        !p->submission || !p->frame || primary->busy != 1 || primary->capturing ||
        primary->codec_busy || !q3p_assets_children_idle(primary) ||
        (materials != primary && !qa_q3_assets_idle(materials)))
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Source body comparison lost its real entered model and material owners");
    return body_material_equal(materials,a,b,equal,error);
}
bool qa_q3_presentation_source_body_pass(qa_q3_presentation *p, const qa_q3_ref_entity *entity,
    const qa_q3_presentation_assets *materials, const qa_q3_ref_entity *pass, int32_t time,
    const qa_q3_scene_options *options, uint32_t order, qa_scene_frame *frame, qa_error *error)
{
    const qa_q3_presentation_assets *primary = p ? p->options.assets : NULL;
    if (!p || !primary || !materials || !entity || !pass || !options || !frame ||
        !p->busy || p->submission != options || p->frame != frame || primary->busy != 1 ||
        primary->capturing || primary->codec_busy || !q3p_assets_children_idle(primary) ||
        (materials != primary && !qa_q3_assets_idle(materials)) ||
        entity->kind != QA_Q3_REF_MODEL || entity->model <= 0 || order >= 1022 ||
        pass->kind != QA_Q3_REF_MODEL || !isfinite(pass->shader_time) ||
        !isfinite(pass->shader_texcoord.x) || !isfinite(pass->shader_texcoord.y) ||
        !qa_vec_finite(pass->lighting_origin) || !isfinite(pass->shadow_plane))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Source body pass lost its entered model and retained material owners");
    const q3p_model *model;
    const qa_material *shader;
    if (!q3p_model_get(primary, entity->model, &model, error)) return false;
    if (!model)
        return q3p_fail(error, QA_ERROR_FORMAT, "Source body pass has no actual primary non-world model holder");
    if (model->world) return true;
    if (!q3p_shader_get(materials, pass->custom_shader, &shader, error)) return false;
    if (options->no_entities) return true;
    qa_q3_ref_entity styled = *entity;
    styled.custom_shader = pass->custom_shader;
    styled.custom_skin = shader ? 0 : pass->custom_skin;
    memcpy(styled.color, pass->color, sizeof(styled.color));
    styled.shader_texcoord = pass->shader_texcoord; styled.shader_time = pass->shader_time;
    styled.flags = pass->flags; styled.lighting_origin = pass->lighting_origin;
    styled.shadow_plane = pass->shadow_plane; styled.non_normalized_axes = pass->non_normalized_axes;
    return submit_model(p, primary, materials, materials, options, &styled, order, &time, true, error);
}

static bool submit_effect(qa_q3_presentation *p, const qa_q3_presentation_assets *assets,
                           const qa_q3_scene_options *options,
                           const qa_q3_ref_entity *entity, uint32_t order,
                           const int32_t *source_time, qa_error *error)
{
    if (options->no_entities) return true;
    const qa_material *material;
    if (!q3p_shader_get(assets, entity->custom_shader, &material, error)) return false;
    if (!material) material = qa_material_find(assets->options.provider.materials, "*default");
    if (!material) return q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 effect has no default material");
    qa_scene_fog_volume fog = {0};
    if (!options->world.no_world) qa_scene_world_fog_for_sphere(p->world, entity->origin, entity->radius, &fog);
    size_t first = p->frame->command_count;
    if (entity->kind == QA_Q3_REF_BEAM) {
        if (!qa_scene_q3_beam(p->frame, &options->world.view, entity->origin, entity->old_origin,
                options->world.source_scratch && options->world.source_diagnostics.no_bind ? material->dlight_image :
                qa_scene_white(assets->options.provider.images), &options->state, error)) return false;
        if (options->world.source_scratch) {
            for (size_t i = first; i < p->frame->command_count; ++i)
                if (p->frame->commands[i].kind == QA_SCENE_COMMAND_DRAW)
                    p->frame->commands[i].data.draw.source_direct = QA_SOURCE_DIRECT_BEAM;
            qa_material_context context = effect_context(options);
            if (source_time) { context.seconds = (float)*source_time * .001f; context.milliseconds = *source_time; }
            context.entity = order; context.entity_color = q3p_color(entity->color);
            context.source_entity_cell = options->world.source_entity_cells && !p->submission;
            context.entity_texcoord = entity->shader_texcoord; context.time_offset = entity->shader_time;
            material_fog(&context, &fog);
            if (!qa_material_source_commands(material, &context, p->frame, first, error)) return false;
        }
    } else {
        qa_scene_mesh mesh;
        qa_scene_vec4 color = q3p_color(entity->color);
        if (entity->kind == QA_Q3_REF_SPRITE) {
            if (!qa_scene_sprite_geometry(p->frame, &options->world.view, entity->origin,
                    entity->radius, entity->rotation, color, &mesh, error)) return false;
        } else {
            qa_scene_rail_kind kind = entity->kind == QA_Q3_REF_RAIL_CORE ? QA_RAIL_CORE :
                entity->kind == QA_Q3_REF_RAIL_RINGS ? QA_RAIL_RINGS : QA_RAIL_LIGHTNING;
            qa_scene_rail_options rail = options->rail;
            if (options->world.source_scratch && !qa_material_source_vertices(options->world.source_scratch,
                &rail.retained_vertices, &rail.retained_count, error)) return false;
            if (!qa_scene_rail_geometry(p->frame, &options->world.view, kind,
                    entity->origin, entity->old_origin, color, &rail, &mesh, error)) return false;
        }
        qa_material_context context = effect_context(options);
        if (source_time) {
            context.seconds = (float)*source_time * .001f;
            context.milliseconds = *source_time;
        }
        if (entity->kind == QA_Q3_REF_RAIL_CORE || entity->kind == QA_Q3_REF_RAIL_RINGS ||
            entity->kind == QA_Q3_REF_LIGHTNING) context.source_writer = QA_SOURCE_WRITE_RAIL;
        context.entity = order; context.entity_color = color;
        context.source_entity_cell = options->world.source_entity_cells && !p->submission;
        context.entity_texcoord = entity->shader_texcoord; context.time_offset = entity->shader_time;
        material_fog(&context, &fog);
        if (!qa_material_submit(material, &mesh, &context, p->frame, error)) return false;
    }
    return qa_scene_frame_group(p->frame, first, QA_SCENE_GROUP_SOURCE, material,
        material->sort, order, fog.index, 0, error);
}

bool qa_q3_presentation_selected_binding_read(const qa_q3_presentation *p,
    const qa_q3_scene_options *options, const qa_scene_frame *frame,
    qa_q3_presentation_binding *out, qa_error *error)
{
    if (!p || !options || !frame || !out || !p->busy || p->submission != options ||
        p->frame != frame || !p->options.assets || p->options.assets->busy != 1 ||
        p->options.assets->capturing || p->options.assets->codec_busy ||
        !q3p_assets_children_idle(p->options.assets) ||
        p->world != p->options.assets->world || p->geometry != p->options.assets->geometry ||
        (!p->world != !p->geometry) || (p->entity_text.size && !p->entity_text.data))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Selected binding requires its actual active view and installed map aliases");
    *out = (qa_q3_presentation_binding){p->options, p->frame, p->world, p->geometry, p->entity_text};
    return true;
}

static bool selected_effect_ready(qa_q3_presentation *p, const qa_q3_presentation_assets *assets,
    const qa_q3_scene_options *options, qa_scene_frame *frame, qa_error *error)
{
    if (!p || !assets || !options || !frame || !p->busy || p->submission != options ||
        p->frame != frame || (assets != p->options.assets ? !qa_q3_assets_idle(assets) :
            assets->busy != 1 || assets->capturing || assets->codec_busy || !q3p_assets_children_idle(assets)))
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Selected effect requires its actual registry and active view lease");
    return true;
}

bool qa_q3_presentation_selected_effect(qa_q3_presentation *p,
    const qa_q3_presentation_assets *assets, const qa_q3_ref_entity *entity,
    int32_t source_time_ms, const qa_q3_scene_options *options, uint32_t order, qa_scene_frame *frame, qa_error *error)
{
    if (!selected_effect_ready(p, assets, options, frame, error)) return false;
    if (!entity || entity->kind < QA_Q3_REF_MODEL || entity->kind == QA_Q3_REF_POLY ||
        entity->kind > QA_Q3_REF_LIGHTNING || order >= 1022)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Selected effect requires a genuine captured refEntity");
    if (entity->kind != QA_Q3_REF_MODEL && (entity->flags & 2) && !options->world.view.clip_enabled) return true;
    qa_q3_ref_entity placed = *entity;
    if ((placed.flags & 4) && (options->world.view.clip_enabled ||
        (!options->world.no_world && options->supplemental_weapon))) return true;
    if (!options->world.no_world && (placed.flags & 4)) {
        placed.origin = qa_vec_add(placed.origin, options->weapon_offset);
        placed.old_origin = qa_vec_add(placed.old_origin, options->weapon_offset);
        placed.lighting_origin = qa_vec_add(placed.lighting_origin, options->weapon_offset);
        placed.shadow_plane += options->weapon_offset.z;
    }
    if (placed.kind == QA_Q3_REF_MODEL) {
        const q3p_model *model;
        if (!q3p_model_get(assets, placed.model, &model, error)) return false;
        if (model && model->world)
            return q3p_fail(error, QA_ERROR_FORMAT, "Selected effect has an inline world model instead of its actual effect holder");
        return submit_model(p, assets, assets, assets, options, &placed, order, &source_time_ms, true, error);
    }
    return submit_effect(p, assets, options, &placed, order, &source_time_ms, error);
}

static bool submit_registered_poly(qa_q3_presentation *p,
    const qa_q3_presentation_assets *assets, int32_t shader,
    const qa_scene_vertex *vertices, size_t count,const qa_scene_fog_volume *source_fog,
    int32_t source_time_ms, const qa_q3_scene_options *options,
    qa_scene_frame *frame, qa_error *error)
{
    if (!selected_effect_ready(p, assets, options, frame, error)) return false;
    if (count && !vertices) return q3p_fail(error, QA_ERROR_ARGUMENT, "Selected polygon has no actual vertex span");
    if (!shader) {
        if (p->options.print) p->options.print(p->options.context, "^3WARNING: RE_AddPolyToScene: NULL poly shader\n");
        return true;
    }
    const qa_material *material;
    if (!q3p_shader_get(assets, shader, &material, error)) return false;
    if (!material) material = qa_material_find(assets->options.provider.materials, "*default");
    if (!material) return q3p_fail(error, QA_ERROR_ARGUMENT, "Selected polygon has no default material");
    if (!count) return true;
    qa_bounds bounds = {vertices[0].position, vertices[0].position};
    for (size_t i = 0; i < count; ++i)
        bounds = qa_bounds_union(bounds, (qa_bounds){vertices[i].position, vertices[i].position});
    qa_scene_fog_volume fog = source_fog?*source_fog:(qa_scene_fog_volume){0};
    if (!source_fog) qa_scene_world_fog_for_bounds(p->world, bounds, &fog);
    qa_scene_mesh mesh;
    if (!qa_scene_poly_geometry(frame, vertices, count, &mesh, error)) return false;
    qa_material_context context = effect_context(options);
    context.seconds = (float)source_time_ms * .001f; context.milliseconds = source_time_ms;
    context.source_writer = QA_SOURCE_WRITE_POLY;
    context.entity = 1022; material_fog(&context, &fog);
    size_t first = frame->command_count;
    if (!qa_material_submit(material, &mesh, &context, frame, error)) return false;
    return qa_scene_frame_group(frame, first, QA_SCENE_GROUP_SOURCE, material,
        material->sort, 1022, fog.index, 0, error);
}

bool qa_q3_presentation_selected_poly(qa_q3_presentation *p,
    const qa_q3_presentation_assets *assets,int32_t shader,const qa_scene_vertex *vertices,size_t count,
    int32_t source_time_ms,const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *error)
{
    return submit_registered_poly(p,assets,shader,vertices,count,NULL,source_time_ms,options,frame,error);
}
static bool source_component_ready(qa_q3_presentation *p,const qa_q3_presentation_assets *assets,
    const qa_q3_scene_options *options,qa_scene_frame *frame,qa_error *error)
{
    const qa_q3_presentation_assets *primary=p?p->options.assets:NULL;
    if (!primary || assets==primary || primary->busy!=1 || primary->capturing || primary->codec_busy ||
        !q3p_assets_children_idle(primary) || !selected_effect_ready(p,assets,options,frame,error))
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Source component lost its primary view and actual private registry");
    return true;
}
bool qa_q3_presentation_source_component_entity(qa_q3_presentation *p,
    const qa_q3_presentation_assets *assets,const qa_q3_ref_entity *entity,int32_t source_time_ms,
    const qa_q3_scene_options *options,uint32_t order,qa_scene_frame *frame,qa_error *error)
{
    if (!source_component_ready(p,assets,options,frame,error)) return false;
    if (!entity || entity->kind<QA_Q3_REF_MODEL || entity->kind>QA_Q3_REF_LIGHTNING ||
        entity->kind==QA_Q3_REF_POLY || order>=1022)
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Source component requires its genuine completed entity row");
    if (entity->kind==QA_Q3_REF_PORTAL ||
        ((entity->flags&4) && options->world.view.clip_enabled) ||
        (entity->kind!=QA_Q3_REF_MODEL && (entity->flags&2) && !options->world.view.clip_enabled)) return true;
    if (entity->kind==QA_Q3_REF_MODEL)
        return submit_model(p,assets,assets,assets,options,entity,order,&source_time_ms,true,error);
    return submit_effect(p,assets,options,entity,order,&source_time_ms,error);
}
bool qa_q3_presentation_source_component_poly(qa_q3_presentation *p,
    const qa_q3_presentation_assets *assets,int32_t shader,const qa_scene_vertex *vertices,size_t count,
    const qa_scene_fog_volume *source_fog,int32_t source_time_ms,const qa_q3_scene_options *options,
    qa_scene_frame *frame,qa_error *error)
{
    return source_fog && source_component_ready(p,assets,options,frame,error) &&
        submit_registered_poly(p,assets,shader,vertices,count,source_fog,source_time_ms,options,frame,error);
}
bool qa_q3_presentation_selected_world_models(qa_q3_presentation *p,
    const qa_q3_presentation_assets *assets, const qa_q3_ref_entity *entities, size_t count,
    const qa_q3_scene_options *options, qa_scene_frame *frame, qa_error *error)
{
    if (!selected_effect_ready(p, assets, options, frame, error)) return false;
    if (options->world.no_world || (count && !entities) || count > 65537)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "World model supplement requires its actual world and bounded model span");
    for (size_t i = 0; i < count; ++i) {
        const q3p_model *model;
        if (entities[i].kind != QA_Q3_REF_MODEL || entities[i].model <= 0 || (entities[i].flags & 4))
            return q3p_fail(error, QA_ERROR_ARGUMENT, "World model supplement has no genuine world model ref");
        if (!q3p_model_get(assets, entities[i].model, &model, error)) return false;
        if (!model || model->world)
            return q3p_fail(error, QA_ERROR_FORMAT, "World model supplement requires its retained non-brush model holder");
    }
    size_t commands = frame->command_count, groups = frame->group_count;
    for (size_t i = 0; i < count; ++i) {
        if (!submit_model(p, assets, assets, assets, options, &entities[i], (uint32_t)i,
                NULL, false, error) ||
            !selected_effect_ready(p, assets, options, frame, error)) {
            frame->command_count = commands; frame->group_count = groups;
            return false;
        }
    }
    return true;
}

bool qa_q3_presentation_selected_world_beam(qa_q3_presentation *p,
    const qa_q3_presentation_assets *assets, const qa_material *material, qa_vec3 origin, qa_vec3 end,
    double width, const qa_q3_scene_options *options,
    qa_scene_frame *frame, qa_error *error)
{
    if (!selected_effect_ready(p, assets, options, frame, error)) return false;
    if (options->world.no_world || !material || !qa_vec_finite(origin) || !qa_vec_finite(end) ||
        !isfinite(width) || trunc(width) != width || width < INT32_MIN || width > INT32_MAX)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "World shader cable requires its retained material, endpoints and source int32 width");
    const qa_material_library *library = assets->options.provider.materials;
    bool retained = false;
    for (size_t i = 0; i < qa_material_library_record_count(library); ++i) {
        qa_material_library_record_view record;
        if (qa_material_library_record_read(library, i, &record) && record.material == material) {
            retained = record.kind == QA_MATERIAL_DYNAMIC && record.lightmap_index == -1;
            break;
        }
    }
    if (!retained) return q3p_fail(error, QA_ERROR_FORMAT, "World shader cable material is not its retained dynamic registration");
    qa_scene_rail_options rail = {.core_width = (int32_t)width, .ring_width = 16, .segment_length = 32};
    if (options->world.source_scratch && !qa_material_source_vertices(options->world.source_scratch,
        &rail.retained_vertices, &rail.retained_count, error)) return false;
    qa_scene_mesh mesh;
    if (!qa_scene_rail_geometry(frame, &options->world.view, QA_RAIL_CORE, end, origin,
            (qa_scene_vec4){1, 1, 1, 1}, &rail, &mesh, error)) return false;
    qa_material_context context = effect_context(options);
    size_t commands = frame->command_count, groups = frame->group_count;
    context.source_writer = QA_SOURCE_WRITE_RAIL;
    if (qa_material_submit(material, &mesh, &context, frame, error) &&
        selected_effect_ready(p, assets, options, frame, error) &&
        qa_scene_frame_group(frame, commands, QA_SCENE_GROUP_SEQUENCE, NULL, 9, 0, 0, 0, error)) return true;
    frame->command_count = commands; frame->group_count = groups;
    return false;
}

static bool submit_queued_surfaces(qa_q3_presentation *p, const qa_q3_scene_options *options,size_t entity_count,qa_error *error)
{
    if (!submit_polygons(p, options, error)) return false;
    for (size_t i = 0; !options->no_entities && i < entity_count; ++i) {
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
        const qa_q3_presentation_assets *assets = p->source_entity_assets ? p->source_entity_assets[i] : p->options.assets;
        if (entity.kind == QA_Q3_REF_MODEL) {
            if (!submit_model(p, assets, assets, assets, options, &entity, order, NULL, true, error)) return false;
        } else if (!(entity.flags & 2) || options->world.view.clip_enabled) {
            if (!submit_effect(p, assets, options, &entity, order, NULL, error)) return false;
        }
    }
    if (p->options.submit_view) {
        p->submission = options;
        bool ok = p->options.submit_view(p->options.context, options, p->frame, error);
        p->submission = NULL;
        if (!ok) return false;
    }
    return true;
}
static bool submit_view_surfaces(qa_q3_presentation *p, const qa_q3_scene_options *options, qa_error *error)
{
    if (p->world) {
        if (!qa_scene_world_submit(p->world, &options->world, p->frame, error)) return false;
    } else {
        qa_scene_command command = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = options->world.view};
        if (!qa_scene_frame_emit(p->frame, &command, error)) return false;
    }
    return submit_queued_surfaces(p, options,p->entity_count,error);
}
bool qa_q3_presentation_supplement(qa_q3_presentation *p,qa_q3_source_scene_bank *bank,const qa_q3_scene_options *options,
    qa_scene_frame *frame, qa_error *error)
{
    if (!p || !bank || !options || !frame || p->frame!=frame || !frame->command_count || !qa_q3_presentation_idle(p) ||
        options->world.view.seat!=p->options.seat || !qa_vec_finite(options->world.view.origin) ||
        !options->world.view.viewport.width || !options->world.view.viewport.height)
        return q3p_fail(error,QA_ERROR_ARGUMENT,"Q3 supplement requires its actual unfinished parent view and registry");
    qa_q3_scene_options admitted_options=*options;size_t admitted=0;
    if(!qa_q3_source_scene_bank_entity_range(bank,p->options.assets,p->entities,p->entity_count,
        &admitted_options.first_entity,&admitted,error))return false;
    if (!q3p_begin(p,error)) return false;
    uint32_t milliseconds=(uint32_t)(uint64_t)options->world.milliseconds;
    memcpy(&p->render_milliseconds,&milliseconds,sizeof(milliseconds));
    bool okay=submit_queued_surfaces(p,&admitted_options,admitted,error);
    return q3p_end(p,okay);
}
bool qa_q3_presentation_lights_read(const qa_q3_presentation *p,const qa_scene_light **out,size_t *count,qa_error *e)
{
    if (!p || !out || !count || !qa_q3_presentation_idle(p))
        return q3p_fail(e,QA_ERROR_ARGUMENT,"Q3 lights require their returned actual presentation owner");
    *out=p->lights; *count=p->light_count; return true;
}

static bool submit_view(qa_q3_presentation *p, const qa_q3_scene_options *options, qa_error *error)
{
    qa_material_source_scratch *source = options->world.source_scratch;
    if (source && !qa_material_source_scene_begin(source, p->frame, error)) return false;
    if (source && !qa_material_source_scene_view(source, &options->world, error)) {
        qa_error cleanup = {0};
        (void)qa_material_source_scene_end(source, p->frame, false, &cleanup);
        return false;
    }
    if (source && options->world.source_hyperspace) {
        qa_scene_command view = {.kind = QA_SCENE_COMMAND_VIEW, .data.view = options->world.view};
        bool drawn = qa_scene_frame_emit(p->frame, &view, error);
        qa_error cleanup = {0};
        if (!qa_material_source_scene_end(source, p->frame, drawn, drawn ? error : &cleanup)) drawn = false;
        return drawn;
    }
    bool ok = true;
    if (source) {
        qa_material_context sky = {0};
        ok = qa_scene_world_source_sky_context(p->world, p->options.assets->options.provider.materials,
            &options->world, p->frame, &sky, error) && qa_material_source_scene_sky(source, &sky, error);
    }
    if (ok) ok = submit_view_surfaces(p, options, error);
    if (source) {
        qa_error cleanup = {0};
        if (!qa_material_source_scene_end(source, p->frame, ok, ok ? error : &cleanup)) ok = false;
    }
    if (!ok || !qa_scene_frame_finish(p->frame, &options->world.view, &options->world.fog, error)) return false;
    return options->shadow_mode != 2 || (source && options->world.source_diagnostics.stencil_bits < 4) ||
        (source ? qa_scene_source_stencil_finish : qa_scene_stencil_finish)(p->frame, &options->world.view,
        qa_scene_white(p->options.assets->options.provider.images), error);
}

bool qa_q3_presentation_render(qa_q3_presentation *p, const qa_q3_refdef *refdef, qa_error *error)
{
    if (!refdef || !q3p_begin(p, error)) return false;
    if (!p->frame) return q3p_end(p, q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 scene has no actual frame owner"));
    if (!source_scene_snapshot(p, error)) return q3p_end(p, false);
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
    qa_q3_scene_options options = {.world_family = QA_SCENE_Q3, .shadow_mode = p->options.shadow_mode,
        .lod_scale = p->options.lod_scale, .lod_bias = p->options.lod_bias,
        .ambient_scale = .6f, .directed_scale = 1, .near_clip = p->options.near_clip,
        .rail = {.core_width = p->options.rail_core_width, .ring_width = p->options.rail_ring_width,
            .segment_length = p->options.rail_segment_length}, .world = {
        .view = {.viewport = {(int32_t)x, (int32_t)y, (uint32_t)refdef->width, (uint32_t)refdef->height},
            .origin = refdef->origin, .axis = {refdef->axis[0], refdef->axis[1], refdef->axis[2]},
            .projection = qa_scene_projection(refdef->fov_x, refdef->fov_y, p->options.near_clip, p->options.far_clip),
            .clear_depth = true, .clear_stencil = p->options.shadow_mode == 2, .depth = 1, .seat = p->options.seat},
        .seconds = (float)refdef->time * .001f, .milliseconds = refdef->time,
        .no_world = (refdef->flags & 1) != 0, .source_hyperspace = (refdef->flags & 4) != 0,
        .visible_areas = visible, .visible_area_bytes = sizeof(visible),
        .lights = p->lights, .light_count = p->light_count, .use_projected_lights = true,
        .projected_lights = p->lights, .projected_light_count = p->light_count < 32 ? p->light_count : 32, .source_order = true,
        .identity_light = p->options.identity_light, .curve_error = 250,
        .render_texts = texts, .render_text_count = 8, .video_frame = p->options.video_frame,
        .video_context = p->options.video_context, .source_scratch = p->options.source_scratch,
        .source_white = qa_scene_white(p->options.assets->options.provider.images),
        .source_diagnostics = {.polygon_offset_factor = -1, .polygon_offset_units = -2}}};
    size_t first = p->frame->command_count;
    qa_scene_state_default(&options.state);
    p->render_milliseconds = refdef->time;
    if (p->options.prepare_view) ok = p->options.prepare_view(p->options.context, refdef, &options, error);
    if (ok && p->options.source_state) {
        options.first_entity = p->source_entity_first;
        options.world.source_entity_cells = true;
    }
    options.world.source_cluster_print = p->options.print;
    options.world.source_cluster_print_context = p->options.context;
    if (ok && options.no_refresh) return q3p_end(p, true);
    if (ok && options.world.source_scratch) {
        if (options.world.fast_sky && !options.world.no_world) {
            options.world.view.clear_color = true;
            options.world.view.color = (qa_scene_vec4){0, 0, 0, 1};
        }
        if (options.world.source_hyperspace) {
            float gray = (float)((uint32_t)refdef->time & 255u) / 255;
            options.world.view.clear_color = true;
            options.world.view.color = (qa_scene_vec4){gray, gray, gray, 1};
        }
    }
    if (ok && p->world) ok = qa_scene_world_source_begin_scene(p->world, &options.world, error);
    options.world.view.clear_stencil = options.shadow_mode == 2;
    if (ok && options.near_clip != p->options.near_clip)
        options.world.view.projection = qa_scene_projection(refdef->fov_x, refdef->fov_y,
            options.near_clip, p->options.far_clip);
    if (ok && (!options.world.no_world && (!p->world || !p->world_loaded)))
        ok = q3p_fail(error, QA_ERROR_ARGUMENT, "RE_RenderScene: NULL worldmodel");
    if (ok && (options.first_entity > 1022 || p->entity_count > 1022 - options.first_entity ||
        options.world.projected_light_count > 32 || (options.world.light_count && !options.world.lights) ||
        (options.world.projected_light_count && !options.world.projected_lights) ||
        !qa_vec_finite(options.weapon_offset)))
        ok = q3p_fail(error, QA_ERROR_ARGUMENT, "Q3 scene exceeds source entity/light limits or has invalid spans");
    if (ok && p->world) {
        ok = qa_scene_world_source_prepare_view(p->world, &options.world, p->frame, error);
        if (ok) {
            float far_clip = options.world.source_far_clip, near_clip = options.near_clip;
            options.world.view.projection.m[10] = -(far_clip + near_clip) / (far_clip - near_clip);
            options.world.view.projection.m[14] = -2 * far_clip * near_clip / (far_clip - near_clip);
        }
    } else if (ok && options.world.no_world) {
        float far_clip = 2048, near_clip = options.near_clip;
        options.world.source_far_clip = far_clip;
        options.world.view.projection.m[10] = -(far_clip + near_clip) / (far_clip - near_clip);
        options.world.view.projection.m[14] = -2 * far_clip * near_clip / (far_clip - near_clip);
    }
    size_t portal_count = 0;
    if (ok) ok = q3p_reserve((void **)&p->portals, &p->portal_capacity, p->entity_count, sizeof(*p->portals), error);
    for (size_t i = 0; ok && !options.no_entities && !options.no_portals &&
        options.world.fast_sky != 1 && i < p->entity_count; ++i) {
        const qa_q3_ref_entity *entity = &p->entities[i];
        if (entity->kind != QA_Q3_REF_PORTAL) continue;
        qa_scene_portal portal = {.origin = entity->origin, .old_origin = entity->old_origin,
            .axis = {entity->axis[0], entity->axis[1], entity->axis[2]},
            .rotation_speed = entity->old_frame && entity->frame ? (float)entity->frame : 0,
            .rotation_offset = (float)entity->skin, .oscillate = entity->old_frame && !entity->frame};
        p->portals[portal_count++] = portal;
    }
    bool portal_drawn = false;
    if (ok && p->world && !options.world.no_world && portal_count) {
        qa_scene_view child; qa_vec3 pvs; bool found;
        ok = qa_scene_world_portal_view(p->world, &options.world, p->portals, portal_count, &child, &pvs, &found, error);
        if (ok && found) {
            qa_q3_scene_options child_options = options;
            child_options.world.view = child; child_options.world.pvs_origin = pvs;
            child_options.world.use_pvs_origin = true;
            ok = qa_scene_world_source_prepare_view(p->world, &child_options.world, p->frame, error);
            if (ok) {
                float far_clip = child_options.world.source_far_clip, near_clip = child_options.near_clip;
                child_options.world.view.projection.m[10] = -(far_clip + near_clip) / (far_clip - near_clip);
                child_options.world.view.projection.m[14] = -2 * far_clip * near_clip / (far_clip - near_clip);
                ok = submit_view(p, &child_options, error);
            }
            portal_drawn = ok;
        }
    }
    if (ok && !(options.portal_only && portal_drawn)) ok = submit_view(p, &options, error);
    if (ok) { p->material_view = options.world.view; p->material_view_valid = true; }
    if (ok && p->options.scene_completed)
        ok = p->options.scene_completed(p->options.context, refdef, &options,
            p->entities, p->entity_count, p->polygons, p->polygon_count,
            p->vertices, p->vertex_count, p->lights, p->light_count, error);
    if (ok && (p->options.source_state || p->options.source_scene_membership)) {
        if (p->options.source_state) {
            qa_material_source_scratch *source = p->options.source_state(p->options.context, error);
            ok = source && qa_material_source_entity_scene(source, &p->source_entity_first, error);
        }
        if (ok) p->entity_count = p->polygon_count = p->vertex_count = p->light_count = 0;
    }
    if (!ok) { p->frame->command_count = first; p->frame->group_count = 0; }
    p->source_entity_assets = p->source_polygon_assets = NULL;
    return q3p_end(p, ok);
}

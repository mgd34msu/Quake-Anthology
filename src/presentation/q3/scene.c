#include "internal.h"

qa_scene_vec4 q3p_color(const uint8_t bytes[4])
{
    return (qa_scene_vec4){bytes[0] / 255.0f, bytes[1] / 255.0f,
        bytes[2] / 255.0f, bytes[3] / 255.0f};
}

bool qa_q3_presentation_clear(qa_q3_presentation *p, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    p->entity_count = p->polygon_count = p->vertex_count = p->light_count = 0;
    if (p->options.scene_cleared) p->options.scene_cleared(p->options.context);
    return q3p_end(p, true);
}

bool qa_q3_presentation_entity(qa_q3_presentation *p, const qa_q3_ref_entity *entity,
                                qa_error *error)
{
    if (!entity || !q3p_begin(p, error)) return false;
    const qa_material *shader; const qa_model_skin_map *skin; const q3p_model *model;
    bool ok = entity->kind >= QA_Q3_REF_MODEL && entity->kind <= QA_Q3_REF_PORTAL;
    if (!ok) q3p_fail(error, QA_ERROR_FORMAT, "RE_AddRefEntityToScene: bad reType");
    if (ok && entity->kind != QA_Q3_REF_POLY && entity->kind != QA_Q3_REF_PORTAL)
        ok = q3p_shader_get(p->options.assets, entity->custom_shader, &shader, error);
    if (ok && entity->kind == QA_Q3_REF_MODEL)
        ok = q3p_model_get(p->options.assets, entity->model, &model, error) &&
             q3p_skin_get(p->options.assets, entity->custom_skin, &skin, error);
    if (ok && p->entity_count == SIZE_MAX) ok = q3p_fail(error, QA_ERROR_MEMORY, "Q3 scene entity count overflow");
    if (ok) ok = q3p_reserve((void **)&p->entities, &p->entity_capacity,
        p->entity_count + 1, sizeof(*p->entities), error);
    if (ok) p->entities[p->entity_count++] = *entity;
    return q3p_end(p, ok);
}

bool qa_q3_presentation_poly(qa_q3_presentation *p, int32_t shader,
                              const qa_q3_poly_vertex *vertices, size_t count, qa_error *error)
{
    if ((count && !vertices) || !q3p_begin(p, error)) return false;
    if (!shader) {
        if (p->options.print) p->options.print(p->options.context, "^3WARNING: RE_AddPolyToScene: NULL poly shader\n");
        return q3p_end(p, true);
    }
    const qa_material *material;
    bool ok = q3p_shader_get(p->options.assets, shader, &material, error);
    if (ok && (count > SIZE_MAX - p->vertex_count || p->polygon_count == SIZE_MAX))
        ok = q3p_fail(error, QA_ERROR_MEMORY, "Q3 scene polygon count overflow");
    if (ok) ok = q3p_reserve((void **)&p->vertices, &p->vertex_capacity,
        p->vertex_count + count, sizeof(*p->vertices), error) &&
        q3p_reserve((void **)&p->polygons, &p->polygon_capacity,
        p->polygon_count + 1, sizeof(*p->polygons), error);
    if (ok) {
        qa_bounds bounds = count ? (qa_bounds){vertices[0].position, vertices[0].position} : (qa_bounds){0};
        for (size_t i = 0; i < count; ++i) {
            p->vertices[p->vertex_count + i] = (qa_scene_vertex){.position = vertices[i].position,
                .texcoord = vertices[i].texcoord, .color = q3p_color(vertices[i].color)};
            bounds = qa_bounds_union(bounds, (qa_bounds){vertices[i].position, vertices[i].position});
        }
        q3p_polygon polygon = {.shader = shader, .first = p->vertex_count, .count = count};
        if (count) qa_scene_world_fog_for_bounds(p->world, bounds, &polygon.fog);
        p->polygons[p->polygon_count++] = polygon; p->vertex_count += count;
    }
    return q3p_end(p, ok);
}

bool qa_q3_presentation_light(qa_q3_presentation *p, qa_vec3 origin, float radius,
                               qa_vec3 color, bool additive, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    if (radius <= 0) return q3p_end(p, true);
    bool ok = p->light_count < SIZE_MAX;
    if (!ok) q3p_fail(error, QA_ERROR_MEMORY, "Q3 scene light count overflow");
    if (ok) ok = q3p_reserve((void **)&p->lights, &p->light_capacity,
        p->light_count + 1, sizeof(*p->lights), error);
    if (ok) p->lights[p->light_count++] = (qa_scene_light){.origin = origin, .radius = radius,
        .color = color, .additive = additive, .family = QA_SCENE_Q3};
    return q3p_end(p, ok);
}

void qa_q3_presentation_color(qa_q3_presentation *p, const qa_scene_vec4 *color)
{
    qa_error ignored = {0};
    if (!q3p_begin(p, &ignored)) return;
    p->color = color ? *color : (qa_scene_vec4){1, 1, 1, 1};
    q3p_end(p, true);
}

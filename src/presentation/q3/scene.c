#include "internal.h"
#include "qa/q3_source_scene_bank.h"

qa_scene_vec4 q3p_color(const uint8_t bytes[4])
{
    return (qa_scene_vec4){bytes[0] / 255.0f, bytes[1] / 255.0f,
        bytes[2] / 255.0f, bytes[3] / 255.0f};
}

bool qa_q3_presentation_clear(qa_q3_presentation *p, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    if (p->options.source_state) {
        qa_material_source_scratch *source = p->options.source_state(p->options.context, error);
        if (!source || !qa_material_source_entity_scene(source, &p->source_entity_first, error))
            return q3p_end(p, false);
    }
    p->entity_count = p->polygon_count = p->vertex_count = p->light_count = 0;
    if (p->options.scene_cleared) p->options.scene_cleared(p->options.context);
    return q3p_end(p, true);
}

bool qa_q3_presentation_entity(qa_q3_presentation *p, const qa_q3_ref_entity *entity,
                                qa_error *error)
{
    if (!entity || !q3p_begin(p, error)) return false;
    qa_material_source_scratch *source = NULL;
    if (p->options.source_state) {
        bool available = false;
        source = p->options.source_state(p->options.context, error);
        qa_q3_source_scene_bank *bank = source ? qa_material_source_scene_bank(source, error) : NULL;
        if (!bank) return q3p_end(p, false);
        available = qa_q3_source_scene_bank_entity_capacity(bank);
        if (!available) return q3p_end(p, true);
        if (entity->kind < QA_Q3_REF_MODEL || entity->kind > QA_Q3_REF_PORTAL)
            return q3p_end(p, q3p_fail(error, QA_ERROR_FORMAT, "RE_AddRefEntityToScene: bad reType"));
        uint32_t ordinal; bool admitted;
        return q3p_end(p, qa_q3_source_scene_bank_entity(bank, p->options.assets, entity,
            &ordinal, &admitted, error));
    }
    const qa_material *shader; const qa_model_skin_map *skin; const q3p_model *model;
    bool ok = entity->kind >= QA_Q3_REF_MODEL && entity->kind <= QA_Q3_REF_PORTAL;
    if (!ok) q3p_fail(error, QA_ERROR_FORMAT, "RE_AddRefEntityToScene: bad reType");
    bool source_scene = p->options.source_state || p->options.source_scene_membership;
    if (ok && !source_scene && entity->kind != QA_Q3_REF_POLY && entity->kind != QA_Q3_REF_PORTAL)
        ok = q3p_shader_get(p->options.assets, entity->custom_shader, &shader, error);
    if (ok && !source_scene && entity->kind == QA_Q3_REF_MODEL)
        ok = q3p_model_get(p->options.assets, entity->model, &model, error) &&
             q3p_skin_get(p->options.assets, entity->custom_skin, &skin, error);
    if (ok && p->entity_count == SIZE_MAX) ok = q3p_fail(error, QA_ERROR_MEMORY, "Q3 scene entity count overflow");
    if (ok) ok = q3p_reserve((void **)&p->entities, &p->entity_capacity,
        p->entity_count + 1, sizeof(*p->entities), error);
    bool admitted = true;
    if (ok && admitted) p->entities[p->entity_count++] = *entity;
    return q3p_end(p, ok);
}

bool qa_q3_presentation_entity_cursor(const qa_q3_presentation *p, size_t *index,
    bool *available, qa_error *error)
{
    if (!p || !index || !available)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Source entity cursor requires its actual presentation");
    if (p->options.source_state) {
        qa_material_source_scratch *source = p->options.source_state(p->options.context, error);
        qa_q3_source_scene_bank *bank = source ? qa_material_source_scene_bank(source, error) : NULL;
        qa_q3_source_scene_membership membership;
        if (!bank || !qa_q3_source_scene_bank_membership(bank, &membership)) return false;
        if (membership.first_entity > membership.entities)
            return q3p_fail(error, QA_ERROR_FORMAT, "Source entity cursor leaves its actual scene membership");
        *index = membership.entities - membership.first_entity;
        *available = qa_q3_source_scene_bank_entity_capacity(bank);
    } else {
        *index = p->entity_count; *available = p->entity_count != SIZE_MAX;
    }
    return true;
}

bool qa_q3_presentation_poly(qa_q3_presentation *p, int32_t shader,
                              const qa_q3_poly_vertex *vertices, size_t count, qa_error *error)
{
    if ((count && !vertices) || !q3p_begin(p, error)) return false;
    if (!shader) {
        if (p->options.print) p->options.print(p->options.context, "^3WARNING: RE_AddPolyToScene: NULL poly shader\n");
        return q3p_end(p, true);
    }
    if (p->options.source_state) {
        qa_material_source_scratch *source = p->options.source_state(p->options.context, error);
        qa_q3_source_scene_bank *bank = source ? qa_material_source_scene_bank(source, error) : NULL;
        if (!bank) return q3p_end(p, false);
        if (!qa_q3_source_scene_bank_poly_capacity(bank, count)) return q3p_end(p, true);
        qa_scene_fog_volume fog = {0};
        if (count) {
            qa_bounds bounds = {vertices[0].position, vertices[0].position};
            for (size_t i = 1; i < count; ++i)
                bounds = qa_bounds_union(bounds, (qa_bounds){vertices[i].position, vertices[i].position});
            qa_scene_world_fog_for_bounds(p->world, bounds, &fog);
        }
        bool admitted;
        return q3p_end(p, qa_q3_source_scene_bank_poly(bank, p->options.assets,
            shader, vertices, count, &fog, &admitted, error));
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

bool qa_q3_presentation_poly_cursor(const qa_q3_presentation *p, size_t vertices,
    size_t *index, bool *available, qa_error *error)
{
    if (!p || !index || !available)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Source polygon cursor requires its actual presentation");
    if (p->options.source_state) {
        qa_material_source_scratch *source = p->options.source_state(p->options.context, error);
        qa_q3_source_scene_bank *bank = source ? qa_material_source_scene_bank(source, error) : NULL;
        qa_q3_source_scene_membership membership;
        if (!bank || !qa_q3_source_scene_bank_membership(bank, &membership)) return false;
        if (membership.first_polygon > membership.polygons)
            return q3p_fail(error, QA_ERROR_FORMAT, "Source polygon cursor leaves its actual scene membership");
        *index = membership.polygons - membership.first_polygon;
        *available = qa_q3_source_scene_bank_poly_capacity(bank, vertices);
    } else {
        *index = p->polygon_count;
        *available = p->polygon_count != SIZE_MAX && vertices <= SIZE_MAX - p->vertex_count;
    }
    return true;
}

bool qa_q3_presentation_light_cursor(const qa_q3_presentation *p, size_t *index,
    bool *available, qa_error *error)
{
    if (!p || !index || !available)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Source light cursor requires its actual presentation");
    if (p->options.source_state) {
        qa_material_source_scratch *source = p->options.source_state(p->options.context, error);
        qa_q3_source_scene_bank *bank = source ? qa_material_source_scene_bank(source, error) : NULL;
        qa_q3_source_scene_membership membership;
        if (!bank || !qa_q3_source_scene_bank_membership(bank, &membership)) return false;
        if (membership.first_light > membership.lights)
            return q3p_fail(error, QA_ERROR_FORMAT, "Source light cursor leaves its actual scene membership");
        *index = membership.lights - membership.first_light;
        *available = qa_q3_source_scene_bank_light_capacity(bank);
    } else {
        *index = p->light_count; *available = p->light_count != SIZE_MAX;
    }
    return true;
}

bool qa_q3_presentation_light(qa_q3_presentation *p, qa_vec3 origin, float radius,
                               qa_vec3 color, bool additive, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    qa_material_source_scratch *source = NULL;
    if (p->options.source_state) {
        source = p->options.source_state(p->options.context, error);
        qa_q3_source_scene_bank *bank = source ? qa_material_source_scene_bank(source, error) : NULL;
        if (!bank) return q3p_end(p, false);
        if (!qa_q3_source_scene_bank_light_capacity(bank)) return q3p_end(p, true);
        if (radius <= 0) return q3p_end(p, true);
        qa_scene_light value = {.origin = origin, .radius = radius, .color = color,
            .additive = additive, .family = QA_GAME_Q3};
        bool admitted;
        return q3p_end(p, qa_q3_source_scene_bank_light(bank, p->options.assets, &value, &admitted, error));
    }
    if (radius <= 0) return q3p_end(p, true);
    bool ok = p->light_count < SIZE_MAX;
    if (!ok) q3p_fail(error, QA_ERROR_MEMORY, "Q3 scene light count overflow");
    if (ok) ok = q3p_reserve((void **)&p->lights, &p->light_capacity,
        p->light_count + 1, sizeof(*p->lights), error);
    bool admitted = true;
    if (ok && admitted) p->lights[p->light_count++] = (qa_scene_light){.origin = origin, .radius = radius,
        .color = color, .additive = additive, .family = QA_GAME_Q3};
    return q3p_end(p, ok);
}

void qa_q3_presentation_color(qa_q3_presentation *p, const qa_scene_vec4 *color)
{
    qa_error ignored = {0};
    if (!q3p_begin(p, &ignored)) return;
    p->color = color ? *color : (qa_scene_vec4){1, 1, 1, 1};
    q3p_end(p, true);
}

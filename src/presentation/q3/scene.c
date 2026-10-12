#include "internal.h"
#include "qa/q3_source_scene_bank.h"

qa_vec4 q3p_color(const uint8_t bytes[4])
{
    return (qa_vec4){bytes[0] / 255.0f, bytes[1] / 255.0f,
        bytes[2] / 255.0f, bytes[3] / 255.0f};
}

qa_q3_source_scene_bank *q3p_scene_bank(const qa_q3_presentation *p, qa_error *error)
{
    if (p->options.source_state) {
        qa_material_source_scratch *source = p->options.source_state(p->options.context, error);
        return source ? qa_material_source_scene_bank(source, error) : NULL;
    }
    return p->scene_bank;
}

void q3p_scene_scratch_clear(qa_q3_presentation *p)
{
    p->entities = NULL; p->polygons = NULL; p->vertices = NULL; p->lights = NULL;
    p->source_entity_assets = p->source_polygon_assets = NULL;
    p->entity_count = p->polygon_count = p->vertex_count = p->light_count = 0;
}

bool qa_q3_presentation_clear(qa_q3_presentation *p, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    if (p->options.source_state) {
        qa_material_source_scratch *source = p->options.source_state(p->options.context, error);
        if (!source || !qa_material_source_entity_scene(source, &p->source_entity_first, error))
            return q3p_end(p, false);
    } else {
        qa_q3_source_scene_bank_clear(p->scene_bank);
        qa_q3_source_scene_membership membership;
        if (!qa_q3_source_scene_bank_membership(p->scene_bank, &membership)) return q3p_end(p, false);
        p->source_entity_first = membership.first_entity;
    }
    q3p_scene_scratch_clear(p);
    if (p->options.scene_cleared) p->options.scene_cleared(p->options.context);
    return q3p_end(p, true);
}

bool qa_q3_presentation_entity(qa_q3_presentation *p, const qa_q3_ref_entity *entity,
    qa_error *error)
{
    if (!entity || !q3p_begin(p, error)) return false;
    qa_q3_source_scene_bank *bank = q3p_scene_bank(p, error);
    if (!bank) return q3p_end(p, false);
    if (!qa_q3_source_scene_bank_entity_capacity(bank)) return q3p_end(p, true);
    bool ok = entity->kind >= QA_Q3_REF_MODEL && entity->kind <= QA_Q3_REF_PORTAL;
    if (!ok) q3p_fail(error, QA_ERROR_FORMAT, "RE_AddRefEntityToScene: bad reType");
    bool source_scene = p->options.source_state || p->options.source_scene_membership;
    const qa_material *shader; const qa_model_skin_map *skin; const q3p_model *model;
    if (ok && !source_scene && entity->kind != QA_Q3_REF_POLY && entity->kind != QA_Q3_REF_PORTAL)
        ok = q3p_shader_get(p->options.assets, entity->custom_shader, &shader, error);
    if (ok && !source_scene && entity->kind == QA_Q3_REF_MODEL)
        ok = q3p_model_get(p->options.assets, entity->model, &model, error) &&
            q3p_skin_get(p->options.assets, entity->custom_skin, &skin, error);
    uint32_t ordinal; bool admitted;
    if (ok) ok = qa_q3_source_scene_bank_entity(bank, p->options.assets, entity, &ordinal, &admitted, error);
    return q3p_end(p, ok);
}

bool qa_q3_presentation_entity_cursor(const qa_q3_presentation *p, size_t *index,
    bool *available, qa_error *error)
{
    if (!p || !index || !available)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Source entity cursor requires its actual presentation");
    qa_q3_source_scene_bank *bank = q3p_scene_bank(p, error);
    qa_q3_source_scene_membership membership;
    if (!bank || !qa_q3_source_scene_bank_membership(bank, &membership)) return false;
    *index = membership.entities - membership.first_entity;
    *available = qa_q3_source_scene_bank_entity_capacity(bank);
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
    qa_q3_source_scene_bank *bank = q3p_scene_bank(p, error);
    if (!bank) return q3p_end(p, false);
    if (!qa_q3_source_scene_bank_poly_capacity(bank, count)) return q3p_end(p, true);
    const qa_material *material;
    bool ok = p->options.source_state || q3p_shader_get(p->options.assets, shader, &material, error);
    qa_scene_fog_volume fog = {0};
    if (ok && count) {
        qa_bounds bounds = {vertices[0].position, vertices[0].position};
        for (size_t i = 1; i < count; ++i)
            bounds = qa_bounds_union(bounds, (qa_bounds){vertices[i].position, vertices[i].position});
        qa_scene_world_fog_for_bounds(p->world, bounds, &fog);
    }
    bool admitted;
    if (ok) ok = qa_q3_source_scene_bank_poly(bank, p->options.assets, shader, vertices, count, &fog, &admitted, error);
    return q3p_end(p, ok);
}

bool qa_q3_presentation_poly_cursor(const qa_q3_presentation *p, size_t vertices,
    size_t *index, bool *available, qa_error *error)
{
    if (!p || !index || !available)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Source polygon cursor requires its actual presentation");
    qa_q3_source_scene_bank *bank = q3p_scene_bank(p, error);
    qa_q3_source_scene_membership membership;
    if (!bank || !qa_q3_source_scene_bank_membership(bank, &membership)) return false;
    *index = membership.polygons - membership.first_polygon;
    *available = qa_q3_source_scene_bank_poly_capacity(bank, vertices);
    return true;
}

bool qa_q3_presentation_light_cursor(const qa_q3_presentation *p, size_t *index,
    bool *available, qa_error *error)
{
    if (!p || !index || !available)
        return q3p_fail(error, QA_ERROR_ARGUMENT, "Source light cursor requires its actual presentation");
    qa_q3_source_scene_bank *bank = q3p_scene_bank(p, error);
    qa_q3_source_scene_membership membership;
    if (!bank || !qa_q3_source_scene_bank_membership(bank, &membership)) return false;
    *index = membership.lights - membership.first_light;
    *available = qa_q3_source_scene_bank_light_capacity(bank);
    return true;
}

bool qa_q3_presentation_light(qa_q3_presentation *p, qa_vec3 origin, float radius,
    qa_vec3 color, bool additive, qa_error *error)
{
    if (!q3p_begin(p, error)) return false;
    qa_q3_source_scene_bank *bank = q3p_scene_bank(p, error);
    if (!bank) return q3p_end(p, false);
    if (!qa_q3_source_scene_bank_light_capacity(bank) || radius <= 0) return q3p_end(p, true);
    qa_scene_light value = {.origin = origin, .radius = radius, .color = color,
        .additive = additive, .family = QA_GAME_Q3};
    bool admitted;
    return q3p_end(p, qa_q3_source_scene_bank_light(bank, p->options.assets, &value, &admitted, error));
}

void qa_q3_presentation_color(qa_q3_presentation *p, const qa_vec4 *color)
{
    qa_error ignored = {0};
    if (!q3p_begin(p, &ignored)) return;
    p->color = color ? *color : (qa_vec4){1, 1, 1, 1};
    q3p_end(p, true);
}

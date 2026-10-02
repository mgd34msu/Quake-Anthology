#include "source_scene_bank_private.h"

static bool fail(qa_error *error, qa_status code, const char *message)
{ qa_error_set(error, code, 0, "%s", message); return false; }
bool qa_q3_source_scene_bank_create(uint32_t polygons, uint32_t vertices,
    qa_q3_source_scene_bank **out, qa_error *error)
{
    if (!out || *out || !polygons || !vertices ||
        sizeof(qa_q3_source_polygon_cell) > SIZE_MAX / polygons ||
        sizeof(qa_q3_poly_vertex) > SIZE_MAX / vertices)
        return fail(error, QA_ERROR_ARGUMENT, "Source scene bank requires real allocation limits and empty output");
    qa_q3_source_scene_bank *bank = calloc(1, sizeof(*bank));
    if (!bank) return fail(error, QA_ERROR_MEMORY, "Allocating Source scene bank");
    bank->cycle = 1;
    bank->membership.max_polygons = polygons; bank->membership.max_vertices = vertices;
    bank->polygons = calloc(polygons, sizeof(*bank->polygons));
    bank->vertices = calloc(vertices, sizeof(*bank->vertices));
    if (!bank->polygons || !bank->vertices) {
        qa_q3_source_scene_bank_destroy(bank);
        return fail(error, QA_ERROR_MEMORY, "Allocating Source polygon and vertex cells");
    }
    *out = bank; return true;
}
void qa_q3_source_scene_bank_destroy(qa_q3_source_scene_bank *bank)
{
    if (!bank) return;
    for (uint32_t i = 0; i < QA_Q3_SOURCE_ENTITY_CAPACITY; ++i)
        qa_q3_assets_release(bank->entities[i].assets);
    for (uint32_t i = 0; i < QA_Q3_SOURCE_LIGHT_CAPACITY; ++i)
        qa_q3_assets_release(bank->lights[i].assets);
    if (bank->polygons) for (uint32_t i = 0; i < bank->membership.max_polygons; ++i)
        qa_q3_assets_release(bank->polygons[i].assets);
    free(bank->polygons); free(bank->vertices); free(bank);
}
void qa_q3_source_scene_bank_frame(qa_q3_source_scene_bank *bank)
{
    if (!bank) return;
    if (bank->cycle) bank->cycle = bank->cycle == UINT64_MAX ? 0 : bank->cycle + 1;
    bank->membership.entities = bank->membership.first_entity = 0;
    bank->membership.polygons = bank->membership.first_polygon = bank->membership.vertices = 0;
    bank->membership.lights = bank->membership.first_light = 0;
}
bool qa_q3_source_scene_bank_cycle(const qa_q3_source_scene_bank *bank, uint64_t *out)
{
    if (!bank || !out || !bank->cycle) return false;
    *out = bank->cycle; return true;
}
void qa_q3_source_scene_bank_clear(qa_q3_source_scene_bank *bank)
{
    if (!bank) return;
    bank->membership.first_entity = bank->membership.entities;
    bank->membership.first_polygon = bank->membership.polygons;
    bank->membership.first_light = bank->membership.lights;
}
bool qa_q3_source_scene_bank_membership(const qa_q3_source_scene_bank *bank,
    qa_q3_source_scene_membership *out)
{ if (!bank || !out) return false; *out = bank->membership; return true; }
bool qa_q3_source_scene_bank_entity_capacity(const qa_q3_source_scene_bank *bank)
{ return bank && bank->cycle && bank->membership.entities < QA_Q3_SOURCE_ENTITY_LIMIT; }
bool qa_q3_source_scene_bank_poly_capacity(const qa_q3_source_scene_bank *bank, size_t vertices)
{ return bank && bank->cycle && bank->membership.polygons < bank->membership.max_polygons &&
    vertices <= bank->membership.max_vertices - bank->membership.vertices; }
bool qa_q3_source_scene_bank_light_capacity(const qa_q3_source_scene_bank *bank)
{ return bank && bank->cycle && bank->membership.lights < QA_Q3_SOURCE_LIGHT_CAPACITY; }
bool qa_q3_source_scene_bank_entity(qa_q3_source_scene_bank *bank,
    qa_q3_presentation_assets *assets, const qa_q3_ref_entity *value, uint32_t *ordinal,
    bool *admitted, qa_error *error)
{
    if (!bank || !value || !ordinal || !admitted)
        return fail(error, QA_ERROR_ARGUMENT, "Source entity admission requires its actual bank and value");
    if (!bank->cycle) return fail(error, QA_ERROR_ARGUMENT, "Source scene bank frame cycle is exhausted");
    *ordinal = bank->membership.entities; *admitted = false;
    if (*ordinal >= QA_Q3_SOURCE_ENTITY_LIMIT) return true;
    if ((unsigned)value->kind > QA_Q3_REF_PORTAL)
        return fail(error, QA_ERROR_FORMAT, "RE_AddRefEntityToScene: bad reType");
    if (!qa_q3_assets_retain(assets, error)) return false;
    qa_q3_source_entity_cell *cell = bank->entities + *ordinal;
    qa_q3_presentation_assets *previous = cell->assets;
    cell->assets = assets; cell->value = *value; cell->lighting_calculated = false;
    ++bank->membership.entities; *admitted = true;
    qa_q3_assets_release(previous); return true;
}
bool qa_q3_source_scene_bank_entity_range(qa_q3_source_scene_bank *bank,
    qa_q3_presentation_assets *assets, const qa_q3_ref_entity *values, size_t count,
    uint32_t *first, size_t *admitted, qa_error *error)
{
    if (!bank || !first || !admitted)
        return fail(error, QA_ERROR_ARGUMENT, "Source entity range requires its actual bank and outputs");
    if (!bank->cycle) return fail(error, QA_ERROR_ARGUMENT, "Source scene bank frame cycle is exhausted");
    *first = bank->membership.entities; *admitted = 0;
    while (*admitted < count && qa_q3_source_scene_bank_entity_capacity(bank)) {
        if (!values) return fail(error, QA_ERROR_ARGUMENT, "Source entity range lacks its actual incoming values");
        uint32_t ordinal; bool accepted;
        if (!qa_q3_source_scene_bank_entity(bank, assets, values + *admitted, &ordinal, &accepted, error)) return false;
        if (!accepted) break;
        ++*admitted;
    }
    return true;
}
bool qa_q3_source_scene_bank_poly(qa_q3_source_scene_bank *bank,
    qa_q3_presentation_assets *assets, int32_t shader, const qa_q3_poly_vertex *vertices,
    size_t count, const qa_scene_fog_volume *fog, bool *admitted, qa_error *error)
{
    if (!bank || !admitted || (count && !vertices))
        return fail(error, QA_ERROR_ARGUMENT, "Source polygon admission requires its actual bank and vertices");
    if (!bank->cycle) return fail(error, QA_ERROR_ARGUMENT, "Source scene bank frame cycle is exhausted");
    *admitted = false;
    qa_q3_source_scene_membership *m = &bank->membership;
    if (!shader || m->polygons >= m->max_polygons || count > m->max_vertices - m->vertices) return true;
    if (!qa_q3_assets_retain(assets, error)) return false;
    qa_q3_source_polygon_cell *cell = bank->polygons + m->polygons;
    qa_q3_presentation_assets *previous = cell->assets;
    if (count) memcpy(bank->vertices + m->vertices, vertices, count * sizeof(*vertices));
    *cell = (qa_q3_source_polygon_cell){.assets = assets, .shader = shader,
        .first = m->vertices, .count = (uint32_t)count, .fog = fog ? *fog : (qa_scene_fog_volume){0}};
    ++m->polygons; m->vertices += (uint32_t)count; *admitted = true;
    qa_q3_assets_release(previous); return true;
}
bool qa_q3_source_scene_bank_light(qa_q3_source_scene_bank *bank,
    qa_q3_presentation_assets *assets, const qa_scene_light *value, bool *admitted, qa_error *error)
{
    if (!bank || !value || !admitted)
        return fail(error, QA_ERROR_ARGUMENT, "Source light admission requires its actual bank and value");
    if (!bank->cycle) return fail(error, QA_ERROR_ARGUMENT, "Source scene bank frame cycle is exhausted");
    *admitted = false;
    if (bank->membership.lights >= QA_Q3_SOURCE_LIGHT_CAPACITY || value->radius <= 0) return true;
    if (!qa_q3_assets_retain(assets, error)) return false;
    qa_q3_source_light_cell *cell = bank->lights + bank->membership.lights;
    qa_q3_presentation_assets *previous = cell->assets;
    *cell = (qa_q3_source_light_cell){assets, *value};
    ++bank->membership.lights; *admitted = true;
    qa_q3_assets_release(previous); return true;
}
bool qa_q3_source_scene_bank_entity_read(const qa_q3_source_scene_bank *bank,
    uint32_t index, qa_q3_source_entity_cell *out)
{ if (!bank || !out || index >= QA_Q3_SOURCE_ENTITY_CAPACITY) return false; *out = bank->entities[index]; return true; }
bool qa_q3_source_scene_bank_entity_lighting(qa_q3_source_scene_bank *bank, uint32_t index,
    qa_vec3 ambient, qa_vec3 directed, qa_vec3 direction, float alpha, float length, bool need_lights)
{
    if (!bank || index >= QA_Q3_SOURCE_ENTITY_CAPACITY) return false;
    qa_q3_source_entity_cell *cell = bank->entities + index;
    cell->ambient = ambient; cell->directed = directed; cell->light_direction = direction;
    cell->ambient_alpha = alpha; cell->axis_length = length;
    cell->need_lights = need_lights; cell->lighting_calculated = true; return true;
}
bool qa_q3_source_scene_bank_entity_frames(qa_q3_source_scene_bank *bank, uint32_t index,
    int32_t frame, int32_t old_frame)
{
    if (!bank || index >= QA_Q3_SOURCE_ENTITY_CAPACITY) return false;
    bank->entities[index].value.frame = frame;
    bank->entities[index].value.old_frame = old_frame; return true;
}
bool qa_q3_source_scene_bank_poly_read(const qa_q3_source_scene_bank *bank, uint32_t index,
    qa_q3_source_polygon_cell *out, const qa_q3_poly_vertex **vertices)
{
    if (!bank || !out || !vertices || index >= bank->membership.max_polygons) return false;
    *out = bank->polygons[index]; *vertices = bank->vertices + out->first; return true;
}
bool qa_q3_source_scene_bank_light_read(const qa_q3_source_scene_bank *bank,
    uint32_t index, qa_q3_source_light_cell *out)
{ if (!bank || !out || index >= QA_Q3_SOURCE_LIGHT_CAPACITY) return false; *out = bank->lights[index]; return true; }
static qa_q3_presentation_assets *registry_slot(const qa_q3_source_scene_bank *bank, size_t index)
{
    if (index < QA_Q3_SOURCE_ENTITY_CAPACITY) return bank->entities[index].assets;
    index -= QA_Q3_SOURCE_ENTITY_CAPACITY;
    if (index < bank->membership.max_polygons) return bank->polygons[index].assets;
    index -= bank->membership.max_polygons;
    return bank->lights[index].assets;
}
qa_q3_presentation_assets *qa_q3_source_scene_bank_registry_at(
    const qa_q3_source_scene_bank *bank, size_t ordinal)
{
    if (!bank) return NULL;
    size_t count = QA_Q3_SOURCE_ENTITY_CAPACITY + (size_t)bank->membership.max_polygons + QA_Q3_SOURCE_LIGHT_CAPACITY;
    for (size_t i = 0; i < count; ++i) {
        qa_q3_presentation_assets *assets = registry_slot(bank, i);
        if (!assets) continue;
        bool first = true;
        for (size_t j = 0; j < i; ++j) if (registry_slot(bank, j) == assets) { first = false; break; }
        if (first && !ordinal--) return assets;
    }
    return NULL;
}
size_t qa_q3_source_scene_bank_registry_count(const qa_q3_source_scene_bank *bank)
{
    if (!bank) return 0;
    size_t size = QA_Q3_SOURCE_ENTITY_CAPACITY + (size_t)bank->membership.max_polygons + QA_Q3_SOURCE_LIGHT_CAPACITY;
    size_t count = 0;
    for (size_t i = 0; i < size; ++i) {
        qa_q3_presentation_assets *assets = registry_slot(bank, i);
        if (!assets) continue;
        bool first = true;
        for (size_t j = 0; j < i; ++j) if (registry_slot(bank, j) == assets) { first = false; break; }
        count += first;
    }
    return count;
}
bool q3_source_bank_valid_keys(const qa_q3_source_scene_bank *bank, const uint64_t *keys, qa_error *error)
{
    if (!bank || !bank->polygons || !bank->vertices) return false;
    const qa_q3_source_scene_membership *m = &bank->membership;
    if (!m->max_polygons || !m->max_vertices || m->entities > QA_Q3_SOURCE_ENTITY_LIMIT ||
        m->first_entity > m->entities || m->polygons > m->max_polygons || m->first_polygon > m->polygons ||
        m->vertices > m->max_vertices || m->lights > QA_Q3_SOURCE_LIGHT_CAPACITY || m->first_light > m->lights)
        return fail(error, QA_ERROR_FORMAT, "Source scene membership exceeds its physical bank");
    for (uint32_t i = 0; i < m->entities; ++i)
        if (keys ? !keys[i] : !bank->entities[i].assets) return false;
    for (uint32_t i = 0; i < m->lights; ++i)
        if (keys ? !keys[QA_Q3_SOURCE_ENTITY_CAPACITY + (size_t)m->max_polygons + i] : !bank->lights[i].assets) return false;
    uint32_t next = 0;
    for (uint32_t i = 0; i < m->max_polygons; ++i) {
        const qa_q3_source_polygon_cell *cell = bank->polygons + i;
        if (cell->first > m->max_vertices || cell->count > m->max_vertices - cell->first) return false;
        if (i < m->polygons) {
            bool present = keys ? keys[QA_Q3_SOURCE_ENTITY_CAPACITY + (size_t)i] != 0 : cell->assets != NULL;
            if (!present || !cell->shader || cell->first != next) return false;
            next += cell->count;
        }
    }
    return next == m->vertices;
}
bool q3_source_bank_valid(const qa_q3_source_scene_bank *bank, qa_error *error)
{ return q3_source_bank_valid_keys(bank, NULL, error); }

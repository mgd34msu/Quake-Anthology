#include "source_scene_bank_private.h"

static bool vec2(qa_source_save_io *io, qa_scene_vec2 *v)
{ return qa_source_save_f32(io, &v->x) && qa_source_save_f32(io, &v->y); }
static bool entity(qa_source_save_io *io, qa_q3_ref_entity *v)
{
    uint32_t kind = v->kind;
    if (!qa_source_save_u32(io, &kind) || kind > QA_Q3_REF_PORTAL) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) v->kind = (qa_q3_ref_kind)kind;
    if (!qa_source_save_i32(io, &v->flags) || !qa_source_save_i32(io, &v->model) ||
        !qa_source_save_i32(io, &v->frame) || !qa_source_save_i32(io, &v->old_frame) ||
        !qa_source_save_i32(io, &v->skin) || !qa_source_save_i32(io, &v->custom_skin) ||
        !qa_source_save_i32(io, &v->custom_shader) || !qa_source_save_vec3(io, &v->lighting_origin)) return false;
    for (unsigned i = 0; i < 3; ++i) if (!qa_source_save_vec3(io, v->axis + i)) return false;
    return qa_source_save_vec3(io, &v->origin) && qa_source_save_vec3(io, &v->old_origin) &&
        qa_source_save_f32(io, &v->shadow_plane) && qa_source_save_f32(io, &v->back_lerp) &&
        qa_source_save_f32(io, &v->shader_time) && qa_source_save_f32(io, &v->radius) &&
        qa_source_save_f32(io, &v->rotation) && vec2(io, &v->shader_texcoord) &&
        qa_source_save_bytes(io, v->color, sizeof(v->color)) && qa_source_save_bool(io, &v->non_normalized_axes);
}
static bool fog(qa_source_save_io *io, qa_scene_fog_volume *v)
{
    uint32_t kind = v->fog.kind, effect = v->fog.effect;
    if (!qa_source_save_u32(io, &v->index) || !qa_source_save_u32(io, &kind) || kind > QA_FOG_Q2 ||
        !qa_source_save_u32(io, &effect) || effect > QA_FOG_NO_EFFECT) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) {
        v->fog.kind = (qa_scene_fog_kind)kind; v->fog.effect = (qa_scene_fog_effect)effect;
    }
    return qa_source_save_vec3(io, &v->fog.color) && qa_source_save_vec3(io, &v->fog.height_color) &&
        qa_source_save_vec3(io, &v->fog.height_end_color) && qa_source_save_f32(io, &v->fog.density) &&
        qa_source_save_f32(io, &v->fog.amount) && qa_source_save_f32(io, &v->fog.sky_factor) &&
        qa_source_save_f32(io, &v->fog.height_density) && qa_source_save_f32(io, &v->fog.height_start) &&
        qa_source_save_f32(io, &v->fog.height_end) && qa_source_save_f32(io, &v->fog.height_falloff) &&
        qa_source_save_f32(io, &v->fog.far_depth) && qa_source_save_bool(io, &v->fog.sky_drawn) &&
        qa_source_save_f32(io, &v->tc_scale) && qa_source_save_bool(io, &v->has_surface) &&
        qa_source_save_vec3(io, &v->surface.normal) && qa_source_save_f32(io, &v->surface.distance);
}
static bool light(qa_source_save_io *io, qa_scene_light *v)
{
    uint32_t family = v->family;
    if (!qa_source_save_vec3(io, &v->origin) || !qa_source_save_vec3(io, &v->color) ||
        !qa_source_save_vec3(io, &v->direction) || !qa_source_save_f32(io, &v->radius) ||
        !qa_source_save_f32(io, &v->minimum) || !qa_source_save_f32(io, &v->scale) ||
        !qa_source_save_f32(io, &v->cos_half_angle) || !qa_source_save_bool(io, &v->additive) ||
        !qa_source_save_bool(io, &v->spot) || !qa_source_save_bool(io, &v->casts_shadow) ||
        !qa_source_save_u64(io, &v->identity) || !qa_source_save_u64(io, &v->revision) ||
        !qa_source_save_u32(io, &v->shadow_resolution) || !qa_source_save_u32(io, &family) || family > QA_SCENE_Q3) return false;
    if (io->direction == QA_SOURCE_SAVE_READ) v->family = (qa_scene_family)family;
    return true;
}
static bool registry(qa_source_save_io *io, qa_q3_presentation_assets *assets,
    const qa_q3_source_scene_bank_refs *refs, uint64_t *saved)
{
    bool reading = io->direction == QA_SOURCE_SAVE_READ;
    uint64_t key = 0;
    if (!reading && assets && (!refs || !refs->assets_encode ||
        !refs->assets_encode(refs->context, assets, &key, io->error) || !key)) return false;
    if (!qa_source_save_u64(io, &key)) return false;
    if (reading) *saved = key;
    return true;
}
static bool membership(qa_source_save_io *io, qa_q3_source_scene_membership *m)
{
    return qa_source_save_u32(io, &m->entities) && qa_source_save_u32(io, &m->first_entity) &&
        qa_source_save_u32(io, &m->polygons) && qa_source_save_u32(io, &m->first_polygon) &&
        qa_source_save_u32(io, &m->vertices) && qa_source_save_u32(io, &m->lights) &&
        qa_source_save_u32(io, &m->first_light) && qa_source_save_u32(io, &m->max_polygons) &&
        qa_source_save_u32(io, &m->max_vertices);
}
static bool header(qa_source_save_io *io, qa_q3_source_scene_membership *m)
{
    uint8_t magic[4] = {'Q','3','S','B'}; uint32_t version = 1;
    return qa_source_save_bytes(io, magic, sizeof(magic)) && !memcmp(magic, "Q3SB", sizeof(magic)) &&
        qa_source_save_u32(io, &version) && version == 1 && membership(io, m);
}
static bool cells(qa_source_save_io *io, qa_q3_source_scene_bank *bank,
    const qa_q3_source_scene_bank_refs *refs, uint64_t *keys)
{
    for (uint32_t i = 0; i < QA_Q3_SOURCE_ENTITY_CAPACITY; ++i) {
        qa_q3_source_entity_cell *cell = bank->entities + i;
        if (!registry(io, cell->assets, refs, keys ? keys + i : NULL) || !entity(io, &cell->value) ||
            !qa_source_save_vec3(io, &cell->ambient) || !qa_source_save_vec3(io, &cell->directed) ||
            !qa_source_save_vec3(io, &cell->light_direction) || !qa_source_save_f32(io, &cell->ambient_alpha) ||
            !qa_source_save_f32(io, &cell->axis_length) || !qa_source_save_bool(io, &cell->lighting_calculated) ||
            !qa_source_save_bool(io, &cell->need_lights)) return false;
    }
    for (uint32_t i = 0; i < bank->membership.max_polygons; ++i) {
        qa_q3_source_polygon_cell *cell = bank->polygons + i;
        if (!registry(io, cell->assets, refs, keys ? keys + QA_Q3_SOURCE_ENTITY_CAPACITY + (size_t)i : NULL) || !qa_source_save_i32(io, &cell->shader) ||
            !qa_source_save_u32(io, &cell->first) || !qa_source_save_u32(io, &cell->count) || !fog(io, &cell->fog)) return false;
    }
    for (uint32_t i = 0; i < bank->membership.max_vertices; ++i) {
        qa_q3_poly_vertex *v = bank->vertices + i;
        if (!qa_source_save_vec3(io, &v->position) || !vec2(io, &v->texcoord) ||
            !qa_source_save_bytes(io, v->color, sizeof(v->color))) return false;
    }
    for (uint32_t i = 0; i < QA_Q3_SOURCE_LIGHT_CAPACITY; ++i) {
        qa_q3_source_light_cell *cell = bank->lights + i;
        if (!registry(io, cell->assets, refs, keys ? keys + QA_Q3_SOURCE_ENTITY_CAPACITY + (size_t)bank->membership.max_polygons + i : NULL) || !light(io, &cell->value)) return false;
    }
    return q3_source_bank_valid_keys(bank, keys, io->error);
}
bool qa_q3_source_scene_bank_checkpoint(const qa_q3_source_scene_bank *bank,
    const qa_q3_source_scene_bank_refs *refs, qa_buffer *out, qa_error *error)
{
    if (!out || out->data || out->size || !q3_source_bank_valid(bank, error)) return false;
    qa_source_save_io io = {0}; qa_q3_source_scene_membership m = bank->membership;
    bool okay = qa_source_save_writer(&io, NULL, error) && header(&io, &m) &&
        cells(&io, (qa_q3_source_scene_bank *)bank, refs, NULL) && qa_source_save_finish(&io, out);
    qa_source_save_dispose(&io); return okay;
}
bool qa_q3_source_scene_bank_restore(qa_bytes bytes, const qa_q3_source_scene_bank_refs *refs,
    qa_q3_source_scene_bank **out, qa_error *error)
{
    if (!out || *out) return false;
    qa_source_save_io io = {0}; qa_q3_source_scene_membership m = {0};
    qa_q3_source_scene_bank *bank = NULL;
    uint64_t *keys = NULL;
    bool okay = qa_source_save_reader(&io, NULL, bytes, error) && header(&io, &m) &&
        m.max_polygons <= bytes.size / 16 && m.max_vertices <= bytes.size / 24 &&
        qa_q3_source_scene_bank_create(m.max_polygons, m.max_vertices, &bank, error);
    size_t count = QA_Q3_SOURCE_ENTITY_CAPACITY + (size_t)m.max_polygons + QA_Q3_SOURCE_LIGHT_CAPACITY;
    if (okay) {
        keys = count <= SIZE_MAX / sizeof(*keys) ? calloc(count, sizeof(*keys)) : NULL;
        if (!keys) { qa_error_set(error, QA_ERROR_MEMORY, 0, "Retaining Source registry namespace keys"); okay = false; }
    }
    if (okay) { bank->membership = m; okay = cells(&io, bank, refs, keys) && qa_source_save_finish(&io, NULL); }
    qa_source_save_dispose(&io);
    for (size_t i = 0; okay && i < count; ++i) {
        if (!keys[i]) continue;
        qa_q3_presentation_assets *actual = NULL;
        okay = refs && refs->assets_decode && refs->assets_decode(refs->context, keys[i], &actual, error) &&
            actual && qa_q3_assets_retain(actual, error);
        if (!okay) break;
        if (i < QA_Q3_SOURCE_ENTITY_CAPACITY) bank->entities[i].assets = actual;
        else if (i - QA_Q3_SOURCE_ENTITY_CAPACITY < m.max_polygons)
            bank->polygons[i - QA_Q3_SOURCE_ENTITY_CAPACITY].assets = actual;
        else bank->lights[i - QA_Q3_SOURCE_ENTITY_CAPACITY - m.max_polygons].assets = actual;
    }
    free(keys);
    if (okay) *out = bank;
    else {
        qa_q3_source_scene_bank_destroy(bank);
        if (error && error->code == QA_OK) qa_error_set(error, QA_ERROR_FORMAT, 0, "Invalid Source scene bank continuation");
    }
    return okay;
}

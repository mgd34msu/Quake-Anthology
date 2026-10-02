#ifndef QA_Q3_SOURCE_SCENE_BANK_H
#define QA_Q3_SOURCE_SCENE_BANK_H
#include "qa/q3_presentation.h"

#define QA_Q3_SOURCE_ENTITY_CAPACITY 1023u
#define QA_Q3_SOURCE_ENTITY_LIMIT 1022u
#define QA_Q3_SOURCE_LIGHT_CAPACITY 32u

typedef struct qa_q3_source_scene_bank qa_q3_source_scene_bank;
typedef struct qa_q3_source_scene_membership {
    uint32_t entities, first_entity, polygons, first_polygon, vertices;
    uint32_t lights, first_light, max_polygons, max_vertices;
} qa_q3_source_scene_membership;
typedef struct qa_q3_source_entity_cell {
    qa_q3_presentation_assets *assets;
    qa_q3_ref_entity value;
    qa_vec3 ambient, directed, light_direction;
    float ambient_alpha, axis_length;
    bool lighting_calculated, need_lights;
} qa_q3_source_entity_cell;
typedef struct qa_q3_source_polygon_cell {
    qa_q3_presentation_assets *assets;
    int32_t shader;
    uint32_t first, count;
    qa_scene_fog_volume fog;
} qa_q3_source_polygon_cell;
typedef struct qa_q3_source_light_cell {
    qa_q3_presentation_assets *assets;
    qa_scene_light value;
} qa_q3_source_light_cell;

bool qa_q3_source_scene_bank_create(uint32_t max_polygons, uint32_t max_vertices,
    qa_q3_source_scene_bank **, qa_error *);
void qa_q3_source_scene_bank_destroy(qa_q3_source_scene_bank *);
/* Rollover resets membership; retained physical cells survive until overwrite. */
void qa_q3_source_scene_bank_frame(qa_q3_source_scene_bank *);
void qa_q3_source_scene_bank_clear(qa_q3_source_scene_bank *);
bool qa_q3_source_scene_bank_membership(const qa_q3_source_scene_bank *,
    qa_q3_source_scene_membership *);
bool qa_q3_source_scene_bank_entity_capacity(const qa_q3_source_scene_bank *);
bool qa_q3_source_scene_bank_poly_capacity(const qa_q3_source_scene_bank *, size_t vertices);
bool qa_q3_source_scene_bank_light_capacity(const qa_q3_source_scene_bank *);
bool qa_q3_source_scene_bank_entity(qa_q3_source_scene_bank *,
    qa_q3_presentation_assets *, const qa_q3_ref_entity *, uint32_t *ordinal,
    bool *admitted, qa_error *);
bool qa_q3_source_scene_bank_entity_range(qa_q3_source_scene_bank *,
    qa_q3_presentation_assets *, const qa_q3_ref_entity *, size_t count,
    uint32_t *first, size_t *admitted, qa_error *);
bool qa_q3_source_scene_bank_poly(qa_q3_source_scene_bank *,
    qa_q3_presentation_assets *, int32_t shader, const qa_q3_poly_vertex *,
    size_t count, const qa_scene_fog_volume *, bool *admitted, qa_error *);
bool qa_q3_source_scene_bank_light(qa_q3_source_scene_bank *,
    qa_q3_presentation_assets *, const qa_scene_light *, bool *admitted, qa_error *);
/* Physical slot reads include retained inactive cells and perform no lookup. */
bool qa_q3_source_scene_bank_entity_read(const qa_q3_source_scene_bank *, uint32_t,
    qa_q3_source_entity_cell *);
bool qa_q3_source_scene_bank_entity_lighting(qa_q3_source_scene_bank *, uint32_t,
    qa_vec3 ambient, qa_vec3 directed, qa_vec3 direction, float ambient_alpha,
    float axis_length, bool need_lights);
bool qa_q3_source_scene_bank_entity_frames(qa_q3_source_scene_bank *, uint32_t,
    int32_t frame, int32_t old_frame);
bool qa_q3_source_scene_bank_poly_read(const qa_q3_source_scene_bank *, uint32_t,
    qa_q3_source_polygon_cell *, const qa_q3_poly_vertex **);
bool qa_q3_source_scene_bank_light_read(const qa_q3_source_scene_bank *, uint32_t,
    qa_q3_source_light_cell *);
/* Distinct physical registries include every retained inactive cell. */
size_t qa_q3_source_scene_bank_registry_count(const qa_q3_source_scene_bank *);
qa_q3_presentation_assets *qa_q3_source_scene_bank_registry_at(
    const qa_q3_source_scene_bank *, size_t);

typedef struct qa_q3_source_scene_bank_refs {
    void *context;
    bool (*assets_encode)(void *, const qa_q3_presentation_assets *, uint64_t *, qa_error *);
    bool (*assets_decode)(void *, uint64_t, qa_q3_presentation_assets **, qa_error *);
} qa_q3_source_scene_bank_refs;
bool qa_q3_source_scene_bank_checkpoint(const qa_q3_source_scene_bank *,
    const qa_q3_source_scene_bank_refs *, qa_buffer *, qa_error *);
/* Creates isolated value storage and retains actual decoded registry owners. */
bool qa_q3_source_scene_bank_restore(qa_bytes, const qa_q3_source_scene_bank_refs *,
    qa_q3_source_scene_bank **, qa_error *);

#endif

#ifndef QA_SCENE_WORLD_INTERNAL_H
#define QA_SCENE_WORLD_INTERNAL_H

#include "qa/material.h"
#include "qa/stamp.h"

typedef struct qaw_legacy qaw_legacy;
typedef struct qaw_patch qaw_patch;
typedef struct qaw_surface {
    qa_scene_mesh mesh;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    const qa_material *material;
    const qa_material *base_material;
    float material_time_offset;
    qa_scene_image *lightmap;
    qa_scene_plane plane;
    bool has_plane, skip, sky, flare;
    qa_bsp_surface_type type;
    float sort;
    uint32_t source_index, fog_index;
    qa_scene_fog fog;
    qaw_legacy *legacy;
    qaw_patch *patch;
} qaw_surface;

typedef struct qaw_model {
    qa_bsp_model source;
    uint64_t identity;
    uint32_t *surfaces;
    size_t surface_count, surface_capacity;
} qaw_model;

typedef struct qaw_pending { int32_t child; uint32_t lights, planes; } qaw_pending;
typedef struct qaw_visibility_parent { uint32_t node, next; } qaw_visibility_parent;
typedef struct qaw_admission_change { uint32_t surface; uint64_t previous; } qaw_admission_change;

typedef struct qaw_visibility_light { qa_vec3 origin; float radius; } qaw_visibility_light;
typedef struct qaw_visibility_cache {
    qa_vec3 origin, axis[3], pvs_origin;
    float projection[2];
    qa_scene_plane clip_plane;
    qa_bounds bounds_start, bounds;
    qaw_visibility_light lights[32];
    size_t light_count, surface_count, area_bytes;
    uint64_t leaf_visits;
    uint32_t visibility_generation, node_generation;
    int32_t eye, pvs_selector, pvs_secondary;
    bool valid, source, no_cull, no_vis, clip_enabled, pvs_all;
    bool areas_present, bounds_valid;
    uint8_t *areas;
    size_t area_limit, area_capacity;
} qaw_visibility_cache;

struct qa_scene_world_scratch {
    qa_stamp_set pvs_nodes, source_vis, surfaces;
    uint32_t *visible_surfaces, *surface_lights;
    size_t visible_count;
    qaw_pending *pending;
    uint8_t *pvs, *secondary_pvs;
    size_t pvs_size;
    int32_t pvs_selector, pvs_secondary, source_view_cluster;
    bool pvs_cached, pvs_all, pvs_nodes_cached;
    uint8_t source_area_mask[32];
    bool source_area_mask_modified;
    qaw_visibility_cache visible_cache;
};

struct qa_scene_world {
    const qa_resource *source_resource;
    size_t references;
    qa_scene_resources *retained_resources;
    qa_material_library *retained_materials;
    qa_buffer bytes, lit_bytes, entity_bytes, palette_bytes, translation_bytes;
    char *sky_name;
    qa_bsp_view bsp;
    qa_scene_resources *resources;
    qa_material_library *materials;
    qa_scene_world_options options;
    qa_bsp_plane *planes;
    qa_bsp_node *nodes;
    qa_bsp_leaf *leaves;
    uint32_t *leaf_surfaces;
    size_t plane_count, node_count, leaf_count, leaf_surface_count;
    qaw_model *models;
    size_t model_count;
    qaw_surface *surfaces;
    size_t surface_count;
    qa_bounds bounds;
    qa_bsp_lighting lighting;
    qa_bsp_lightgrid lightgrid;
    qa_vec3 grid_size;
    uint64_t identity, revision;
    void *legacy_data, *q3_data;
    uint64_t *admitted_surfaces;
    uint64_t admission_generation;
    qaw_admission_change *admission_changes;
    size_t admission_change_count, admission_change_capacity, transaction_depth;
    const qa_scene_frame *admission_frame;
    uint64_t admission_sequence;
    size_t admission_view;
    size_t pvs_capacity, visibility_area_limit, visibility_area_capacity;
    uint32_t *source_dlight_masks;
    uint32_t cluster_count;
    bool sky_drawn;
    bool checkpoint_active;
    bool restore_pending;
    struct qa_scene_world_capture *capture;
    qa_scene_world_image_policy *image_policy;
    uint32_t *visibility_parent_heads;
    qaw_visibility_parent *visibility_parents;
};

struct qa_scene_source_world_view {
    const qa_scene_world *world;
    const qa_scene_frame *frame;
    uint64_t sequence;
    qa_vec3 origin, axis[3];
    qa_bounds bounds;
    float projection_x, projection_y;
    bool no_cull, no_curves, disable_face_plane_cull;
    uint32_t *surfaces, *lights;
    qa_scene_cull *culls;
    size_t count;
};

bool qaw_world_owners_retain(qa_scene_world *, qa_scene_resources *, qa_material_library *, qa_error *);

bool qaw_build_legacy(qa_scene_world *, qa_error *);
bool qawl_light_styles(qa_scene_world *, const qa_scene_world_input *, qa_error *);
void qaw_destroy_legacy(qa_scene_world *);
bool qaw_submit_legacy(qa_scene_world *, qaw_surface *, const qa_material_context *,
                       const qa_scene_world_input *, qa_scene_frame *, qa_error *);
bool qaw_sample_legacy_light(const qa_scene_world *, qa_vec3, qa_vec3 *, qa_vec3 *, qa_vec3 *);
bool qaw_legacy_casts_shadow(const qa_scene_world *, const qaw_surface *, bool);
bool qaw_build_q3(qa_scene_world *, qa_error *);
void qaw_destroy_q3(qa_scene_world *);
bool qaw_submit_q3(qa_scene_world *, qaw_surface *, const qa_material_context *,
                   const qa_scene_world_input *, qa_scene_frame *, qa_error *);
bool qaw_sample_q3_light(const qa_scene_world *, qa_vec3, qa_vec3 *, qa_vec3 *, qa_vec3 *);
bool qaw_q3_fog_for_sphere(const qa_scene_world *, qa_vec3, float, qa_scene_fog_volume *);
bool qaw_q3_fog_for_bounds(const qa_scene_world *, qa_bounds, qa_scene_fog_volume *);
bool qaw_submit_material_sky(const qa_scene_world *, const qa_material *, const qa_material *,
                              const qa_scene_mesh *, const qa_material_context *,
                              const qa_scene_world_input *, qa_scene_frame *, qa_error *);

char *qaw_string(qa_bytes, qa_error *);
bool qaw_mesh_allocate(qa_scene_world *, qaw_surface *, size_t, size_t, qa_error *);
void qaw_mesh_bounds(qaw_surface *);
qa_vec3 qaw_local_point(const qa_model_transform *, qa_vec3);
qa_vec3 qaw_local_vector(const qa_model_transform *, qa_vec3);
qa_bounds qaw_transformed_bounds(qa_bounds, const qa_model_transform *);

#endif

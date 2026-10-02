#ifndef QA_SCENE_MODELS_INTERNAL_H
#define QA_SCENE_MODELS_INTERNAL_H

#include "qa/material.h"
#include "qa/scene_model_save.h"
#include <math.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>

typedef struct scene_model_image {
    char *name;
    const qa_material *material;
    const qa_scene_image *base, *fullbright;
    qa_buffer indexed_pixels;
    uint32_t indexed_width, indexed_height;
    bool indexed_override;
    struct scene_model_image *next;
} scene_model_image;

typedef struct scene_model_mesh {
    qa_scene_mesh retained;
    qa_scene_vertex *vertices;
    uint32_t *indices;
    uint32_t *sources;
    uint8_t *normal_indices;
    scene_model_image **shaders;
} scene_model_mesh;

typedef struct scene_model_shadow_identity {
    uint32_t entity;
    uint64_t identity;
    struct scene_model_shadow_identity *next;
} scene_model_shadow_identity;
typedef struct qa_scene_model_capture qa_scene_model_capture;

struct qa_scene_model {
    const qa_model *source;
    qa_scene_model_content_lease source_lease, replacement_source_lease, animation_lease;
    qa_scene_resources *resources;
    qa_material_library *materials;
    bool source_topology;
    qa_scene_image_options options;
    uint8_t palette[768], translation[256];
    uint64_t identity;
    scene_model_mesh *meshes;
    scene_model_image **skins, **sprites;
    scene_model_image *images;
    struct qa_scene_model *replacement;
    struct qa_scene_model *replacement_next;
    struct qa_scene_model *replacement_parent;
    struct qa_scene_model *selected_replacement;
    bool replacement_policy_set, replacement_policy_enabled;
    double replacement_distance;
    unsigned active_submissions;
    bool checkpoint_active;
    qa_scene_model_capture *capture;
    qa_scene_model_image_policy *image_policy;
    qa_model_replacement replacement_description;
    const qa_model_replacement *replacement_source;
    scene_model_image ***replacement_skins;
    size_t replacement_skin_count;
    scene_model_shadow_identity *shadow_identities;
};

static inline qa_vec3 model_vec(const float v[3]) { return qa_v3(v[0], v[1], v[2]); }
static inline void model_store(float out[3], qa_vec3 v) { out[0] = v.x; out[1] = v.y; out[2] = v.z; }
static inline qa_vec3 model_origin(const qa_scene_model_input *in) { return model_vec(in->transform.origin); }
static inline qa_bounds model_bounds_empty(void) {
    return (qa_bounds){qa_v3(INFINITY, INFINITY, INFINITY), qa_v3(-INFINITY, -INFINITY, -INFINITY)};
}
static inline void model_bounds_add(qa_bounds *b, qa_vec3 p) {
    b->mins = qa_v3(fminf(b->mins.x, p.x), fminf(b->mins.y, p.y), fminf(b->mins.z, p.z));
    b->maxs = qa_v3(fmaxf(b->maxs.x, p.x), fmaxf(b->maxs.y, p.y), fmaxf(b->maxs.z, p.z));
}

bool scene_model_external(qa_scene_model *, const char *, scene_model_image **, qa_error *);
bool scene_model_external_material(qa_scene_model *, qa_material_library *, const char *,
                                   const qa_material **, qa_error *);
bool scene_model_indexed(qa_scene_model *, const char *, qa_bytes, uint32_t, uint32_t,
                         bool sprite, scene_model_image **, qa_error *);
bool scene_model_indexed_override(qa_scene_model *, const qa_scene_model_indexed_skin *,
                                  scene_model_image **, qa_error *);
void scene_model_images_destroy(qa_scene_model *);
bool scene_model_topology(qa_scene_model *, uint32_t, qa_error *);
void scene_model_topology_destroy(qa_scene_model *);
uint8_t scene_model_normal_index(const float normal[3]);
qa_vec3 scene_model_shell_color(uint32_t flags);
bool scene_model_has_shell(const qa_scene_model_input *);
qa_vec3 scene_model_alias_light(const qa_scene_model_input *);
float scene_model_shade(const qa_scene_model_input *, const float normal[3], uint8_t index);
bool scene_model_sprite_submit(qa_scene_model *, const qa_scene_model_input *, uint32_t,
                                qa_scene_frame *, qa_error *);
bool scene_model_emit(qa_scene_model *, const qa_scene_model_input *, const qa_scene_mesh *,
                       const scene_model_image *, bool unlit, bool world,
                       qa_scene_frame *, qa_error *);

#endif

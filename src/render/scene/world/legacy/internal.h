#ifndef QAW_LEGACY_INTERNAL_H
#define QAW_LEGACY_INTERNAL_H

#include "../internal.h"

typedef struct qawl_texture {
    char *name;
    uint32_t width, height, quake64_shift;
    qa_scene_image *image, *fullbright, *sky[2];
    size_t animation[2][10], animation_count[2];
    int32_t next;
} qawl_texture;

typedef struct qawl_world {
    qawl_texture *textures;
    size_t texture_count;
    qa_bsp_q1_metadata metadata;
    qa_scene_image *sky[6];
    float *q1_styles;
    qa_vec3 *q2_styles;
    size_t style_count;
} qawl_world;

struct qaw_legacy {
    size_t texture;
    size_t *frames, frame_count;
    bool warp, flowing, fence, lightmapped, decoupled;
    float alpha;
    uint32_t width, height;
    float projection[2][4], light_step[2];
    size_t sample_offset;
    uint16_t *styles;
    size_t style_count;
    float *cached_styles;
    bool light_cache_valid, light_cache_dynamic;
    uint8_t *light_pixels;
    uint8_t *encoded_pixels;
    qa_scene_image *direct_lightmap;
    void *light_accumulation;
};

/* Lighting setup computes texel projection before the geometry is populated.
 * All style, image update and sample state belongs to the surface. */
bool qawl_light_setup(qa_scene_world *, qaw_surface *, const qa_bsp_face *,
                      const qa_bsp_texinfo *, const qa_vec3 *, size_t, qa_error *);
bool qawl_light_update(qa_scene_world *, qaw_surface *, const qa_material_context *,
                       const qa_scene_world_input *, qa_error *);
void qawl_light_destroy(qaw_legacy *);
bool qawl_light_styles(qa_scene_world *, const qa_scene_world_input *, qa_error *);
bool qawl_textures_build(qa_scene_world *, qa_error *);
void qawl_textures_destroy(qawl_world *);
bool qawl_geometry_build(qa_scene_world *, qaw_surface *, const qa_bsp_face *,
                         const qa_bsp_texinfo *, qa_error *);

#endif

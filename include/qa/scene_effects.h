#ifndef QA_SCENE_EFFECTS_H
#define QA_SCENE_EFFECTS_H

#include "qa/scene.h"

/* Colors in the effect API are normalized, including caller-resolved palettes.
 * Functions append draws in caller order; they never sort translucent effects. */
bool qa_scene_portal_surface_visible(const qa_scene_mesh *, const qa_scene_view *, float range, bool mirror);
/* Emits the original 16-sector additive fans. Q1 inside-light blending updates
 * the caller's current view blend; Q2 always emits its colored fan. */
bool qa_scene_legacy_dlights(qa_scene_frame *, const qa_scene_view *, qa_scene_family,
                             bool quakeworld, const qa_scene_light *, size_t,
                             qa_scene_vec4 *view_blend, qa_error *);
bool qa_scene_indexed_particle(qa_scene_frame *, const qa_scene_view *, qa_scene_family,
                               qa_vec3 origin, float size, qa_scene_vec4,
                               const qa_scene_image *, qa_error *);

typedef struct qa_scene_sky_bounds { float min_s, min_t, max_s, max_t; } qa_scene_sky_bounds;
void qa_scene_sky_bounds_reset(qa_scene_sky_bounds bounds[6]);
bool qa_scene_sky_clip(const qa_scene_mesh *, size_t count, qa_vec3 origin,
                       qa_scene_sky_bounds bounds[6], qa_error *);
bool qa_scene_q2_sky(qa_scene_frame *, const qa_scene_view *, const qa_scene_image *const images[6],
                     const qa_scene_sky_bounds bounds[6], float rotation, qa_vec3 axis,
                     bool rotating, qa_scene_vec4 color, qa_error *);
typedef struct qa_scene_sky_geometry {
    qa_scene_mesh faces[6], clouds;
    bool visible[6];
} qa_scene_sky_geometry;
/* Callers submit outer faces in face order and clouds through their material.
 * The renderer owns the current cloud-height setting, as in Q3 skyParms. */
bool qa_scene_q3_sky_geometry(qa_scene_frame *, qa_vec3 origin, float far_clip,
                              float cloud_height, const qa_scene_sky_bounds bounds[6],
                              qa_scene_sky_geometry *, qa_error *);

typedef enum qa_scene_rail_kind { QA_RAIL_CORE, QA_RAIL_RINGS, QA_RAIL_LIGHTNING } qa_scene_rail_kind;
typedef struct qa_scene_rail_options {
    int32_t core_width, ring_width;
    float segment_length;
    /* Q3's rail writer leaves normals, lightmap UVs, and alpha untouched. The
     * source-client tess owner supplies the retained cells when applicable. */
    const qa_scene_vertex *retained_vertices;
    size_t retained_count;
} qa_scene_rail_options;
bool qa_scene_rail_geometry(qa_scene_frame *, const qa_scene_view *, qa_scene_rail_kind,
                            qa_vec3 origin, qa_vec3 old_origin, qa_scene_vec4,
                            const qa_scene_rail_options *, qa_scene_mesh *, qa_error *);
typedef struct qa_scene_flare_options {
    qa_vec3 color, rim_color;
    float scale, fade_start, fade_end;
    bool separate_rim, lock_angle, standard_image;
} qa_scene_flare_options;
bool qa_scene_flare(qa_scene_frame *, const qa_scene_view *, qa_vec3 origin,
                    const qa_scene_flare_options *, const qa_scene_image *, qa_error *);
bool qa_scene_q3_beam(qa_scene_frame *, const qa_scene_view *, qa_vec3 origin,
                      qa_vec3 old_origin, const qa_scene_image *,
                      const qa_scene_state *previous_state, qa_error *);
bool qa_scene_poly_geometry(qa_scene_frame *, const qa_scene_vertex *, size_t,
                            qa_scene_mesh *, qa_error *);
/* Geometry is owned by the frame until reset; no draw or material is emitted. */
bool qa_scene_sprite_geometry(qa_scene_frame *, const qa_scene_view *, qa_vec3 origin,
                              float radius, float rotation, qa_scene_vec4,
                              qa_scene_mesh *, qa_error *);
bool qa_scene_default_model(qa_scene_frame *, const qa_scene_view *, qa_scene_matrix model,
                            const qa_scene_image *white, const qa_scene_state *, qa_error *);

/* Call after backend recreation, atlas loss, or dropping an emitted frame. */
void qa_scene_shadows_invalidate(qa_scene_shadows *);
typedef struct qa_scene_shadow_options { bool enabled; uint32_t resolution_cap; } qa_scene_shadow_options;
/* NULL options select enabled shadows and a maximum face resolution of 1024.
 * The cap is a source setting; point-light fitting may reduce it further. */
bool qa_scene_shadows_prepare_options(qa_scene_shadows *, const qa_scene_light *, size_t,
                                      const qa_scene_shadow_caster *, size_t,
                                      const qa_scene_shadow_options *, qa_scene_frame *,
                                      const qa_scene_shadow_light **, size_t *,
                                      const qa_scene_image **, qa_error *);
bool qa_scene_flags_cast_shadow(qa_scene_family, uint32_t flags, float alpha,
                                bool view_model, bool sprite);

typedef enum qa_scene_q1_particle_kind {
    QA_Q1_PARTICLE_STATIC, QA_Q1_PARTICLE_FIRE, QA_Q1_PARTICLE_EXPLODE,
    QA_Q1_PARTICLE_EXPLODE2, QA_Q1_PARTICLE_BLOB, QA_Q1_PARTICLE_BLOB2,
    QA_Q1_PARTICLE_GRAVITY, QA_Q1_PARTICLE_SLOW_GRAVITY
} qa_scene_q1_particle_kind;
typedef struct qa_scene_q1_particle_state {
    qa_vec3 origin, velocity;
    float ramp;
    double die;
    uint32_t color;
    qa_scene_q1_particle_kind kind;
} qa_scene_q1_particle_state;
/* Advance once after the scene sample, never once per local seat. */
void qa_scene_q1_particle_advance(qa_scene_q1_particle_state *, double seconds, float gravity);
typedef struct qa_scene_q2_particle_state {
    int64_t spawn_milliseconds;
    qa_vec3 origin, velocity, acceleration;
    uint32_t color;
    float alpha, alpha_velocity;
} qa_scene_q2_particle_state;
/* The caller retires alpha_velocity == -10000 after one successful sample. */
bool qa_scene_q2_particle_sample(const qa_scene_q2_particle_state *, int64_t milliseconds,
                                 qa_vec3 *origin, float *alpha);
/* Foreign world snapshots may retain fractional milliseconds. The supplied
 * sample remains signed; it is not a client clock or a new particle birth. */
bool qa_scene_q2_particle_sample_at(const qa_scene_q2_particle_state *, double milliseconds,
                                   qa_vec3 *origin, float *alpha);
bool qa_scene_particle_image(qa_scene_resources *, qa_scene_family, qa_scene_image **, qa_error *);

/* The caller selects r_shadows == 2, clears scene stencil, and invokes finish
 * once after all model parts. local_light is expressed in mesh coordinates. */
bool qa_scene_stencil_shadow(qa_scene_frame *, const qa_scene_view *, const qa_scene_mesh *,
                             qa_scene_matrix model, qa_vec3 local_light,
                             const qa_scene_image *white, qa_error *);
bool qa_scene_source_stencil_shadow(qa_scene_frame *, const qa_scene_view *, const qa_scene_mesh *,
                                    qa_scene_matrix model, qa_vec3 local_light,
                                    const qa_scene_image *white, qa_error *);
bool qa_scene_stencil_finish(qa_scene_frame *, const qa_scene_view *,
                             const qa_scene_image *white, qa_error *);

#endif

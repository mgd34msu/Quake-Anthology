#ifndef QA_FRONTEND_LEGACY_RENDER_POLICY_H
#define QA_FRONTEND_LEGACY_RENDER_POLICY_H

#include "qa/frontend.h"
#include "qa/scene.h"

float frontend_legacy_lightstyle_sample(qa_game_family, const char *pattern, double seconds);
qa_vec3 frontend_legacy_entity_angles(qa_scene_family, qa_product_edition,
    const qa_model *, uint64_t effects, qa_vec3 angles, double seconds, int64_t milliseconds);

typedef struct frontend_legacy_render_policy {
    qa_scene_family family;
    bool quakeworld, flashblend, double_eyes, planar_shadows, texture_sort;
    float mirror_alpha;
    qa_scene_legacy_policy lighting;
} frontend_legacy_render_policy;

/* Borrow the reached canonical ENGINE values; QACV owns their continuation. */
bool frontend_legacy_render_policy_read(const qa_frontend *, const qa_product *,
    frontend_legacy_render_policy *, qa_error *);
typedef struct frontend_legacy_cvar_handles {
    qa_cvar_handle r_shadows, gl_shadows, gl_flashblend, gl_doubleeys;
    qa_cvar_handle r_mirroralpha, gl_texsort, r_fullbright, r_lightmap, gl_lightmap;
    qa_cvar_handle r_dynamic, gl_dynamic, gl_polyblend, gl_cull, gl_clear;
    qa_cvar_handle gl_modulate, gl_monolightmap, gl_saturatelighting, cl_flares;
    qa_cvar_handle cl_predict, r_drawviewmodel, cl_gun, hand;
} frontend_legacy_cvar_handles;

void frontend_legacy_cvars_bind(const qa_cvars *, frontend_legacy_cvar_handles *);
bool frontend_legacy_render_policy_read_controls(const qa_cvars *,
    const frontend_legacy_cvar_handles *, const qa_product *,
    frontend_legacy_render_policy *, qa_error *);
bool frontend_legacy_local_policy_read(const qa_frontend *, uint32_t physical_seat,
    const qa_product *, frontend_legacy_render_policy *, qa_error *);
bool frontend_legacy_source_register(qa_cvars *, qa_console_dialect, uint64_t owner, qa_error *);
bool frontend_legacy_source_owns(const qa_cvars *, const char *name);
bool frontend_remote_q1_initial_clear(qa_frontend *, uint32_t seat, bool *active, bool *clear, qa_error *);
bool frontend_remote_q2_initial_clear(qa_frontend *, uint32_t seat, bool *active, bool *clear, qa_error *);
bool frontend_remote_unified_initial_clear(qa_frontend *, uint32_t seat, bool *active, bool *clear, qa_error *);
bool frontend_legacy_model_input(const qa_scene_world *,
    const qa_scene_world_input *, qa_scene_model_input *, qa_error *);
typedef struct frontend_legacy_scene_services {
    void *context;
    bool (*current)(void *);
    bool (*view_blend)(void *,const qa_scene_world_input *,qa_scene_vec4 *,qa_error *);
    bool (*visuals)(void *, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
    bool (*particles)(void *, const qa_scene_world_input *, qa_scene_frame *, qa_error *);
    bool (*dlights)(void *, const qa_scene_world_input *, qa_scene_frame *, qa_scene_vec4 *, qa_error *);
    bool (*reflected_lights)(void *, qa_scene_world_input *, qa_scene_frame *, qa_error *);
    bool (*blend)(void *, const qa_scene_world_input *, qa_scene_vec4, qa_error *);
    bool (*policy)(void *, const qa_product *, frontend_legacy_render_policy *, qa_error *);
} frontend_legacy_scene_services;
bool frontend_legacy_scene_submit_product(qa_frontend *, qa_scene_world *, const qa_product *,
    const qa_scene_world_input *, qa_scene_frame *, const frontend_legacy_scene_services *, qa_error *);
/* The real generic world/visual/particle producers, including a captured NQ
 * mirror pass. No source CGAME render scene or gameplay call is replayed. */
bool frontend_legacy_scene_submit(qa_frontend *, uint32_t physical_seat, qa_actor_owner exclude,
    const qa_scene_world_input *, qa_scene_frame *, qa_error *);

#endif

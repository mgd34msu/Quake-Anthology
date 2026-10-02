#ifndef QA_FRONTEND_LEGACY_RENDER_POLICY_H
#define QA_FRONTEND_LEGACY_RENDER_POLICY_H

#include "internal.h"

typedef struct frontend_legacy_render_policy {
    qa_scene_family family;
    bool quakeworld, flashblend, double_eyes, planar_shadows, texture_sort;
    float mirror_alpha;
    qa_scene_legacy_policy lighting;
} frontend_legacy_render_policy;

/* Borrow the reached canonical ENGINE values; QACV owns their continuation. */
bool frontend_legacy_render_policy_read(const qa_frontend *, const qa_product *,
    frontend_legacy_render_policy *, qa_error *);
bool frontend_legacy_render_policy_read_registry(const qa_cvars *, const qa_product *,
    frontend_legacy_render_policy *, qa_error *);
bool frontend_legacy_local_policy_read(const qa_frontend *, uint32_t physical_seat,
    const qa_product *, frontend_legacy_render_policy *, qa_error *);
bool frontend_legacy_source_register(qa_cvars *, qa_console_dialect, uint64_t owner, qa_error *);
bool frontend_legacy_source_owns(const qa_cvars *, const char *name);
bool frontend_remote_q1_initial_clear(qa_frontend *, uint32_t seat, bool *active, bool *clear, qa_error *);
bool frontend_remote_q2_initial_clear(qa_frontend *, uint32_t seat, bool *active, bool *clear, qa_error *);
bool frontend_remote_unified_initial_clear(qa_frontend *, uint32_t seat, bool *active, bool *clear, qa_error *);
bool frontend_legacy_model_input(const qa_frontend *, qa_product_id, const qa_scene_world *,
    const qa_scene_world_input *, qa_scene_model_input *, qa_error *);
bool frontend_legacy_model_input_product(const qa_frontend *, const qa_product *, const qa_scene_world *,
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

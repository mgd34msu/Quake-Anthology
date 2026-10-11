#ifndef QA_FRONTEND_REMOTE_Q2_FOOTSTEPS_H
#define QA_FRONTEND_REMOTE_Q2_FOOTSTEPS_H
#include "remote_q2_effects.h"
#include "qa/audio.h"
#include "qa/persistence_content.h"
struct frontend_remote_q2;
struct remote_q2_footsteps;
typedef struct remote_q2_footsteps frontend_q2_footsteps;
typedef struct frontend_q2_footstep_source {
    qa_catalog *catalog;
    qa_product_id product;
    const char *map_name;
    qa_resource *map;
    qa_vfs *files;
    qa_collision_geometry *geometry;
    qa_audio_bank *sounds;
    qa_strings *strings;
    void *context;
    bool (*current)(void *, const struct frontend_q2_footstep_source *);
    bool (*trace)(void *, const qa_trace_query *, qa_trace_result *, qa_error *);
    bool (*sound)(void *, qa_audio_asset *, qa_vec3, qa_actor_id, double,
        int32_t, float, float, double, qa_error *);
} frontend_q2_footstep_source;
typedef struct frontend_q2_footstep_sample {
    frontend_remote_q2_effects_pose pose;
    qa_bounds trace_bounds;
    float bottom, footsteps;
    double milliseconds;
    uint32_t event;
} frontend_q2_footstep_sample;
/* The parent keeps its genuine Source and media bank alive. The child owns
 * sidecars, sound admissions and no-repeat state; it invents no wire frame. */
bool frontend_q2_footsteps_create(const frontend_q2_footstep_source *, frontend_q2_footsteps **, qa_error *);
bool frontend_q2_footsteps_current(const frontend_q2_footsteps *, const frontend_q2_footstep_source *, qa_error *);
bool frontend_q2_footsteps_destroy(frontend_q2_footsteps **, qa_error *);
bool frontend_q2_footsteps_emit(frontend_q2_footsteps *, const frontend_q2_footstep_source *,
    const frontend_q2_footstep_sample *, qa_builtin_random *, qa_error *);
bool frontend_q2_footsteps_visit(const frontend_q2_footsteps *, const qa_application_content_visitor *, qa_error *);
bool frontend_q2_footsteps_checkpoint(const frontend_q2_footsteps *, const frontend_q2_footstep_source *,
    const qa_application_content_graph *, qa_buffer *, qa_error *);
bool frontend_q2_footsteps_restore(const frontend_q2_footstep_source *, qa_application_content_graph *,
    qa_bytes, frontend_q2_footsteps **, qa_error *);
bool remote_q2_footsteps_prepare(struct frontend_remote_q2 *, qa_error *);
bool remote_q2_footsteps_current(const struct frontend_remote_q2 *, qa_error *);
void remote_q2_footsteps_clear(struct frontend_remote_q2 *);
bool remote_q2_footstep(void *, const frontend_remote_q2_effects_pose *, uint32_t,
    double, qa_builtin_random *, qa_error *);
bool remote_q2_footsteps_visit(const struct frontend_remote_q2 *, const qa_application_content_visitor *, qa_error *);
bool remote_q2_footsteps_checkpoint(const struct frontend_remote_q2 *, const qa_application_content_graph *, qa_buffer *, qa_error *);
bool remote_q2_footsteps_restore(struct frontend_remote_q2 *, qa_application_content_graph *, qa_bytes, qa_error *);
#endif

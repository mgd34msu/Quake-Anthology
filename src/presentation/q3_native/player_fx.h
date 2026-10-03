#ifndef QA_Q3_NATIVE_PLAYER_FX_H
#define QA_Q3_NATIVE_PLAYER_FX_H

#include "body.h"
#include "qa/source_save.h"

typedef struct q3n_frame q3n_frame;
typedef struct q3n_entity q3n_entity;
typedef struct q3n_remote_entity q3n_remote_entity;

/* The containing centity qualifies these source presentation continuations
 * with its physical GAME row and full actor generation. Client-info owns the
 * donor's breath, medkit and invulnerability clocks. */
typedef struct q3n_player_fx_state {
    q3n_lerp_frame flag;
    float flag_yaw;
    bool flag_yawing;
    uint32_t skull_count;
    qa_vec3 skull_positions[10];
} q3n_player_fx_state;
typedef struct q3n_player_fx_settings {
    int32_t shadow_mode;
    bool draw_friend, enable_breath, enable_dust, animations_disabled;
} q3n_player_fx_settings;
typedef struct q3n_player_fx_backend {
    void *context;
    /* trap_CM_* uses the actual bound world only; dust uses CG_Trace through
     * q3n_events_trace, which also observes source entities. */
    bool (*world_trace)(void *, const q3n_frame *, qa_vec3, qa_vec3,
        qa_bounds, uint32_t mask, qa_trace_result *, qa_error *);
    bool (*world_point_contents)(void *, const q3n_frame *, qa_vec3,
        uint32_t *, qa_error *);
    bool (*body_hidden)(void *, const q3n_frame *,
        const qa_application_native_q3_entity *, bool *, qa_error *);
    /* part is lower=0, upper=1, head=2. consumed=true means the actual
     * composition service submitted the authored base/effect reference. */
    bool (*body_submit)(void *, const q3n_frame *,
        const qa_application_native_q3_entity *, uint32_t part,
        const qa_q3_ref_entity *, bool base, bool *consumed, qa_error *);
    bool (*player_weapon)(void *, const q3n_frame *,
        const qa_application_native_q3_entity *, q3n_entity *,
        const qa_q3_ref_entity *torso, int32_t team, qa_error *);
} q3n_player_fx_backend;
typedef struct q3n_player_fx_remote_backend {
    void *context;
    bool (*world_trace)(void *, const q3n_frame *, qa_vec3, qa_vec3,
        qa_bounds, uint32_t mask, qa_trace_result *, qa_error *);
    bool (*world_point_contents)(void *, const q3n_frame *, qa_vec3,
        uint32_t *, qa_error *);
    bool (*body_hidden)(void *, const q3n_frame *, const q3n_remote_entity *, bool *, qa_error *);
    bool (*body_submit)(void *, const q3n_frame *, const q3n_remote_entity *, uint32_t part,
        const qa_q3_ref_entity *, bool base, bool *consumed, qa_error *);
    bool (*player_weapon)(void *, const q3n_frame *, const q3n_remote_entity *,
        const qa_q3_ref_entity *torso, int32_t team, qa_error *);
} q3n_player_fx_remote_backend;
typedef struct q3n_player_fx_compiled_backend {
    void *context;
    bool (*world_trace)(void *, const q3n_frame *, qa_vec3, qa_vec3, qa_bounds, uint32_t, qa_trace_result *, qa_error *);
    bool (*world_point_contents)(void *, const q3n_frame *, qa_vec3, uint32_t *, qa_error *);
    bool (*body_hidden)(void *, const q3n_frame *, const q3n_compiled_entity *, bool *, qa_error *);
    bool (*body_submit)(void *, const q3n_frame *, const q3n_compiled_entity *, uint32_t,
        const qa_q3_ref_entity *, bool, bool *, qa_error *);
    bool (*player_weapon)(void *, const q3n_frame *, const q3n_compiled_entity *, const qa_q3_ref_entity *, int32_t, qa_error *);
} q3n_player_fx_compiled_backend;

/* Call once after body_build has advanced the genuine split-body pose. This
 * patches shadow flags/plane on those refs and preserves CG_Player order,
 * including lower/upper/head early returns and the final weapon/powerups. */
bool q3n_player_fx_submit(const q3n_frame *, const qa_application_native_q3_entity *,
    q3n_entity *, const q3n_client_info *, q3n_player_body *,
    const q3n_player_fx_settings *, const q3n_player_fx_backend *, qa_error *);
bool q3n_player_fx_submit_remote(const q3n_frame *, const q3n_remote_entity *,
    const q3n_client_info *, q3n_player_body *, const q3n_player_fx_settings *,
    const q3n_player_fx_remote_backend *, qa_error *);
bool q3n_player_fx_submit_compiled(const q3n_frame *, const q3n_compiled_entity *,
    const q3n_client_info *, q3n_player_body *, const q3n_player_fx_settings *,
    const q3n_player_fx_compiled_backend *, qa_error *);
/* Primitive-only aggregate field codec. No media acquisition, callbacks or
 * source writes; the entity aggregate owns field custody and actor admission. */
bool q3n_player_fx_codec(qa_source_save_io *, q3n_player_fx_state *);

#endif

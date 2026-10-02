#ifndef QA_APPLICATION_GUEST_Q3_GEAR_H
#define QA_APPLICATION_GUEST_Q3_GEAR_H

#include "guest_q3_grapple_profile.h"
#include "qa/q3_host.h"
#include "qa/builtin.h"
#include "qa/console_save.h"

typedef struct application_q3_gear application_q3_gear;
typedef struct application_q3_gear_target {
    qa_actor_id actor;
    qa_body_state body;
    float health, view_height;
    int32_t team;
    bool player, mover;
    const char *userinfo;
} application_q3_gear_target;
typedef struct application_q3_gear_damage {
    qa_actor_id target, inflictor, attacker;
    qa_vec3 direction, point;
    int32_t amount, flags, method;
} application_q3_gear_damage;
typedef struct application_q3_gear_options {
    const application_q3_grapple_profile *profile;
    qa_q3_host_options host;
    qa_builtin_services services;
    void *context;
    bool (*current)(void *);
    size_t (*target_count)(void *);
    bool (*target)(void *, size_t, application_q3_gear_target *, qa_error *);
    bool (*damage)(void *, const application_q3_gear_damage *, qa_error *);
    bool (*velocity)(void *, qa_actor_id, qa_vec3, qa_error *);
    /* Publish the real gear source event after its private text is committed;
     * this is distinct from the primary GAME configstring setter. */
    bool (*configstring)(void *, uint32_t index, const char *value, qa_error *);
} application_q3_gear_options;
typedef struct application_q3_gear_view {
    qa_actor_id actor, tether, target, mover;
    qa_q3_player player;
    int32_t time_ms;
    bool pulling;
    const application_q3_grapple_definition *definition;
} application_q3_gear_view;

/* A separate GAME instance. The supplied host owner names its genuine gear
 * actor namespace, and its services borrow the same admitted world. Restored
 * construction creates actual hooks/services without defaults or GAME Init.
 * Restored host.entity_text is the saved, outer-qualified filtered text;
 * normal construction filters the actual map text exactly once. */
bool application_q3_gear_create(const application_q3_gear_options *, bool restoring,
    application_q3_gear **, qa_error *);
bool application_q3_gear_initialize(application_q3_gear *, int32_t seed, qa_error *);
bool application_q3_gear_idle(const application_q3_gear *);
bool application_q3_gear_destroy(application_q3_gear *, qa_error *);
bool application_q3_gear_admit(application_q3_gear *, qa_actor_id, qa_error *);
bool application_q3_gear_userinfo_bound(application_q3_gear *,qa_actor_id,bool *,qa_error *);
bool application_q3_gear_userinfo_changed(application_q3_gear *,qa_actor_id,qa_error *);
bool application_q3_gear_begin_frame(application_q3_gear *, int32_t time_ms,
    int32_t frame, qa_error *);
bool application_q3_gear_fire(application_q3_gear *, qa_actor_id, qa_error *);
bool application_q3_gear_release(application_q3_gear *, qa_actor_id, bool force, qa_error *);
bool application_q3_gear_pull(application_q3_gear *, qa_actor_id, qa_vec3 forward,
    qa_vec3 *velocity, bool *apply, qa_error *);
bool application_q3_gear_actor_releasing(application_q3_gear *, qa_actor_id, qa_error *);
/* Actual session notification after invalidation. Active source callbacks
 * retain private records until their outer call has returned; capture cannot
 * pass that unfinished retirement. */
bool application_q3_gear_actor_released(application_q3_gear *, qa_actor_record, qa_error *);
bool application_q3_gear_read(application_q3_gear *, qa_actor_id,
    application_q3_gear_view *, qa_error *);
qa_console *application_q3_gear_console(application_q3_gear *, qa_cvars **);
bool application_q3_gear_checkpoint(application_q3_gear *, qa_buffer *, qa_error *);
bool application_q3_gear_restore(application_q3_gear *, qa_bytes,
    const qa_console_save_resolvers *, qa_error *);
bool application_q3_gear_finish_restore(application_q3_gear *, qa_error *);

#endif

#ifndef QA_APPLICATION_Q3_WORLD_RESTART_H
#define QA_APPLICATION_Q3_WORLD_RESTART_H

#include "internal.h"
#include "qa/network_q3.h"

typedef struct application_q3_world_restart application_q3_world_restart_state;
typedef struct application_q3_world_startup {
    uint64_t initial_time_ns;
    uint32_t max_clients, random_seed;
    int32_t game_type, restarted;
    bool warmup, restart;
} application_q3_world_startup;

/* Only the actual full replacement owns this transient startup record. Reads
 * qualify the candidate against the retained source instance and program. */
bool application_q3_world_restart_active(const qa_application *);
bool application_q3_world_restart_guest_shutdown(const qa_application *,
    const application_provider *);
bool application_q3_world_restart_guest_handoff(qa_application *, application_provider *,
    qa_cvars *, qa_error *);
bool application_q3_world_restart_source(const qa_application *,
    const application_provider *, application_q3_world_startup *);
bool application_q3_world_restart_cvars(qa_application *, application_provider *,
    qa_cvars *, qa_error *);
/* A missing old actor returns NOT_FOUND. The observations remain owned by the
 * replacement transaction until its final candidate publication or abort. */
bool application_q3_world_restart_client(const qa_application *, qa_actor_id,
    qa_q3_usercmd *, const char **userinfo, qa_error *);

bool application_q3_world_restart(qa_application *, application_provider *,
    qa_mode_id, bool *mutated, qa_error *);

/* Called by the ordinary publication owner on this transaction's actual
 * ticket. These calls are no-ops for other configuration publications. */
bool application_q3_world_restart_prepared(qa_application *,
    application_publication *, qa_error *);
bool application_q3_world_restart_begin(qa_application *,
    application_publication *, qa_error *);
bool application_q3_world_restart_shutdown(qa_application *,
    application_publication *, qa_error *);
bool application_q3_world_restart_retired(qa_application *,
    application_publication *, bool *retain_bots, qa_error *);
bool application_q3_world_restart_admitted(qa_application *,
    application_publication *, qa_error *);
bool application_q3_world_restart_published(qa_application *,
    application_publication *, qa_error *);

/* Ordinary map travel keeps the actual native GAME and transport. The caller
 * retains the cut result across canonical retirement and genuine admission. */
bool application_q3_map_shutdown(qa_application *, application_publication *,
    bool *cut, qa_error *);
bool application_q3_map_published(qa_application *, application_publication *,
    bool cut, qa_error *);

#endif

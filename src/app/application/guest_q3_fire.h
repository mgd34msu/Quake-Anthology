#ifndef QA_APPLICATION_GUEST_Q3_FIRE_H
#define QA_APPLICATION_GUEST_Q3_FIRE_H

#include "qa/game_q3.h"
#include "qa/q3_host.h"
#include "qa/source_save.h"

struct q3g_role;
struct application_provider;
typedef struct q3g_fire_continuation {
    qa_actor_id actor;
    qa_q3_fire_stamp stamp;
    uint32_t sequence;
    int32_t spawn_count, external_event, external_time;
    bool initialized;
} q3g_fire_continuation;
typedef struct q3g_fire_scope {
    struct q3g_role *role;
    qa_q3_host_game_data data;
    qa_qvm_binding binding;
} q3g_fire_scope;

/* The write binding borrows one actual GAME entry only. No idle callback or
 * source RAM address is retained in the player continuation or its codec. */
bool q3g_fire_begin(struct q3g_role *, q3g_fire_scope *, qa_error *);
bool q3g_fire_end(q3g_fire_scope *, qa_error *);
bool q3g_fire_fields(qa_source_save_io *, q3g_fire_continuation *);
bool q3g_fire_valid(const q3g_fire_continuation *, qa_actor_id);
bool application_q3_guest_fire_read(struct application_provider *, qa_actor_id,
    qa_q3_fire_stamp *, qa_error *);

#endif

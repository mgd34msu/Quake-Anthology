#ifndef QA_APPLICATION_GUEST_Q3_DAMAGE_SCOPE_H
#define QA_APPLICATION_GUEST_Q3_DAMAGE_SCOPE_H

#include "guest_q3_combat.h"

typedef struct application_q3_damage_scopes application_q3_damage_scopes;
typedef struct application_q3_damage_frame application_q3_damage_frame;
typedef struct application_q3_damage_call {
    const qa_qvm_call *qvm;
    uint64_t entry;
    const qa_native_value *arguments;
    size_t count;
    void *context;
    bool (*proceed)(void *, const qa_native_value *, size_t, qa_error *);
    bool (*cancel)(void *, qa_error *);
} application_q3_damage_call;
typedef bool (*application_q3_damage_reaction_bind)(void *, uint64_t,
    const application_q3_reaction_call *, qa_error *);

bool application_q3_damage_scopes_create(struct q3g_role *,
    const application_q3_combat_profile *, application_q3_damage_reaction_bind, void *,
    application_q3_damage_scopes **, qa_error *);
bool application_q3_damage_scopes_destroy(application_q3_damage_scopes **, qa_error *);
bool application_q3_damage_scopes_idle(const application_q3_damage_scopes *);
bool application_q3_damage_scopes_quiescent(const application_q3_damage_scopes *);
bool application_q3_damage_scopes_enter(application_q3_damage_scopes *,
    qa_qvm_function_resolver, void *, qa_error *);
bool application_q3_damage_scopes_leave(application_q3_damage_scopes *, qa_error *);
void application_q3_damage_scopes_health_projected(application_q3_damage_scopes *, qa_actor_id, double);
bool application_q3_damage_scope_run(application_q3_damage_scopes *,
    const application_q3_damage_call *, const application_q3_combat_actor *,
    const qa_damage_request *, qa_damage_observer *, qa_damage_result *, qa_error *);
bool application_q3_damage_scope_reaction(application_q3_damage_scopes *,
    const application_q3_reaction_invocation *, bool *handled, qa_error *);
application_q3_damage_frame *application_q3_damage_scope_current(application_q3_damage_scopes *, uint64_t target);
const qa_damage_request *application_q3_damage_scope_request(const application_q3_damage_frame *);
const application_q3_damage_call *application_q3_damage_scope_call(const application_q3_damage_frame *);
const application_q3_combat_actor *application_q3_damage_scope_actor(const application_q3_damage_frame *);
bool application_q3_damage_scope_cancel(application_q3_damage_frame *, qa_error *);

#endif

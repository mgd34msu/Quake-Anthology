#ifndef QA_APPLICATION_GUEST_Q3_MOD_OPERATIONS_H
#define QA_APPLICATION_GUEST_Q3_MOD_OPERATIONS_H
#include "guest_q3_mod.h"
#include "qa/inventory.h"

typedef struct application_q3_mod_operations application_q3_mod_operations;
typedef struct application_q3_mod_actor_request {
    qa_actor_id self;
    union {
        struct { uint64_t time_ns, elapsed_ns; } think;
        struct { qa_actor_id other; } touch;
        struct { qa_actor_id other, activator; } use;
        struct { qa_actor_id attacker; float damage, kick; } pain;
        struct { qa_actor_id attacker, inflictor; float damage, kick; qa_vec3 point; } die;
    } source;
} application_q3_mod_actor_request;
typedef bool (*application_q3_mod_actor_body)(void *,
    const application_q3_mod_actor_request *, bool *result, qa_error *);

/* One canonical application owner supplies all enabled components. Combat
 * and inventory operations are borrowed; actor channels wrap actual source
 * callbacks through dispatch, and must outlive their registrations. */
bool application_q3_mod_operations_create(qa_session *, qa_combat *, qa_inventory *,
    application_q3_mod_operations **, qa_error *);
bool application_q3_mod_operations_destroy(application_q3_mod_operations **, qa_error *);
bool application_q3_mod_operations_idle(const application_q3_mod_operations *);
bool application_q3_mod_operations_read(application_q3_mod_operations *,
    application_q3_mod_operation_services[Q3_MOD_OPERATION_COUNT], qa_error *);
bool application_q3_mod_actor_dispatch(application_q3_mod_operations *,
    application_q3_mod_operation, const application_q3_mod_actor_request *,
    application_q3_mod_actor_body, void *, bool *, qa_error *);
#endif

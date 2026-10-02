#ifndef QA_APPLICATION_GUEST_Q3_MOD_ACTORS_H
#define QA_APPLICATION_GUEST_Q3_MOD_ACTORS_H
#include "guest_q3_mod_operations.h"
#include "qa/world.h"

typedef struct application_q3_mod_actors application_q3_mod_actors;
typedef struct application_q3_mod_actors_options {
    application_q3_mod_profile *profile;
    application_q3_mod *mod;
    application_q3_mod_operations *operations;
    qa_qvm *vm;
    qa_session *session;
    qa_world *world;
    qa_combat *combat;
    qa_actor_owner owner;
    void *context;
    bool (*current)(void *,qa_error *);
    bool (*owned)(void *,qa_actor_id);
    /* Null actor lowers to zero. Foreign live actors use the genuine declared
     * projection; the component decides whether that projection exists. */
    bool (*pointer)(void *,qa_actor_id,uint32_t *,qa_error *);
    bool (*actor)(void *,int32_t,qa_actor_id *,qa_error *);
    bool (*team)(void *,qa_actor_id,qa_team_id *,qa_error *);
    /* Fill actual canonical attack provenance before the source damage entry
     * enters the shared operation. This owner adds its retained attack ordinal. */
    bool (*damage_context)(void *,qa_damage_request *,qa_error *);
} application_q3_mod_actors_options;

bool application_q3_mod_actors_create(const application_q3_mod_actors_options *,
    bool restoring,application_q3_mod_actors **,qa_error *);
bool application_q3_mod_actors_destroy(application_q3_mod_actors **,qa_error *);
bool application_q3_mod_actors_idle(const application_q3_mod_actors *);
bool application_q3_mod_actors_admit(application_q3_mod_actors *,qa_actor_id,qa_error *);
bool application_q3_mod_actors_before_release(application_q3_mod_actors *,qa_actor_id,qa_error *);
bool application_q3_mod_actors_release(application_q3_mod_actors *,qa_actor_id,qa_error *);
/* Factory owns the one physical resolver/function union. Match reads actual
 * function-pointer fields and the literal target word; hook proceeds once. */
bool application_q3_mod_actors_match(application_q3_mod_actors *,const qa_qvm_call *,bool *,qa_error *);
bool application_q3_mod_actors_hook(application_q3_mod_actors *,const qa_qvm_call *,int32_t *,qa_error *);
/* The physical function composer enters this before any effectful middleware
 * and clears it on every return. The literal call is borrowed only while its
 * actual lower hook is active, never saved or retained after that boundary. */
bool application_q3_mod_actors_entry_begin(application_q3_mod_actors *,const qa_qvm_call *,qa_error *);
void application_q3_mod_actors_entry_end(application_q3_mod_actors *,const qa_qvm_call *);
bool application_q3_mod_actors_damage_entry(const application_q3_mod_actors *,uint32_t *);
/* Already inside the canonical actor operation: these enter only its genuine
 * source body, so native callers do not dispatch the same operation twice. */
bool application_q3_mod_actors_touch(application_q3_mod_actors *,const qa_touch_contact *,bool *,qa_error *);
bool application_q3_mod_actors_callback(application_q3_mod_actors *,application_q3_mod_operation,
    const application_q3_mod_actor_request *,bool *,qa_error *);
bool application_q3_mod_actors_binding(application_q3_mod_actors *,qa_actor_id,
    qa_combat_binding *,qa_error *);
bool application_q3_mod_actors_binding_saved(application_q3_mod_actors *,qa_actor_id,
    uint64_t serial,qa_combat_binding *,qa_error *);
bool application_q3_mod_actors_checkpoint(application_q3_mod_actors *,qa_buffer *,qa_error *);
bool application_q3_mod_actors_restore(application_q3_mod_actors *,qa_bytes,qa_error *);
bool application_q3_mod_actors_finish_restore(application_q3_mod_actors *,qa_error *);
#endif

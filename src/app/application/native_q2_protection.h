#ifndef QA_APPLICATION_NATIVE_Q2_PROTECTION_H
#define QA_APPLICATION_NATIVE_Q2_PROTECTION_H
#include "native_q2_armor.h"
#include "native_q2_records.h"

typedef struct application_native_q2_protection application_native_q2_protection;
typedef struct application_native_q2_protection_options {
    application_native_q2_callbacks *callbacks;
    qa_session *session;
    qa_combat *combat;
    qa_inventory *inventory;
    qa_actor_owner owner;
} application_native_q2_protection_options;
typedef bool (*application_native_q2_protection_execute_fn)(void *,qa_error *);

bool application_native_q2_protection_create(const application_native_q2_protection_options *,
    application_native_q2_protection **,qa_error *);
bool application_native_q2_protection_reserve(application_native_q2_protection *,qa_actor_id,qa_error *);
bool application_native_q2_protection_bind(application_native_q2_protection *,qa_actor_id,qa_error *);
bool application_native_q2_protection_release(application_native_q2_protection *,qa_actor_id,qa_error *);
/* Pickup authorization requires the actual bound canonical reservation. */
bool application_native_q2_protection_item(void *,qa_actor_id,qa_protection_channel,
    qa_item_id,bool *,qa_error *);
bool application_native_q2_protection_observe(application_native_q2_protection *,qa_actor_id,
    qa_protection_observer *,application_native_q2_protection_execute_fn,void *,qa_error *);
bool application_native_q2_protection_observe_pickup(application_native_q2_protection *,qa_actor_id,
    qa_pickup_execution *,application_native_q2_protection_execute_fn,void *,qa_error *);
bool application_native_q2_protection_restored_inventory(application_native_q2_protection *,qa_error *);
bool application_native_q2_protection_checkpoint(application_native_q2_protection *,qa_buffer *,qa_error *);
bool application_native_q2_protection_restore(application_native_q2_protection *,qa_bytes,qa_error *);
bool application_native_q2_protection_saved_binding(application_native_q2_protection *,qa_actor_id,
    qa_protection_channel,const qa_protection_claim *,qa_protection_binding *,qa_error *);
bool application_native_q2_protection_finish_restore(application_native_q2_protection *,qa_error *);
bool application_native_q2_protection_idle(const application_native_q2_protection *);
bool application_native_q2_protection_drain(application_native_q2_protection *,qa_error *);
bool application_native_q2_protection_destroy(application_native_q2_protection **,qa_error *);
#endif

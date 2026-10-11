#ifndef QA_APPLICATION_NATIVE_Q2_CALLBACKS_H
#define QA_APPLICATION_NATIVE_Q2_CALLBACKS_H

#include "qa/native.h"
#include "qa/session.h"
#include "qa/inventory.h"
#include "guest_q3_mod.h"

struct application_native_q2;
struct qa_application;
struct application_native_q2_records;
struct application_native_q2_items;
typedef struct application_native_q2_callbacks application_native_q2_callbacks;
typedef struct application_native_callback_inputs {
    const application_q3_mod_value *values;
    qa_bytes user_command;
} application_native_callback_inputs;
typedef struct application_native_q2_source_authority {
    void *context;
    bool (*current)(void *, qa_error *);
    bool (*retain)(void *, qa_error *);
    void (*release)(void *);
} application_native_q2_source_authority;

/* Owns the acquired callback document. It borrows the actual engine until
 * checked disposal. Calls resolve source addresses anew after original restore. */
bool application_native_q2_callbacks_prepare(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_validate(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_register(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_suspend(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_run(struct application_native_q2 *, const char *section,
    const application_native_callback_inputs *, bool *accepted, qa_error *);
bool application_native_q2_callbacks_call(application_native_q2_callbacks *, qa_json_id,
    const application_native_callback_inputs *, double *, qa_error *);
/* A source call within the real surrounding transfer, used by original item
 * and protection stages which own their own synchronous observations. */
bool application_native_q2_callbacks_call_scoped(application_native_q2_callbacks *, qa_json_id,
    const application_native_callback_inputs *, double *, qa_error *);
bool application_native_q2_callbacks_transfer(application_native_q2_callbacks *,
    bool (*execute)(void *, qa_error *), void *, qa_error *);
bool application_native_q2_callbacks_transfer_current(const application_native_q2_callbacks *);
bool application_native_q2_callbacks_storage_transfer(application_native_q2_callbacks *,
    bool (*execute)(void *, qa_error *), void *, qa_error *);
struct application_native_q2_records *application_native_q2_callbacks_records(
    application_native_q2_callbacks *);
struct application_native_q2_items *application_native_q2_callbacks_items(application_native_q2_callbacks *);
bool application_native_q2_callbacks_components_admit(struct application_native_q2 *,qa_actor_id,qa_error *);
bool application_native_q2_callbacks_components_project(struct application_native_q2 *,qa_actor_id,qa_error *);
bool application_native_q2_callbacks_components_begin(struct application_native_q2 *,qa_actor_id,qa_error *);
bool application_native_q2_client_accepts_attack(struct application_native_q2 *,qa_actor_id,bool *,qa_error *);
bool application_native_q2_callbacks_equipment_restore_prepare(struct qa_application *,qa_error *);
bool application_native_q2_callbacks_protection_saved_binding(struct application_native_q2 *,qa_actor_id,
    qa_protection_channel,const qa_protection_claim *,qa_protection_binding *,qa_error *);
bool application_native_q2_callbacks_pickup_saved_rule(struct application_native_q2 *,qa_actor_id,
    qa_actor_owner,uint64_t,uint32_t,qa_pickup_rule *,qa_error *);
bool application_native_q2_callbacks_inventory_group(struct application_native_q2 *,qa_actor_id,uint64_t,
    const qa_inventory_source_group *,qa_inventory_items *,qa_error *);
qa_native_instance *application_native_q2_callbacks_instance(application_native_q2_callbacks *);
bool application_native_q2_callbacks_scalar_read(application_native_q2_callbacks *,
    qa_native_address, qa_native_value_type, double *, qa_error *);
bool application_native_q2_callbacks_scalar_write(application_native_q2_callbacks *,
    qa_native_address, qa_native_value_type, double, qa_error *);
bool application_native_q2_callbacks_observation_required(const application_native_q2_callbacks *);
bool application_native_q2_callbacks_entry(application_native_q2_callbacks *, qa_json_id,
    qa_native_address *, qa_error *);
bool application_native_q2_callbacks_entry_call(application_native_q2_callbacks *, qa_json_id,
    qa_json_id returns, const qa_native_value *, size_t, bool *entered, qa_error *);
bool application_native_q2_callbacks_input_write(application_native_q2_callbacks *, qa_json_id,
    const application_native_callback_inputs *, qa_native_address, qa_error *);
bool application_native_q2_callbacks_value_validate(application_native_q2_callbacks *, qa_json_id,
    qa_native_value_type *, qa_error *);
application_q3_mod_input application_native_q2_callbacks_input_index(const application_native_q2_callbacks *, qa_json_id);
size_t application_native_q2_callbacks_record_index(const application_native_q2_callbacks *, qa_json_id);
bool application_native_q2_callbacks_record(application_native_q2_callbacks *, qa_actor_id,
    size_t, qa_native_address *, qa_error *);
bool application_native_q2_callbacks_pickup_foreign(application_native_q2_callbacks *,
    const qa_pickup_offer *, qa_error *);
bool application_native_q2_callbacks_pickup_context_address(application_native_q2_callbacks *,
    qa_actor_id, size_t, uint32_t, size_t, qa_native_address *, qa_error *);
bool application_native_q2_callbacks_address(application_native_q2_callbacks *, qa_json_id,
    qa_native_address *, qa_error *);
bool application_native_q2_callbacks_idle(const application_native_q2_callbacks *);
bool application_native_q2_callbacks_current(const application_native_q2_callbacks *);
/* Exact physical source ownership, including during its synchronous call or
 * committed-write callback. This does not certify idle/capture readiness. */
bool application_native_q2_callbacks_storage_current(application_native_q2_callbacks *, qa_error *);
bool application_native_q2_callbacks_client_current(application_native_q2_callbacks *, qa_actor_id, qa_error *);
bool application_native_q2_callbacks_client_live_read(application_native_q2_callbacks *,
    qa_actor_id, bool *, qa_error *);
bool application_native_q2_callbacks_client_reserved_current(application_native_q2_callbacks *, qa_actor_id, qa_error *);
bool application_native_q2_callbacks_restoring(const application_native_q2_callbacks *);
bool application_native_q2_callbacks_time_read(application_native_q2_callbacks *, double *, qa_error *);
bool application_native_q2_callbacks_protection_absorb(application_native_q2_callbacks *, qa_json_id,
    const qa_damage_request *, const qa_damage_geometry *, float, qa_damage_flags,
    const application_native_q2_source_authority *, float *, qa_error *);
bool application_native_q2_callbacks_close(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_drain(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_drain_application(struct qa_application *, qa_error *);
const qa_json_document *application_native_q2_callbacks_document(const application_native_q2_callbacks *);
bool application_native_q2_callbacks_source_before(void *, qa_error *);
bool application_native_q2_callbacks_source_after(void *, qa_error *);
bool application_native_q2_callbacks_release_actor(struct application_native_q2 *, qa_actor_id, qa_error *);
bool application_native_q2_callbacks_capture(struct application_native_q2 *, qa_buffer *, qa_error *);
bool application_native_q2_callbacks_restore(struct application_native_q2 *, qa_bytes, qa_error *);
bool application_native_q2_callbacks_finish_restore(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_arrays_validate(struct application_native_q2 *, qa_error *);
bool application_native_q2_callbacks_reserved_slot(void *,uint32_t,bool *,qa_error *);
bool application_native_q2_callbacks_userinfo_validate(struct application_native_q2 *,const char *,qa_error *);
bool application_native_q2_callbacks_import(void *,const qa_native_import_call *,qa_native_value *,bool *,qa_error *);

#endif

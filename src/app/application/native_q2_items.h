#ifndef QA_APPLICATION_NATIVE_Q2_ITEMS_H
#define QA_APPLICATION_NATIVE_Q2_ITEMS_H
#include "native_q2_armor.h"
#include "qa/inventory.h"

typedef struct application_native_q2_items application_native_q2_items;
typedef struct application_native_q2_item_receipt application_native_q2_item_receipt;
typedef struct application_native_q2_items_options {
    application_native_q2_callbacks *callbacks;
    qa_session *session;
    qa_inventory *inventory;
    qa_pickups *pickups;
    qa_actor_owner owner;
} application_native_q2_items_options;
bool application_native_q2_items_create(const application_native_q2_items_options *,
    application_native_q2_items **,qa_error *);
bool application_native_q2_items_admit(application_native_q2_items *,qa_actor_id,qa_error *);
bool application_native_q2_items_actor_current(void *,qa_actor_id,qa_error *);
bool application_native_q2_items_actor_admitted(const application_native_q2_items *,qa_actor_id);
/* A lexical receipt identifies an actual item-current retirement failure,
 * independently of a native execution error. It pins the reached entry. */
bool application_native_q2_items_receipt_begin(application_native_q2_items *,qa_actor_id,
    application_native_q2_item_receipt **,qa_error *);
bool application_native_q2_items_receipt_retired(const application_native_q2_item_receipt *);
bool application_native_q2_items_receipt_accepts_error(const application_native_q2_item_receipt *,const qa_error *);
bool application_native_q2_items_receipt_source_current(const application_native_q2_item_receipt *,qa_error *);
void application_native_q2_items_receipt_end(application_native_q2_item_receipt **);
bool application_native_q2_items_finish_restore(application_native_q2_items *,qa_error *);
bool application_native_q2_items_release(application_native_q2_items *,qa_actor_id,qa_error *);
bool application_native_q2_items_idle(const application_native_q2_items *);
bool application_native_q2_items_destroy(application_native_q2_items **,qa_error *);
/* The original module saves private count/selection words. Cold canonical
 * group reconstruction imports those same real Source bindings. */
bool application_native_q2_items_inventory_group(application_native_q2_items *,qa_actor_id,
    uint64_t serial,const qa_inventory_source_group *,qa_inventory_items *,qa_error *);
bool application_native_q2_items_definitions(const application_native_q2_items *,
    const qa_item_admission **,size_t *,qa_error *);
qa_json_id application_native_q2_items_weapon_stage(const application_native_q2_items *);
#endif

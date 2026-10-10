#ifndef QA_APPLICATION_GUEST_Q3_MOD_ITEMS_H
#define QA_APPLICATION_GUEST_Q3_MOD_ITEMS_H
#include "guest_q3_mod.h"
#include "qa/inventory.h"
#include "qa/equipment_weapon_slot.h"
#include "qa/qvm_save.h"

typedef struct application_q3_mod_items_profile application_q3_mod_items_profile;
typedef struct application_q3_mod_items application_q3_mod_items;
typedef struct application_q3_mod_items_application application_q3_mod_items_application;
typedef struct application_q3_mod_items_entry application_q3_mod_items_entry;
typedef struct application_q3_item_request {
    qa_actor_id actor;
    uint64_t id;
    qa_item_id item;
} application_q3_item_request;
typedef struct application_q3_items_weapon_view {
    qa_actor_id actor;
    qa_actor_owner provider;
    qa_item_id active,pending;
    bool settled;
} application_q3_items_weapon_view;
typedef struct application_q3_mod_items_services {
    void *context;
    struct qa_application *application;
    bool (*selected)(void *,qa_actor_id);
    bool (*posture)(void *,qa_actor_id,qa_bounds *,double *view_height,int32_t *ground,qa_error *);
    /* Validate actual original pickup writes before their canonical delivery. */
    bool (*pickup_write)(void *,qa_actor_id,qa_item_id,bool count,bool capacity,qa_error *);
    bool (*weapon_bind)(void *,qa_actor_id,application_q3_mod_items *,qa_error *);
    bool (*weapon_unbind)(void *,qa_actor_id,application_q3_mod_items *,qa_error *);
    bool (*weapon_current)(void *,qa_actor_id,const application_q3_mod_items *);
} application_q3_mod_items_services;
/* Absent declarations return success with an absent profile. The real generic
 * profile already owns and qualifies the artifact/declaration pair. */
bool application_q3_mod_items_profile_create(application_q3_mod_profile *,qa_strings *,
    application_q3_mod_items_profile **,qa_error *);
void application_q3_mod_items_profile_destroy(application_q3_mod_items_profile *);
bool application_q3_mod_items_create(application_q3_mod_items_profile *,application_q3_mod *,
    qa_inventory *,const application_q3_mod_items_services *,application_q3_mod_items **,qa_error *);
bool application_q3_mod_items_idle(const application_q3_mod_items *);
bool application_q3_mod_items_destroy(application_q3_mod_items **,qa_error *);
bool application_q3_mod_items_admit(application_q3_mod_items *,qa_actor_id,qa_error *);
bool application_q3_mod_items_release(application_q3_mod_items *,qa_actor_id,qa_error *);
bool application_q3_mod_items_actor_current(application_q3_mod_items *,qa_actor_id);
bool application_q3_mod_items_item_read(application_q3_mod_items *,qa_actor_id,qa_item_id,
    qa_item_admission *,qa_bytes *icon,qa_bytes *held,bool *found,qa_error *);
bool application_q3_mod_items_weapon_read(application_q3_mod_items *,qa_actor_id,
    application_q3_items_weapon_view *,qa_error *);
bool application_q3_mod_items_weapon_accepts(application_q3_mod_items *,qa_actor_id,
    qa_item_id,bool *,qa_error *);
bool application_q3_mod_items_weapon_declares(application_q3_mod_items *,qa_actor_id,
    qa_item_id,bool *,qa_error *);
bool application_q3_mod_items_weapon_holster(application_q3_mod_items *,qa_actor_id,qa_error *);
bool application_q3_mod_items_weapon_holstered(application_q3_mod_items *,qa_actor_id,bool *,qa_error *);
bool application_q3_mod_items_request_restore(application_q3_mod_items *,qa_actor_id,
    uint64_t,qa_item_id,application_q3_item_request *,qa_error *);
bool application_q3_mod_items_request(application_q3_mod_items *,qa_actor_id,qa_item_id,
    application_q3_item_request *,qa_error *);
bool application_q3_mod_items_request_status(application_q3_mod_items *,const application_q3_item_request *,
    qa_weapon_request_status *,qa_error *);
bool application_q3_mod_items_request_cancel(application_q3_mod_items *,const application_q3_item_request *,qa_error *);
bool application_q3_mod_items_requested(application_q3_mod_items *,qa_actor_id,int32_t *,bool *,qa_error *);
bool application_q3_mod_items_open(application_q3_mod_items *,qa_actor_id,
    application_q3_mod_application *,qa_unified_frame_lease *,application_q3_mod_items_application **,qa_error *);
bool application_q3_mod_items_close(application_q3_mod_items_application **,qa_error *);
bool application_q3_mod_items_apply(application_q3_mod_items_application *,uint32_t entry,
    const application_q3_mod_inputs *,qa_error *);
bool application_q3_mod_items_applies(const application_q3_mod_items *,uint32_t entry);
size_t application_q3_mod_items_entry_count(const application_q3_mod_items *);
bool application_q3_mod_items_entry_instruction(const application_q3_mod_items *,size_t,uint32_t *);
bool application_q3_mod_items_entry_begin(application_q3_mod_items *,const qa_qvm_call *,
    application_q3_mod_items_entry **,qa_error *);
bool application_q3_mod_items_entry_end(application_q3_mod_items_entry **,bool succeeded,qa_error *);
typedef bool (*application_q3_mod_items_proceed)(void *,const qa_qvm_call *,int32_t *,qa_error *);
bool application_q3_mod_items_hook_run(application_q3_mod_items *,const qa_qvm_call *,
    application_q3_mod_items_proceed,void *,int32_t *,qa_error *);
size_t application_q3_mod_items_definition_count(const application_q3_mod_items_profile *);
bool application_q3_mod_items_definition(const application_q3_mod_items_profile *,size_t,
    qa_item_admission *,qa_bytes *icon,qa_bytes *held,qa_error *);
bool application_q3_mod_items_checkpoint(application_q3_mod_items *,qa_buffer *,qa_error *);
bool application_q3_mod_items_restore(application_q3_mod_items *,qa_bytes,qa_error *);
bool application_q3_mod_items_finish_restore(application_q3_mod_items *,qa_error *);
size_t application_q3_mod_items_watch_count(const application_q3_mod_items *);
bool application_q3_mod_items_watch(const application_q3_mod_items *,size_t,
    qa_qvm_saved_write_watch *,qa_qvm_binding *saved,qa_error *);
bool application_q3_mod_items_watches_adopt(application_q3_mod_items *,qa_error *);
bool application_q3_mod_items_inventory_group(application_q3_mod_items *,qa_actor_id,
    uint64_t serial,const qa_inventory_source_group *,qa_inventory_items *,qa_error *);
#endif

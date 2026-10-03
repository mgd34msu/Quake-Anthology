#ifndef APPLICATION_GUEST_QC_ITEMS_H
#define APPLICATION_GUEST_QC_ITEMS_H
#include "guest_qc_profile.h"
#include "qa/inventory.h"
typedef struct application_qc_item_definition {
    application_qc_call actions[2];
    qa_buffer icon, held;
} application_qc_item_definition;
typedef struct application_qc_item_bit { qa_item_id item; uint32_t mask; } application_qc_item_bit;
typedef struct application_qc_item_storage {
    const qa_qc_definition *field, *capacity;
    qa_item_id item;
    float constant_capacity;
    bool bits;
    uint32_t private_mask;
    application_qc_item_bit *items;
    size_t count;
} application_qc_item_storage;
struct application_qc_items {
    qa_item_admission *admissions;
    application_qc_item_definition *definitions;
    size_t definition_count;
    application_qc_item_storage *storage;
    size_t storage_count;
    struct application_qc_item_weapons *weapons;
};
bool application_qc_items_qualify(application_provider *, const qa_json_document *, qa_json_id, qa_error *);
void application_qc_items_profile_free(struct application_qc_items *);
bool application_qc_items_actor_current(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_items_admit(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_items_release(struct application_qc_state *, qa_actor_id, qa_error *);
bool application_qc_items_close(struct application_qc_state *, qa_error *);
bool application_qc_items_source_stored(struct application_qc_state *, qa_qc_instance *, const qa_qc_store_event *, qa_error *);
bool application_qc_items_restore_finish(application_provider *,qa_error *);
bool application_qc_items_saved_group(application_provider *, qa_actor_id, uint64_t,
    const qa_inventory_source_group *, qa_inventory_items *, qa_error *);
bool application_qc_items_field_permission(struct application_qc_state *, qa_actor_id, qa_item_id,
    bool count, bool capacity, qa_error *);
bool application_qc_items_item_read(application_provider *, qa_actor_id, qa_item_id,
    qa_item_admission *, qa_bytes *icon, qa_bytes *held, bool *found, qa_error *);
#endif

#ifndef QA_EQUIPMENT_WEAPON_SLOT_H
#define QA_EQUIPMENT_WEAPON_SLOT_H
#include "qa/inventory.h"
#include "qa/source_save.h"

typedef struct qa_weapon_slot qa_weapon_slot;
typedef enum qa_weapon_slot_phase {
    QA_WEAPON_SLOT_ACTIVE, QA_WEAPON_SLOT_SWITCHING, QA_WEAPON_SLOT_ACTIVATING
} qa_weapon_slot_phase;
typedef enum qa_weapon_request_status {
    QA_WEAPON_REQUEST_PENDING, QA_WEAPON_REQUEST_ACCEPTED, QA_WEAPON_REQUEST_REFUSED
} qa_weapon_request_status;
typedef struct qa_weapon_slot_state {
    qa_weapon_slot_phase phase;
    qa_actor_owner provider, next_provider;
    qa_item_id next_item;
    uint64_t request;
} qa_weapon_slot_state;
typedef struct qa_weapon_presentation {
    qa_actor_owner provider;
    qa_item_id active, pending;
    /* The source owns any model/resource data reached through this context. */
    void *context;
    qa_actor_id actor;
    uint64_t binding_serial;
} qa_weapon_presentation;
typedef struct qa_equipment_weapon_binding {
    qa_actor_owner provider;
    void *context;
    bool source_input;
    bool (*current)(void *, qa_actor_id);
    bool (*read)(void *, qa_actor_id, qa_weapon_presentation *, qa_error *);
    bool (*declares)(void *, qa_actor_id, qa_item_id, bool *, qa_error *);
    bool (*accepts)(void *, qa_actor_id, qa_item_id, bool *, qa_error *);
    bool (*select)(void *, qa_actor_id, qa_item_id, bool *, qa_error *);
    bool (*holster)(void *, qa_actor_id, qa_error *);
    bool (*holstered)(void *, qa_actor_id, bool *, qa_error *);
    /* Immediate sources return accepted. Input sources return their real ID. */
    bool (*resume)(void *, qa_actor_id, qa_item_id, bool *, uint64_t *, qa_error *);
    bool (*status)(void *, qa_actor_id, uint64_t, qa_item_id, qa_weapon_request_status *, qa_error *);
    bool (*cancel)(void *, qa_actor_id, uint64_t, qa_item_id, qa_error *);
    bool (*restore_request)(void *, qa_actor_id, uint64_t, qa_item_id, qa_error *);
} qa_equipment_weapon_binding;

bool qa_weapon_slot_create(qa_actor_registry *, qa_actor_id,
    const qa_equipment_weapon_binding *primary, const qa_weapon_slot_state *restored,
    qa_weapon_slot **, qa_error *);
bool qa_weapon_slot_destroy(qa_weapon_slot **, qa_error *);
bool qa_weapon_slot_idle(const qa_weapon_slot *);
bool qa_weapon_slot_bind(qa_weapon_slot *, const qa_equipment_weapon_binding *, qa_error *);
bool qa_weapon_slot_unbind(qa_weapon_slot *, qa_actor_owner, void *exact_context, qa_error *);
bool qa_weapon_slot_request(qa_weapon_slot *, qa_actor_owner, qa_item_id, bool *, qa_error *);
bool qa_weapon_slot_return_primary(qa_weapon_slot *, qa_error *);
bool qa_weapon_slot_reset_primary(qa_weapon_slot *, qa_error *);
bool qa_weapon_slot_restore(qa_weapon_slot *, const qa_weapon_slot_state *, qa_error *);
bool qa_weapon_slot_reconcile(qa_weapon_slot *, qa_error *);
bool qa_weapon_slot_validate_restore(qa_weapon_slot *, qa_error *);
bool qa_weapon_slot_selected(const qa_weapon_slot *, qa_actor_owner);
bool qa_weapon_slot_presented(const qa_weapon_slot *, qa_actor_owner);
bool qa_weapon_slot_primary_selected(const qa_weapon_slot *);
bool qa_weapon_slot_binding_is(const qa_weapon_slot *, qa_actor_owner, const void *exact_context);
bool qa_weapon_slot_snapshot(const qa_weapon_slot *, qa_weapon_slot_state *);
size_t qa_weapon_slot_binding_count(const qa_weapon_slot *);
bool qa_weapon_slot_presentation(qa_weapon_slot *, size_t, qa_weapon_presentation *, bool *, qa_error *);
bool qa_weapon_slot_presentation_current(qa_weapon_slot *, const qa_weapon_presentation *);
bool qa_weapon_slot_state_fields(qa_source_save_io *, qa_weapon_slot_state *);
#endif

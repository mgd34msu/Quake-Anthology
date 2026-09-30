#ifndef QA_INVENTORY_INTERNAL_H
#define QA_INVENTORY_INTERNAL_H
#include "qa/inventory.h"
#include "qa/operation.h"

typedef struct item_group {
    struct item_group *next;
    uint64_t serial;
    bool active, definitions_only;
    qa_actor_owner owner;
    qa_item_admission *items;
    size_t count;
    qa_inventory_binding binding;
    void *action_context;
    bool (*invoke)(void *, qa_item_id, qa_item_action, qa_error *);
} item_group;
typedef struct inventory_store {
    qa_actor_id actor;
    uint64_t serial;
    uint64_t revision;
    size_t references;
    bool local, attached;
    qa_inventory_entry *entries;
    size_t count, capacity;
    qa_inventory_binding primary;
    item_group *groups;
    struct pickup_claim *pickups;
} inventory_store;
typedef struct admission_entry {
    qa_inventory_entry initial;
    item_group *owner;
    bool missing;
} admission_entry;
struct qa_inventory_admission {
    qa_inventory *table;
    inventory_store *store;
    uint64_t revision;
    size_t count, missing;
    bool validated;
    admission_entry entries[];
};
typedef struct pickup_claim {
    struct pickup_claim *next;
    qa_item_id item;
    const void *token;
} pickup_claim;
struct qa_inventory {
    qa_actor_registry *actors;
    inventory_store **stores;
    uint32_t capacity;
    uint64_t serial;
    size_t calls;
    qa_operation *operations[4];
};

/* Storage identity includes the primary binding, even when no component owns
 * the item. Pickup leases use it to reject storage replaced during callbacks. */
bool qa_inventory_storage_token(qa_inventory *, qa_actor_id, qa_item_id,
                                uint64_t *, qa_error *);
bool qa_inventory_pickup_claim(qa_inventory *, qa_actor_id, qa_item_id, const void *, qa_error *);
bool qa_inventory_pickup_current(qa_inventory *, qa_actor_id, qa_item_id, const void *);
void qa_inventory_pickup_release(qa_inventory *, qa_actor_id, const void *);
void qa_inventory_hold(qa_inventory *);
void qa_inventory_unhold(qa_inventory *);
inventory_store *qa_inventory_checkpoint_acquire(qa_inventory *, qa_actor_id);
void qa_inventory_checkpoint_release(qa_inventory *, inventory_store *);
#endif

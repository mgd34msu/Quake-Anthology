#ifndef QA_INVENTORY_INTERNAL_H
#define QA_INVENTORY_INTERNAL_H
#include "qa/inventory.h"
#include "qa/operation.h"
#include "qa/pool.h"
#include "qa/id_index.h"

typedef struct inventory_source_index {
    qa_arena storage;
    qa_id_index items;
    uint64_t revision;
    qa_inventory *owner;
    size_t storage_slot;
    bool ready, validated;
} inventory_source_index;

typedef struct item_group {
    struct item_group *next;
    uint64_t serial;
    bool active, definitions_only;
    qa_actor_owner owner;
    qa_item_admission *items;
    size_t count;
    size_t order;
    qa_arena index_storage;
    qa_id_index item_index;
    inventory_source_index *source_index;
    qa_inventory_binding binding;
    void *action_context;
    bool (*invoke)(void *, qa_item_id, qa_item_action, qa_error *);
} item_group;
typedef struct inventory_store {
    qa_actor_id actor;
    uint64_t serial;
    uint64_t revision, definition_revision;
    size_t references;
    bool local, attached;
    qa_inventory *storage_owner;
    size_t storage_slot;
    qa_inventory_entry *entries;
    qa_inventory *entries_owner;
    size_t entries_slot, entries_pages;
    size_t count, capacity;
    qa_arena index_storage;
    qa_id_index local_items, definitions, pickup_items;
    inventory_source_index *primary_index;
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
    size_t storage_slot, storage_pages;
    bool validated;
    admission_entry entries[];
};
typedef struct pickup_claim {
    struct pickup_claim *next;
    qa_item_id item;
    const void *token;
    struct pickup_claim **link;
    size_t storage_slot;
} pickup_claim;
struct qa_inventory {
    qa_actor_registry *actors;
    inventory_store **stores;
    uint32_t capacity;
    uint64_t serial;
    size_t calls;
    qa_operation *operations[4];
    qa_arena record_storage;
    qa_pool store_records, admission_pages, source_records, pickup_records;
    qa_arena scratch_storage;
    qa_pool scratch_pages;
    size_t storage_leases;
    bool closed;
};

static inline void qa_inventory_scratch_begin(qa_inventory *table, qa_arena *storage)
{ qa_arena_init_pool(storage, &table->scratch_pages); }
static inline void qa_inventory_scratch_acquire(qa_inventory *table, qa_arena *storage)
{ ++table->storage_leases; qa_inventory_scratch_begin(table, storage); }

/* Storage identity includes the primary binding, even when no component owns
 * the item. Pickup leases use it to reject storage replaced during callbacks. */
bool qa_inventory_storage_token(qa_inventory *, qa_actor_id, qa_item_id,
                                uint64_t *, qa_error *);
bool qa_inventory_pickup_claim(qa_inventory *, qa_actor_id, qa_item_id, const void *, qa_error *);
bool qa_inventory_pickup_current(qa_inventory *, qa_actor_id, qa_item_id, const void *);
bool qa_inventory_pickup_storage_current(const qa_inventory *,qa_actor_id,qa_item_id,
                                        const void *,uint64_t,qa_actor_owner);
void qa_inventory_pickup_release(qa_inventory *, qa_actor_id, const void *);
void qa_inventory_hold(qa_inventory *);
void qa_inventory_unhold(qa_inventory *);
bool qa_inventory_store_reserve_entries(qa_inventory *, inventory_store *, size_t, qa_error *);
inventory_store *qa_inventory_store_create(qa_inventory *, qa_error *);
void qa_inventory_store_drop(inventory_store *);
inventory_store *qa_inventory_checkpoint_acquire(qa_inventory *, qa_actor_id);
void qa_inventory_checkpoint_release(qa_inventory *, inventory_store *);
bool qa_inventory_store_index_restore(qa_inventory *, inventory_store *, qa_error *);
#endif

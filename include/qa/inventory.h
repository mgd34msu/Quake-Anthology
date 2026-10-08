#ifndef QA_INVENTORY_H
#define QA_INVENTORY_H
#include "qa/gameplay.h"

typedef enum qa_inventory_count_policy { QA_COUNT_STACK, QA_COUNT_SOURCE_FLOAT, QA_COUNT_SOURCE_INT32, QA_COUNT_SOURCE_DOUBLE } qa_inventory_count_policy;
/* Double API values retain every signed int32 counter exactly. SOURCE_FLOAT
 * stores round to native float; this is not a selectable arithmetic backend. */
typedef struct qa_inventory_entry { qa_item_id item; double count, capacity; qa_inventory_count_policy policy; } qa_inventory_entry;
typedef struct qa_inventory_binding {
    void *context;
    size_t (*count)(void *);
    bool (*at)(void *, size_t, qa_inventory_entry *, qa_error *);
    bool (*write)(void *, const qa_inventory_entry *, qa_error *);
    bool (*mutable_capacity)(void *, qa_item_id);
    /* Optional source reader; preferred over count when present. */
    bool (*checked_count)(void *, size_t *, qa_error *);
    /* Optional original acquisition policy. Preview is pure; publish commits
     * the actual Source result under this same binding. Unhandled entries use
     * the ordinary capacity rule. writes includes genuine companion effects;
     * item, capacity and count policy stay fixed. */
    bool (*acquire)(void *, const qa_inventory_entry *, double amount, bool publish,
                    qa_inventory_entry *, bool *handled, bool *writes, qa_error *);
} qa_inventory_binding;
typedef enum qa_item_action { QA_ITEM_USE = 1, QA_ITEM_DROP = 2 } qa_item_action;
typedef struct qa_item_definition {
    qa_item_id item, ammo;
    qa_actor_owner owner;
    const char *label;
    bool weapon;
    uint32_t actions;
} qa_item_definition;
typedef struct qa_item_admission { qa_item_definition definition; bool replace_primary; } qa_item_admission;
typedef struct qa_inventory_items {
    qa_actor_owner owner;
    const qa_item_admission *items;
    size_t count;
    qa_inventory_binding state;
    void *action_context;
    bool (*invoke)(void *, qa_item_id, qa_item_action, qa_error *);
} qa_inventory_items;
typedef struct qa_inventory_lease { qa_actor_id actor; uint64_t serial; } qa_inventory_lease;
typedef struct qa_inventory_change { bool had_before; qa_actor_id actor; qa_inventory_entry before, after; } qa_inventory_change;
typedef bool (*qa_inventory_committed_fn)(void *, const qa_inventory_change *, qa_error *);
typedef enum qa_inventory_operation_kind { QA_INVENTORY_GIVE, QA_INVENTORY_CONSUME, QA_INVENTORY_CONFIGURE, QA_INVENTORY_ADJUST } qa_inventory_operation_kind;
typedef struct qa_inventory_request { qa_actor_id actor; qa_item_id item; double amount; qa_inventory_entry entry; } qa_inventory_request;
typedef struct qa_inventory_result { double amount; bool consumed; } qa_inventory_result;
typedef struct qa_inventory_admission qa_inventory_admission;

/* Setup preserves admitted values. New native entries belong to canonical
 * storage, including on actors whose primary inventory is external.
 * Prepare/validate may call source readers; commit allocates nothing and calls
 * no provider. Success consumes the token; abort consumes an uncommitted token. */
bool qa_inventory_prepare_entries(qa_inventory *, qa_actor_id, const qa_inventory_entry *,
                                  size_t, qa_inventory_admission **, qa_error *);
bool qa_inventory_admission_validate(qa_inventory_admission *, qa_error *);
bool qa_inventory_admission_commit(qa_inventory_admission *, qa_error *);
void qa_inventory_admission_abort(qa_inventory_admission *);

bool qa_inventory_create(qa_actor_registry *, qa_inventory **, qa_error *);
/* Pure callback/admission lifetime read; an absent owner is idle. */
bool qa_inventory_idle(const qa_inventory *);
bool qa_inventory_destroy(qa_inventory *, qa_error *);
void qa_inventory_actor_released(qa_inventory *, qa_actor_record);
bool qa_inventory_create_actor(qa_inventory *, qa_actor_id, const qa_inventory_entry *, size_t, qa_error *);
bool qa_inventory_bind(qa_inventory *, qa_actor_id, const qa_inventory_binding *, qa_error *);
/* Adopt a local primary without replacing item groups or native-only entries.
 * Overlapping counts move into the source's declared policy and capacity.
 * Detach snapshots the exact source binding back into canonical local storage.
 * Both operations require the actor's inventory callbacks to have drained. */
bool qa_inventory_adopt_primary(qa_inventory *, qa_actor_id, const qa_inventory_binding *,
                                qa_inventory_lease *, qa_error *);
bool qa_inventory_detach_primary(qa_inventory *, qa_inventory_lease, void *context, qa_error *);
/* Read-only identity check for an actually published external primary. It
 * calls no source callbacks and does not recognize prepared private claims. */
bool qa_inventory_primary_current(qa_inventory *, qa_inventory_lease, const void *context);
bool qa_inventory_bind_items(qa_inventory *, qa_actor_id, const qa_inventory_items *, qa_inventory_lease *, qa_error *);
/* Native definitions share primary count storage. An explicitly admitted
 * external item override shadows these actions until its lease closes.
 * Closing definitions never removes counts or another owner's pickup claim. */
bool qa_inventory_bind_definitions(qa_inventory *, qa_actor_id, qa_actor_owner,
    const qa_item_definition *, size_t,
    bool (*invoke)(void *, qa_item_id, qa_item_action, qa_error *), void *context,
    qa_inventory_lease *, qa_error *);
bool qa_inventory_replace_definitions(qa_inventory *, qa_actor_id, qa_actor_owner,
    const qa_item_definition *, size_t,
    bool (*invoke)(void *, qa_item_id, qa_item_action, qa_error *), void *context,
    qa_inventory_lease previous, qa_inventory_lease *, qa_error *);
bool qa_inventory_lease_current(qa_inventory *, qa_inventory_lease);
bool qa_inventory_close_items(qa_inventory *, qa_inventory_lease, qa_error *);
bool qa_inventory_source_stored(qa_inventory *, qa_inventory_lease, const qa_inventory_change *, size_t, qa_error *);
/* retired_token identifies failure at a source lease retirement guard, not a
 * failure returned by a source reader or a configure callback. */
bool qa_inventory_source_stored_ex(qa_inventory *, qa_inventory_lease, const qa_inventory_change *, size_t,
    bool *retired_token, qa_error *);
bool qa_inventory_entry_read(qa_inventory *, qa_actor_id, qa_item_id, qa_inventory_entry *, qa_error *);
/* A current actor with no store or a missing item has count zero. Retired
 * actors and changes to storage or item ownership during a read fail. */
bool qa_inventory_count_read(qa_inventory *, qa_actor_id, qa_item_id, double *, qa_error *);
bool qa_inventory_entries(qa_inventory *, qa_actor_id, qa_inventory_entry *, size_t capacity, size_t *count, qa_error *);
bool qa_inventory_has(const qa_inventory *, qa_actor_id);
bool qa_inventory_mutable_capacity(qa_inventory *, qa_actor_id, qa_item_id);
/* Only component overrides return true; serial is the exact storage lease. */
bool qa_inventory_item_owner(qa_inventory *, qa_actor_id, qa_item_id, qa_actor_owner *, uint64_t *serial);
bool qa_inventory_item_action(qa_inventory *, qa_actor_id, qa_item_id, qa_item_action, qa_error *);
/* Returned definition labels are borrowed until the group closes or its actor
 * retires. Use source_items for an owned checkpoint snapshot. */
bool qa_inventory_item_definitions(qa_inventory *, qa_actor_id, qa_item_definition *, size_t, size_t *, qa_error *);
/* Pure observation of an admitted definition. Labels remain
 * borrowed until that group closes. No source reader or storage mutation runs.
 * Missing storage or a missing declaration returns found=false and leaves the
 * definition output unchanged. A NULL owner selects the visible definition;
 * otherwise the lookup uses that exact owner, including owner zero. */
bool qa_inventory_item_definition_find(const qa_inventory *, qa_actor_id,
    const qa_actor_owner *, qa_item_id, qa_item_definition *, bool *found, qa_error *);
/* Required exact-owner lookup; missing storage or definition fails. */
bool qa_inventory_source_definition_read(const qa_inventory *, qa_actor_id,
    qa_actor_owner, qa_item_id, qa_item_definition *, qa_error *);
bool qa_inventory_give(qa_inventory *, qa_actor_id, qa_item_id, double, double *given, qa_error *);
bool qa_inventory_consume(qa_inventory *, qa_actor_id, qa_item_id, double, bool *consumed, qa_error *);
bool qa_inventory_configure(qa_inventory *, qa_actor_id, const qa_inventory_entry *, qa_inventory_committed_fn, void *, qa_error *);
bool qa_inventory_adjust(qa_inventory *, qa_actor_id, qa_item_id, double, double *, qa_error *);
qa_operation *qa_inventory_operation(qa_inventory *, qa_inventory_operation_kind);
bool qa_inventory_validate_entry(const qa_inventory_entry *, qa_inventory_entry *normalized, qa_error *);
bool qa_inventory_preview_give(const qa_inventory_entry *, double, qa_inventory_entry *, double *, bool *writes, qa_error *);
/* Owner-qualified preview for a retained supply's simulated entry. */
bool qa_inventory_preview_acquire(qa_inventory *, qa_actor_id, const qa_inventory_entry *,
    double, qa_inventory_entry *, double *, bool *, qa_error *);
typedef struct qa_inventory_source_group { qa_actor_owner owner; qa_item_admission *items; size_t count; bool definitions_only; } qa_inventory_source_group;
typedef struct qa_inventory_source_snapshot {
    qa_inventory_entry *primary;
    size_t primary_count, primary_native_count;
    bool primary_external;
    qa_inventory_source_group *groups;
    size_t group_count;
} qa_inventory_source_snapshot;
bool qa_inventory_source_items(qa_inventory *, qa_actor_id, qa_inventory_source_snapshot *, qa_error *);
void qa_inventory_source_snapshot_free(qa_inventory_source_snapshot *);

typedef struct qa_pickup_grant { qa_item_id item; double amount; } qa_pickup_grant;
typedef struct qa_pickup_receipt { qa_item_id item; double before, given; } qa_pickup_receipt;
typedef enum qa_pickup_selection_mode { QA_PICKUP_SWITCH_NEVER, QA_PICKUP_SWITCH_ALWAYS, QA_PICKUP_SWITCH_IF_BETTER } qa_pickup_selection_mode;
typedef struct qa_supply_mapping { qa_item_id source; const qa_item_id *destinations; size_t count; } qa_supply_mapping;
typedef struct qa_supply_source_owner { qa_item_id item, source; } qa_supply_source_owner;
typedef struct qa_supply_profile {
    const qa_supply_mapping *weapons, *ammo;
    size_t weapon_count, ammo_count;
    const qa_supply_source_owner *weapon_owners, *ammo_owners;
    size_t weapon_owner_count, ammo_owner_count;
} qa_supply_profile;
typedef struct qa_supply_hooks {
    void *context;
    bool (*ammo_granted)(void *, qa_actor_id, const qa_pickup_receipt *, size_t, bool auto_switch, qa_error *);
    bool (*weapon_granted)(void *, qa_actor_id, const qa_item_id *, size_t, qa_pickup_selection_mode, qa_error *);
    /* Pure qualification of the actual provider/actor association. Called
     * while the supplier is held, before grants and after source callbacks. */
    bool (*current)(void *, qa_actor_id, qa_error *);
} qa_supply_hooks;
typedef struct qa_supply qa_supply;
bool qa_supply_create(qa_inventory *, const qa_supply_profile *, const qa_supply_hooks *, qa_supply **, qa_error *);
void qa_supply_destroy(qa_supply *);
bool qa_supply_idle(const qa_supply *);
bool qa_supply_maps(const qa_supply *, qa_item_id, bool weapon);
bool qa_supply_owns(qa_supply *, qa_actor_id, qa_item_id, bool *, qa_error *);
bool qa_supply_ammo(qa_supply *, qa_actor_id, qa_pickup_grant, bool, bool *, qa_error *);
bool qa_supply_ammo_weapon(qa_supply *, qa_actor_id, qa_item_id weapon, qa_pickup_grant, qa_pickup_selection_mode, bool only_empty, bool *, qa_error *);
bool qa_supply_cargo(qa_supply *, qa_actor_id, const qa_pickup_cargo *, size_t,
                      qa_pickup_selection_mode, bool canonical, bool *accepted, qa_error *);
bool qa_supply_select_weapon(qa_supply *, qa_actor_id, qa_item_id, qa_pickup_selection_mode, qa_error *);
typedef struct qa_supply_quantity { double amount; bool exact, accepted; } qa_supply_quantity;
typedef bool (*qa_supply_quantity_fn)(void *, const qa_inventory_entry *, qa_supply_quantity *, qa_error *);
typedef enum qa_supply_offer_kind { QA_SUPPLY_AMMO, QA_SUPPLY_AMMO_WEAPON, QA_SUPPLY_WEAPON } qa_supply_offer_kind;
typedef struct qa_supply_offer {
    qa_supply_offer_kind kind;
    qa_item_id item, weapon;
    const qa_pickup_grant *ammo;
    size_t ammo_count;
} qa_supply_offer;
typedef struct qa_supply_options {
    qa_pickup_selection_mode selection;
    bool auto_switch, only_empty, canonical;
    qa_supply_quantity_fn quantity;
    void *quantity_context;
} qa_supply_options;
typedef struct qa_supply_preview_result {
    bool accepted;
    qa_pickup_receipt *weapons, *ammo;
    size_t weapon_count, ammo_count;
} qa_supply_preview_result;
typedef struct qa_pickup_grant_plan {
    bool weapon_offer, accept_nonzero, shared_weapon_ammo;
    const qa_pickup_grant *weapons, *ammo;
    size_t weapon_count, ammo_count;
} qa_pickup_grant_plan;
/* Shared-ammo plans use weapon item IDs only. Every weapon must also have an
 * ammo grant; receipt reuse preserves the original source's pickup rules. */
bool qa_pickup_preview_grants(const qa_inventory_entry *, size_t, const qa_pickup_grant_plan *, qa_supply_preview_result *, qa_error *);
bool qa_supply_apply(qa_supply *, qa_actor_id, const qa_supply_offer *, const qa_supply_options *, bool *, qa_error *);
bool qa_supply_preview(qa_supply *, qa_actor_id, const qa_supply_offer *, bool canonical, qa_supply_preview_result *, qa_error *);
bool qa_supply_cargo_preview(qa_supply *, qa_actor_id, const qa_pickup_cargo *, size_t,
                              bool canonical, qa_supply_preview_result *, qa_error *);
void qa_supply_preview_free(qa_supply_preview_result *);
typedef struct qa_supply_weapon { qa_item_id item, ammo; bool drop; } qa_supply_weapon;
/* Outputs parallel the selected array; zero means the source cannot drop it.
 * Ambiguous/missing mappings fail instead of selecting by declaration order. */
bool qa_supply_weapon_sources(const qa_supply_profile *, const qa_supply_weapon *selected, size_t selected_count,
                              const qa_supply_weapon *original, size_t original_count, qa_item_id *sources, qa_error *);
#endif

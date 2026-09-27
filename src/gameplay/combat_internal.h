#ifndef QA_COMBAT_INTERNAL_H
#define QA_COMBAT_INTERNAL_H
#include "qa/gameplay.h"
#include "qa/operation.h"

typedef struct qa_combat_protection {
    uint64_t serial;
    qa_protection_claim claim;
    qa_protection_binding binding;
    bool reserved, bound;
} qa_combat_protection;
typedef struct qa_combat_record {
    qa_actor_id actor;
    uint64_t serial;
    bool active, external, power_admitting;
    qa_combat_state state;
    qa_combat_binding binding;
    qa_inventory *power_inventory;
    qa_item_id power_item;
    qa_combat_protection protection[2];
} qa_combat_record;
typedef struct qa_combat_cursor {
    struct qa_combat_cursor *previous;
    qa_damage_outcome *outcome;
    qa_combat_state observed;
    qa_vec3 velocity;
    size_t journal_capacity;
    uint64_t binding_serial;
    bool active, has_velocity, reaction_seen;
    qa_damage_result reaction;
} qa_combat_cursor;
struct qa_combat {
    qa_actor_registry *actors;
    qa_combat_record *records;
    qa_combat_hooks hooks;
    qa_combat_policy *policies;
    size_t policy_count, policy_capacity;
    qa_combat_policy_admission *admissions;
    size_t admission_count;
    size_t active_calls, active_hits;
    uint64_t next_serial;
    qa_operation *damage;
    qa_combat_cursor *current;
    qa_combat_pickup_scope *pickups;
};
struct qa_damage_observer {
    qa_combat *combat;
    qa_combat_cursor *cursor;
    bool open, failed;
    qa_error failure;
};
struct qa_protection_observer {
    qa_combat *combat;
    qa_combat_cursor *cursor;
    qa_protection_lease lease;
    qa_actor_owner owner;
    bool open, failed;
    qa_error failure;
};
bool qa_combat_argument(qa_error *, const char *);
bool qa_combat_live(qa_combat *, qa_actor_id);
bool qa_combat_policy_execute(qa_combat *, const qa_combat_policy *, const qa_damage_request *, qa_damage_result *, qa_error *);
bool qa_combat_impulse(qa_combat *, const qa_damage_request *, qa_vec3, float, qa_error *);
#endif

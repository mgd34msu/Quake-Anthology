#ifndef QA_PICKUPS_INTERNAL_H
#define QA_PICKUPS_INTERNAL_H
#include "inventory_internal.h"

typedef struct pickup_rule {
    qa_pickup_rule rule;
    uint64_t *storage;
} pickup_rule;
typedef struct pickup_registration {
    struct pickup_registration *next;
    qa_actor_id actor;
    qa_actor_owner owner;
    uint64_t serial;
    bool active;
    pickup_rule *rules;
    size_t count;
} pickup_registration;
typedef enum pickup_consumption { PICKUP_LIVE, PICKUP_REMOVING, PICKUP_CONSUMED } pickup_consumption;
typedef struct pickup_observation_owner {
    struct pickup_observation_owner *retired_next;
    qa_actor_id actor;
    qa_actor_owner owner;
    uint64_t serial;
    qa_pickup_observer observer;
    bool active;
} pickup_observation_owner;
struct qa_pickup_execution {
    struct qa_pickup_execution *previous;
    qa_pickups *service;
    qa_pickup_offer offer;
    pickup_registration *registration;
    pickup_rule *rule;
    qa_pickup_selection selection;
    qa_combat_pickup_scope combat_scope;
    pickup_consumption consumption;
    bool open, accepted, grant_used, observer_open, source_scope, failed;
    qa_error failure;
};
struct qa_pickups {
    qa_actor_registry *actors;
    qa_combat *combat;
    qa_inventory *inventory;
    pickup_registration *registrations;
    qa_pickup_execution *scopes;
    uint64_t serial;
    size_t calls;
    pickup_observation_owner **observations, *retired_observations;
    uint32_t observation_capacity;
    size_t observation_calls;
    void *eligibility_context;
    bool (*eligible)(void *, const qa_pickup_offer *, bool *, qa_error *);
};
bool qa_pickups_checkpoint_write_current(qa_pickups *, pickup_registration *, const qa_pickup_write *, uint64_t);
#endif

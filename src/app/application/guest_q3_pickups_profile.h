#ifndef QA_APPLICATION_GUEST_Q3_PICKUPS_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_PICKUPS_PROFILE_H
#include "qa/qvm.h"

struct q3g_role;
typedef struct application_q3_pickup_branch { uint32_t instruction; bool taken; } application_q3_pickup_branch;
typedef struct application_q3_pickup_function {
    uint32_t entry, *calls;
    size_t call_count;
} application_q3_pickup_function;
typedef struct application_q3_pickup_grant {
    int32_t item_type;
    application_q3_pickup_function function;
    bool region, weapon, inventory;
    uint32_t accepted_return, entry, join, quantity, bits_offset, ammo_offset;
    int32_t return_value;
    qa_qvm_region_evaluation evaluation;
    uint32_t *inputs;
    application_q3_pickup_branch *branches;
    size_t branch_count;
    enum { Q3_PICKUP_ELIGIBLE, Q3_PICKUP_SOURCE_BRANCHES,
        Q3_PICKUP_THREEWAVE_OWNER, Q3_PICKUP_THREEWAVE_LITHIUM } eligibility;
} application_q3_pickup_grant;
typedef struct application_q3_pickup_profile {
    const qa_qvm_image *image;
    qa_qvm_abi abi;
    uint32_t entity_stride, client_stride;
    struct { uint32_t inuse, client, health, item, count, flags; } fields;
    uint32_t dropped_flag, touch, free, item_argument, player_argument;
    int32_t weapon_type, ammo_type, *objective_types;
    size_t objective_count;
    application_q3_pickup_function gate, targets;
    application_q3_pickup_grant *grants;
    size_t grant_count;
    bool present;
} application_q3_pickup_profile;

/* An unknown undeclared executable has no admitted original pickup profile. */
bool application_q3_pickup_profile_read(struct q3g_role *, qa_bytes primary,
    application_q3_pickup_profile *, qa_error *);
void application_q3_pickup_profile_free(application_q3_pickup_profile *);
#endif

#ifndef QA_APPLICATION_GUEST_PROJECTION_PRIVATE_H
#define QA_APPLICATION_GUEST_PROJECTION_PRIVATE_H

#include "guest_q3_private.h"
#include "qa/json.h"

typedef enum guest_record_kind { GUEST_ENTITY_RECORD, GUEST_CLIENT_RECORD } guest_record_kind;
typedef struct guest_field { guest_record_kind record; uint32_t offset; } guest_field;
typedef struct guest_condition { uint32_t address; int32_t value; bool equal; } guest_condition;
typedef struct guest_team_value { int32_t value; qa_team_id team; } guest_team_value;
typedef struct guest_armor_tier { int32_t value; float protection; } guest_armor_tier;
typedef struct guest_state_profile {
    uint32_t inuse, health, takedamage, flags, health_stat, team_stat, armor_stat, tier_stat;
    uint32_t invulnerable, no_knockback, notarget;
    enum { GUEST_MASS_CONSTANT, GUEST_MASS_INT32, GUEST_MASS_FLOAT32 } mass_kind;
    union { float constant; uint32_t offset; } mass;
    guest_team_value *teams;
    size_t team_count;
    float armor_protection, tier_fallback;
    guest_armor_tier *tiers;
    size_t tier_count;
    guest_condition *tier_conditions;
    size_t tier_condition_count;
} guest_state_profile;
typedef enum guest_capacity_kind {
    GUEST_CAPACITY_CONSTANT, GUEST_CAPACITY_FIELD, GUEST_CAPACITY_SOURCE
} guest_capacity_kind;
typedef struct guest_capacity_override { guest_condition condition; int32_t value; } guest_capacity_override;
typedef struct guest_capacity {
    guest_capacity_kind kind;
    union { int32_t constant; guest_field field; uint32_t address; } value;
    guest_capacity_override *overrides;
    size_t override_count;
} guest_capacity;
typedef struct guest_inventory_field {
    qa_item_id item;
    guest_field field;
    uint32_t mask, private_mask, allowed_mask;
    guest_capacity capacity;
} guest_inventory_field;
typedef struct guest_projection_actor {
    struct guest_projection_actor *next;
    struct application_guest_projection *projection;
    qa_actor_id actor;
    uint32_t slot;
    qa_inventory_lease inventory_lease;
    bool inventory_bound, inventory_prepared;
} guest_projection_actor;
typedef struct application_guest_projection {
    q3g_role *role;
    uint32_t entity_stride, client_stride, client_pointer;
    guest_inventory_field *inventory;
    size_t inventory_count;
    guest_projection_actor *actors;
    guest_state_profile state;
    bool has_inventory, has_state;
} application_guest_projection;

bool application_guest_projection_prepare(q3g_role *, qa_bytes, qa_error *);
bool application_guest_projection_close(q3g_role *, qa_error *);
bool application_guest_projection_admit(q3g_role *, qa_actor_id, qa_error *);
bool application_guest_projection_inventory_binding(q3g_role *, qa_actor_id,
    uint64_t saved_serial, qa_inventory_binding *, qa_error *);
bool application_guest_projection_detach(q3g_role *, qa_actor_id, qa_error *);
bool application_guest_projection_profile_read(q3g_role *, qa_bytes, application_guest_projection *, qa_error *);
void application_guest_projection_profile_free(application_guest_projection *);
bool application_guest_actor_admit(application_provider *, qa_actor_id, qa_error *);
bool application_guest_client_drop(application_provider *, uint32_t, const char *, qa_error *);
bool application_guest_bot_allocate(application_provider *, int32_t *, qa_error *);
bool application_guest_bot_free(application_provider *, int32_t, qa_error *);
bool application_guest_clients_drain(application_provider *, qa_error *);
bool application_guest_bots_admit(application_provider *, qa_error *);
bool application_guest_player_state(application_provider *, qa_actor_id, qa_combat_state *, qa_error *);
bool application_players_guest_attach(qa_application *, application_provider *, uint32_t,
                                     qa_actor_id, const qa_builtin_player_info *, qa_error *);
bool application_players_guest_detach(qa_application *, application_provider *, uint32_t,
                                     qa_actor_id, qa_error *);

#endif

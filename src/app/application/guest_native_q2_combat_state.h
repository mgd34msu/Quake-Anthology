#ifndef QA_APPLICATION_NATIVE_Q2_COMBAT_STATE_H
#define QA_APPLICATION_NATIVE_Q2_COMBAT_STATE_H
#include "guest_native_q2_calls.h"
#include "qa/gameplay.h"
#include "qa/game_q2.h"
struct application_native_q2;
typedef struct application_q2_armor_row {
    uint32_t index;
    qa_item_id item;
} application_q2_armor_row;
typedef struct application_q2_combat_profile {
    struct application_native_q2 *engine;
    qa_json_document *document;
    qa_json_id world;
    qa_native_target target;
    bool kex, resolved;
    uint32_t entity_bytes, client_bytes, client_pointer, health, damageable, flags,
        mass, velocity, pain, die, generation, inuse, svflags;
    uint32_t inventory, inventory_count, invincible, team, userinfo, userinfo_bytes,
        view_angles, time, item_table, item_stride, item_classname, item_info,
        normal, energy, screen, shield, cells, empty;
    uint64_t godmode, no_knockback, power_armor;
    uint32_t model_team, skin_team;
    uint32_t damage_entry, regular_entry, power_entry, process_entry;
    uint32_t monster_attacker, monster_inflictor, monster_blood, monster_knockback,
        monster_point, monster_mod, monster_invincible;
    application_q2_armor_row *regular;
    size_t regular_count;
    application_q2_call calls[6];
} application_q2_combat_profile;
typedef struct application_q2_combat_actor {
    application_q2_combat_profile *profile;
    qa_actor_id actor;
    qa_native_address address;
    uint32_t slot;
    int32_t generation;
    /* Source admission precedes native slot publication. Only the initial
     * binding read may use this flag; callers clear it before returning. */
    bool admitting;
} application_q2_combat_actor;
bool application_native_q2_inventory_index(struct application_native_q2 *, qa_item_id,
    uint32_t *, qa_error *);
bool application_native_q2_inventory_item(struct application_native_q2 *, uint32_t,
    qa_item_id *, qa_error *);
bool application_q2_combat_profile_read(struct application_native_q2 *,
    application_q2_combat_profile *, qa_error *);
bool application_q2_combat_profile_resolve(application_q2_combat_profile *, qa_error *);
void application_q2_combat_profile_free(application_q2_combat_profile *);
bool application_q2_combat_actor_init(application_q2_combat_profile *, uint32_t,
    qa_actor_id, bool admitting, application_q2_combat_actor *, qa_error *);
bool application_q2_combat_actor_valid(application_q2_combat_actor *, qa_error *);
bool application_q2_combat_state_read(void *, qa_combat_state *, qa_error *);
bool application_q2_combat_health_write(void *, float, qa_error *);
bool application_q2_combat_armor_read(application_q2_combat_actor *, qa_armor *, qa_error *);
bool application_q2_combat_armor_validate(void *, const qa_armor *, qa_error *);
bool application_q2_combat_armor_write(void *, const qa_armor *, qa_error *);
bool application_q2_combat_empty_armor(void *, double, qa_regular_armor *, bool *, qa_error *);
bool application_q2_combat_normalize_armor(void *, const qa_armor *, qa_armor *, qa_error *);
bool application_q2_combat_cause_read(const application_q2_combat_profile *,
    const qa_native_value *, uint32_t flags, qa_damage_cause *, qa_error *);
bool application_q2_combat_cause_lower(const application_q2_combat_profile *,
    const qa_damage_cause *, uint8_t mod[3], qa_native_value *, qa_error *);
bool application_q2_native_cause_read(bool rerelease,qa_q2_classic_cause_profile,
    const qa_native_value *,uint32_t flags,qa_damage_cause *,qa_error *);
bool application_q2_native_cause_lower(bool rerelease,qa_q2_classic_cause_profile,
    const qa_damage_cause *,uint8_t mod[3],qa_native_value *,qa_error *);
#endif

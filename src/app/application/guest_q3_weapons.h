#ifndef QA_APPLICATION_GUEST_Q3_WEAPONS_H
#define QA_APPLICATION_GUEST_Q3_WEAPONS_H

#include "guest_q3_weapon_profile.h"
#include "qa/qvm_save.h"

typedef struct application_q3_weapons application_q3_weapons;
typedef struct application_q3_weapon_prediction {
    int32_t source_weapon, state, time_ms;
} application_q3_weapon_prediction;
typedef enum application_q3_weapon_grant { APPLICATION_Q3_GIVE_WEAPONS, APPLICATION_Q3_GIVE_AMMO } application_q3_weapon_grant;
typedef enum application_q3_weapon_power { APPLICATION_Q3_QUAD, APPLICATION_Q3_HASTE, APPLICATION_Q3_FLIGHT } application_q3_weapon_power;
typedef struct application_q3_weapon_preparation {
    void *context;
    bool (*finish)(void *, qa_error *);
} application_q3_weapon_preparation;
typedef struct application_q3_weapon_drop {
    int32_t weapon, ammo;
    const qa_qvm_source_word *words;
    size_t word_count;
    bool present, inventory;
} application_q3_weapon_drop;
typedef struct application_q3_weapon_services {
    void *context;
    bool (*selected)(void *, qa_actor_id);
    bool (*attempted)(void *, qa_actor_id, int32_t, qa_error *);
    bool (*accepted)(void *, qa_actor_id, int32_t, qa_error *);
    bool (*completed)(void *, qa_actor_id, bool reached_attack_decision, qa_error *);
    bool (*give)(void *, qa_actor_id, application_q3_weapon_grant, qa_error *);
    bool (*give_item)(void *, qa_actor_id, qa_bytes, bool *handled, qa_error *);
    bool (*drop)(void *, qa_actor_id, application_q3_weapon_drop *, qa_error *);
    /* Borrows the actual original movement equipment frame. Its owner keeps
     * byte width, source currentness and compare-before-restore authority. */
    bool (*prepare_weapon)(void *, qa_actor_id, const qa_qvm_call *,
        application_q3_weapon_preparation *, qa_error *);
} application_q3_weapon_services;

/* Moves profile arrays into the real runtime before binding. On failure, out
 * retains a partial owner only if checked cleanup refused. No Init runs. */
bool application_q3_weapons_create(struct q3g_role *, application_q3_weapon_profile *,
    const application_q3_weapon_services *, application_q3_weapons **, qa_error *);
bool application_q3_weapons_destroy(application_q3_weapons **, qa_error *);
bool application_q3_weapons_cleanup_ready(const application_q3_weapons *);
bool application_q3_weapons_cleanup(application_q3_weapons *, qa_error *);
bool application_q3_weapons_idle(const application_q3_weapons *);
const application_q3_weapon_profile *application_q3_weapons_profile(const application_q3_weapons *);
/* Qualifies an actual catalog read after Source Init or restored RAM. The
 * constructor may retain a declared live selection before that table exists. */
bool application_q3_weapons_catalog_refresh(application_q3_weapons *,
    const application_q3_weapon_catalog_entry *, size_t, qa_error *);
size_t application_q3_weapons_descriptor_count(const application_q3_weapons *);
bool application_q3_weapons_descriptors(const application_q3_weapons *,
    qa_qvm_saved_function *, size_t, qa_error *);
void application_q3_weapons_adopt(application_q3_weapons *, const qa_qvm_binding [6]);
bool application_q3_weapons_settled(application_q3_weapons *, qa_actor_id, bool *, qa_error *);
bool application_q3_weapons_active(application_q3_weapons *, qa_actor_id, qa_item_id *, qa_error *);
bool application_q3_weapons_prediction_read(application_q3_weapons *, qa_actor_id,
    application_q3_weapon_prediction *, qa_error *);
/* Resolve host intent without inventing a Source request or acceptance. The
 * real input owner publishes this value in its original usercmd phase. */
bool application_q3_weapons_selection_for_item(application_q3_weapons *, qa_actor_id,
    qa_item_id, int32_t *, qa_error *);
bool application_q3_weapons_available(application_q3_weapons *, qa_actor_id, bool attacking, bool *, qa_error *);
bool application_q3_weapons_max_health(application_q3_weapons *, qa_actor_id, int32_t *, qa_error *);
bool application_q3_weapons_set_max_health(application_q3_weapons *, qa_actor_id, int32_t, qa_error *);
bool application_q3_weapons_powerup_until(application_q3_weapons *, qa_actor_id,
    application_q3_weapon_power, int32_t *, qa_error *);
bool application_q3_weapons_water_level(application_q3_weapons *, qa_actor_id, int32_t *, qa_error *);
bool application_q3_weapons_score(application_q3_weapons *, qa_actor_id, int32_t *, qa_error *);
bool application_q3_weapons_set_score(application_q3_weapons *, qa_actor_id, int32_t, qa_error *);
bool application_q3_weapons_canonical_team(application_q3_weapons *, qa_actor_id,
    qa_team_id source, qa_team_id *, qa_error *);
bool application_q3_weapons_team_command(application_q3_weapons *, qa_actor_id, qa_team_id,
    const application_q3_weapon_team_command **, qa_error *);
bool application_q3_weapons_damage_factor(application_q3_weapons *, qa_actor_id, float *, qa_error *);
bool application_q3_weapons_delay(application_q3_weapons *, qa_actor_id, int32_t, int32_t *, qa_error *);
bool application_q3_weapons_equipment_delay(application_q3_weapons *, qa_actor_id, qa_string_id,
    int32_t, int32_t *, qa_error *);
bool application_q3_weapons_animation(application_q3_weapons *, qa_actor_id, bool melee, qa_error *);
bool application_q3_weapons_drop_objectives(application_q3_weapons *, qa_actor_id, qa_error *);
bool application_q3_weapons_teleport(application_q3_weapons *, qa_actor_id, qa_error *);
bool application_q3_weapons_spawn_point(application_q3_weapons *, qa_actor_id, qa_vec3 *, qa_vec3 *, qa_error *);
bool application_q3_weapons_teleport_state(application_q3_weapons *, qa_actor_id,
    qa_vec3 origin, qa_vec3 velocity, qa_vec3 angles, int32_t hold_ms, qa_error *);

#endif

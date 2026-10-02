#ifndef QA_APPLICATION_GUEST_Q3_WEAPONS_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_WEAPONS_PRIVATE_H

#include "guest_q3_weapons.h"
#include "guest_q3_private.h"

typedef struct q3_weapon_actor {
    qa_actor_id actor;
    qa_q3_host_game_data data;
    uint32_t slot, entity, player;
} q3_weapon_actor;
typedef struct q3_weapon_dispatch {
    struct q3_weapon_dispatch *previous;
    q3_weapon_actor actor;
    const qa_qvm_call *call;
    bool reached;
} q3_weapon_dispatch;
typedef enum q3_weapon_evaluation_kind { Q3_WEAPON_DELAY, Q3_WEAPON_DAMAGE, Q3_WEAPON_TELEPORT, Q3_WEAPON_OBJECTIVES } q3_weapon_evaluation_kind;
typedef struct q3_weapon_evaluation {
    struct q3_weapon_evaluation *previous;
    q3_weapon_actor actor;
    q3_weapon_evaluation_kind kind;
    int32_t input;
    float damage;
    bool entered, produced;
} q3_weapon_evaluation;
typedef struct q3_weapon_projection {
    struct q3_weapon_projection *next;
    qa_qvm_word_projection *lease;
    q3_weapon_actor actor;
    bool restore, actor_record;
} q3_weapon_projection;
struct application_q3_weapons {
    q3g_role *role;
    application_q3_weapon_profile profile;
    application_q3_weapon_services services;
    qa_qvm_binding bindings[6];
    size_t binding_count;
    q3_weapon_dispatch *dispatch;
    q3_weapon_evaluation *evaluation;
    q3_weapon_projection *projections;
    size_t calls;
};
bool q3_weapons_actor(application_q3_weapons *, qa_actor_id, q3_weapon_actor *, qa_error *);
bool q3_weapons_current(application_q3_weapons *, const q3_weapon_actor *);
bool q3_weapons_read(application_q3_weapons *, uint32_t, int32_t *, qa_error *);
bool q3_weapons_write(application_q3_weapons *, uint32_t, int32_t, qa_error *);

#endif

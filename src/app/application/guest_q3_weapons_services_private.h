#ifndef QA_APPLICATION_GUEST_Q3_WEAPONS_SERVICES_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_WEAPONS_SERVICES_PRIVATE_H
#include "guest_q3_weapons_services.h"
#include "guest_q3_weapons_private.h"
#include "qa/pool.h"

typedef struct q3_weapon_request { qa_actor_id actor; int32_t weapon; } q3_weapon_request;
typedef struct q3_weapon_actor_state q3_weapon_actor_state;
typedef struct q3_weapon_match {
    struct q3_weapon_match *next;
    struct q3_weapon_match *actor_next;
    application_q3_weapons_services *services;
    q3_weapon_actor_state *actor_state;
    size_t storage_slot;
    qa_actor_id actor;
    qa_actor_owner source_owner;
    qa_mode_id mode;
} q3_weapon_match;
struct q3_weapon_actor_state {
    qa_actor_id actor;
    size_t storage_slot;
    q3_weapon_actor_state *request_previous, *request_next;
    q3_weapon_match *matches;
    int32_t weapon;
    bool requested;
};
typedef union q3_weapon_storage {
    q3_weapon_actor_state actor;
    q3_weapon_match match;
} q3_weapon_storage;
struct application_q3_weapons_services {
    q3g_role *role;
    qa_qvm_image *image;
    qa_qvm *vm;
    qa_qvm_abi abi;
    q3_weapon_actor_state *first_request, *last_request;
    size_t request_count, calls;
    qa_arena state_storage;
    qa_pool state_pool;
    qa_actor_index actor_states;
    qa_arena inventory_storage;
    qa_pool inventory_pages;
    qa_qvm_source_word drop_words[2];
    q3_weapon_match *matches;
    bool closing_match;
};
bool q3_weapon_services_source(application_q3_weapons_services *, qa_actor_id,
    q3_weapon_actor *, qa_error *);
bool q3_weapon_request_store(application_q3_weapons_services *, qa_actor_id, int32_t, qa_error *);
void q3_weapon_requests_clear(application_q3_weapons_services *);
#endif

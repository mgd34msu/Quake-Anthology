#ifndef QA_APPLICATION_GUEST_Q3_WEAPONS_SERVICES_PRIVATE_H
#define QA_APPLICATION_GUEST_Q3_WEAPONS_SERVICES_PRIVATE_H
#include "guest_q3_weapons_services.h"
#include "guest_q3_weapons_private.h"

typedef struct q3_weapon_request { qa_actor_id actor; int32_t weapon; } q3_weapon_request;
typedef struct q3_weapon_match {
    struct q3_weapon_match *next;
    application_q3_weapons_services *services;
    qa_actor_id actor;
    qa_actor_owner source_owner;
    qa_mode_id mode;
} q3_weapon_match;
struct application_q3_weapons_services {
    q3g_role *role;
    qa_qvm_image *image;
    qa_qvm *vm;
    qa_qvm_abi abi;
    q3_weapon_request *requests;
    size_t request_count, calls;
    qa_qvm_source_word drop_words[2];
    q3_weapon_match *matches;
    bool closing_match;
};
bool q3_weapon_services_source(application_q3_weapons_services *, qa_actor_id,
    q3_weapon_actor *, qa_error *);
#endif

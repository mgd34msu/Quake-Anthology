#ifndef QA_APPLICATION_GUEST_INVENTORY_PROFILE_H
#define QA_APPLICATION_GUEST_INVENTORY_PROFILE_H

#include "qa/qvm.h"
#include "qa/json.h"

enum { GUEST_INVENTORY_ARGUMENTS = 62 };
typedef enum guest_inventory_word_kind {
    GUEST_INVENTORY_CONSTANT, GUEST_INVENTORY_WEAPON, GUEST_INVENTORY_CLIENT,
    GUEST_INVENTORY_ENTITY, GUEST_INVENTORY_CLIENT_NUMBER, GUEST_INVENTORY_MAXIMUM_GRANT
} guest_inventory_word_kind;
typedef struct guest_inventory_word {
    guest_inventory_word_kind kind;
    int32_t value;
} guest_inventory_word;
typedef struct guest_public_inventory_profile {
    const qa_qvm_image *image; /* Borrowed from the actual retained GAME role. */
    qa_qvm *vm; /* Exact private executor, reconstructed by the real role owner. */
    qa_qvm_abi abi;
    uint32_t weapons_offset, ammo_offset;
    enum { GUEST_PUBLIC_CONSTANT, GUEST_PUBLIC_GLOBAL,
           GUEST_PUBLIC_REGION, GUEST_PUBLIC_COUNTER, GUEST_PUBLIC_THREEWAVE } capacity_kind;
    int32_t constant;
    uint32_t global, function;
    uint32_t *functions, *locals;
    size_t function_count;
    int32_t fallback, special_mode, first_weapon;
    uint32_t game_type, lithium;
    int32_t *weapon_limits;
    size_t weapon_limit_count;
    guest_inventory_word arguments[GUEST_INVENTORY_ARGUMENTS];
    size_t argument_count;
    guest_inventory_word *inputs;
    qa_qvm_region_evaluation region;
    qa_qvm_evaluation_stack stack;
    bool has_stack;
} guest_public_inventory_profile;
typedef struct guest_inventory_source {
    uint32_t client, entity, client_number, weapon;
} guest_inventory_source;

bool application_guest_public_inventory_profile_read(const qa_qvm_image *, qa_qvm *, qa_qvm_abi,
    const qa_json_document *, qa_json_id, guest_public_inventory_profile *, qa_error *);
bool application_guest_public_inventory_profile_default(const qa_qvm_image *, qa_qvm *, qa_qvm_abi,
    guest_public_inventory_profile *, bool *found, qa_error *);
void application_guest_public_inventory_profile_free(guest_public_inventory_profile *);
bool application_guest_public_inventory_capacity(const guest_public_inventory_profile *,
    qa_qvm *, const guest_inventory_source *, int32_t *, qa_error *);

#endif

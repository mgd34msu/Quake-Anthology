#ifndef QA_APPLICATION_GUEST_Q3_WEAPON_MODELS_PROFILE_H
#define QA_APPLICATION_GUEST_Q3_WEAPON_MODELS_PROFILE_H

#include "qa/qvm.h"

typedef enum application_q3_weapon_model_field {
    APPLICATION_Q3_WEAPON_GUN, APPLICATION_Q3_WEAPON_HANDS,
    APPLICATION_Q3_WEAPON_BARREL, APPLICATION_Q3_WEAPON_FLASH,
    APPLICATION_Q3_WEAPON_INVISIBILITY, APPLICATION_Q3_WEAPON_BATTLE,
    APPLICATION_Q3_WEAPON_QUAD, APPLICATION_Q3_WEAPON_MODEL_FIELDS
} application_q3_weapon_model_field;
typedef struct application_q3_weapon_models_profile {
    char *artifact_path;
    char *game_artifact_path;
    qa_sha256_digest artifact;
    qa_sha256_digest game_artifact;
    qa_qvm_abi abi, game_abi;
    uint32_t registration, weapon_argument;
    uint32_t base, count, stride, weapon_offset, registered_offset;
    int32_t index_base;
    uint32_t offsets[APPLICATION_Q3_WEAPON_MODEL_FIELDS];
    bool fields[APPLICATION_Q3_WEAPON_MODEL_FIELDS], indexed, present;
} application_q3_weapon_models_profile;

/* A declaration names actual original CG registration and table storage.
 * Absence supplies no stock model map. The artifact owner retains its opening. */
bool application_q3_weapon_models_profile_read(const qa_qvm_image *, qa_qvm_role,
    qa_qvm_abi, const char *artifact_path, const qa_bytes *declaration,
    application_q3_weapon_models_profile *, qa_error *);
bool application_q3_weapon_models_profile_qualify(const qa_qvm_image *, qa_qvm_abi,
    const char *artifact_path, const application_q3_weapon_models_profile *, qa_error *);
void application_q3_weapon_models_profile_free(application_q3_weapon_models_profile *);
bool application_q3_weapon_models_profile_namespace(const application_q3_weapon_models_profile *,
    const qa_qvm_image *actual_game_image, qa_qvm_abi, const char *actual_game_path, qa_error *);
bool application_q3_weapon_models_profile_checkpoint(const qa_qvm_image *, qa_qvm_abi,
    const char *artifact_path, const application_q3_weapon_models_profile *, qa_buffer *, qa_error *);
bool application_q3_weapon_models_profile_restore(const qa_qvm_image *, qa_qvm_abi,
    const char *artifact_path, qa_bytes, application_q3_weapon_models_profile *, qa_error *);

#endif

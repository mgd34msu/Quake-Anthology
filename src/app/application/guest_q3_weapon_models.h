#ifndef QA_APPLICATION_GUEST_Q3_WEAPON_MODELS_H
#define QA_APPLICATION_GUEST_Q3_WEAPON_MODELS_H

#include "guest_q3_weapon_models_profile.h"
#include "qa/application_q3_weapon_models.h"
#include "qa/qvm_save.h"
#include "qa/session.h"

typedef struct application_q3_weapon_models application_q3_weapon_models;
typedef struct application_q3_weapon_models_module {
    qa_session *session;
    qa_qvm *vm;
    const qa_qvm_image *image;
    qa_qvm *game_vm;
    const qa_qvm_image *game_image;
    const char *game_artifact_path;
    const application_q3_weapon_models_profile *profile;
    qa_q3_presentation_assets *assets;
    void *context;
    bool (*current)(void *, const qa_qvm *actual_cgame, const qa_qvm *actual_game,
        const qa_q3_presentation_assets *, qa_error *);
    bool importing;
} application_q3_weapon_models_module;

/* Assigned even after a partial binding failure so the real parent can retry
 * checked teardown. The immutable module and registry outlive this child. */
bool application_q3_weapon_models_create(const application_q3_weapon_models_module *,
    application_q3_weapon_models **, qa_error *);
bool application_q3_weapon_models_destroy(application_q3_weapon_models *, qa_error *);
bool application_q3_weapon_models_idle(const application_q3_weapon_models *);
size_t application_q3_weapon_models_descriptor_count(const application_q3_weapon_models *);
bool application_q3_weapon_models_descriptors(const application_q3_weapon_models *,
    qa_qvm_saved_function *, size_t, qa_error *);
void application_q3_weapon_models_adopt(application_q3_weapon_models *, const qa_qvm_binding *);
bool application_q3_weapon_models_read(application_q3_weapon_models *, int32_t source_weapon,
    qa_application_q3_weapon_models *, bool *present, qa_error *);
bool application_q3_weapon_models_checkpoint(const application_q3_weapon_models *, qa_buffer *, qa_error *);
/* Import installs only the actual saved receipts before outer RAM restoration.
 * The parent calls qualify after restoring both RAM and the model registry. */
bool application_q3_weapon_models_restore(application_q3_weapon_models *, qa_bytes,
    qa_qvm_binding *saved_binding, qa_error *);
bool application_q3_weapon_models_qualify(application_q3_weapon_models *, qa_error *);

#endif

#ifndef APPLICATION_SOURCE_TRAJECTORY_H
#define APPLICATION_SOURCE_TRAJECTORY_H

#include "qa/application.h"
#include "qa/builtin.h"
#include "qa/launch.h"
#include "qa/qc.h"
#include "qa/qvm.h"
#include "qa/native.h"

typedef struct application_trajectory_attachment {
    qa_actor_id projectile;
    uint64_t handle;
    bool retired;
} application_trajectory_attachment;

/* Private donor state uses the shared world for queries. Only the selected
 * launcher owns the canonical projectile's physics, impact and presentation. */
typedef struct application_trajectory_source {
    void *context;
    bool (*launch)(void *, const qa_builtin_weapon_launch *,
        application_trajectory_attachment *, qa_builtin_trajectory_update *, bool *, qa_error *);
    bool (*step)(void *, application_trajectory_attachment *, const qa_body_state *,
        uint64_t, qa_builtin_trajectory_update *, bool *, qa_error *);
    bool (*release)(void *, application_trajectory_attachment *, qa_error *);
    bool (*actor_released)(void *, qa_actor_record, qa_error *);
    bool (*capture)(void *, qa_buffer *, qa_error *);
    bool (*restore)(void *, qa_bytes, qa_error *);
    bool (*destroy)(void *, qa_error *);
} application_trajectory_source;

bool application_trajectory_entities_prepare(qa_application *, const char *const *,
    size_t, qa_buffer *, qa_error *);
bool application_trajectory_qc_check(qa_qc_program *, const qa_catalog_weapon_behavior *, qa_error *);
bool application_trajectory_qvm_check(qa_qvm_image *, const qa_catalog_weapon_behavior *, qa_error *);
bool application_trajectory_native_check(qa_native_module *, const qa_catalog_weapon_behavior *, qa_error *);

bool application_trajectory_qc_create(qa_application *, const qa_launch_instance *,
    qa_qc_program *, const qa_catalog_weapon_behavior *, application_trajectory_source *, qa_error *);
bool application_trajectory_qvm_create(qa_application *, const qa_launch_instance *,
    qa_qvm_image *, const qa_catalog_weapon_behavior *, application_trajectory_source *, qa_error *);

bool application_trajectory_native_create(qa_application *, const qa_launch_instance *,
    qa_native_module *, const qa_catalog_weapon_behavior *, application_trajectory_source *, qa_error *);

#endif

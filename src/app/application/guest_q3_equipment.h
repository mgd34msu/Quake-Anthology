#ifndef QA_APPLICATION_GUEST_Q3_EQUIPMENT_H
#define QA_APPLICATION_GUEST_Q3_EQUIPMENT_H

#include "qa/application_q3_equipment_source.h"
#include "qa/qvm_save.h"
#include "qa/q3_host.h"
#include "guest_q3_equipment_profile.h"

typedef struct q3g_role q3g_role;
typedef struct application_q3_equipment application_q3_equipment;
typedef qa_application_q3_equipment_draw application_q3_equipment_draw;
typedef qa_application_q3_equipment_services application_q3_equipment_services;

typedef struct application_q3_equipment_module {
    qa_session *session;
    qa_qvm *vm;
    const qa_qvm_image *image;
    const application_q3_equipment_profile *profile;
    qa_actor_owner receiver;
    uint32_t seat;
    qa_q3_host_client_services client;
} application_q3_equipment_module;

/* Assigns the candidate owner before binding, so partial construction remains
 * owned on failure. The role destroys it before its real executor/artifact. */
bool application_q3_equipment_create(q3g_role *,
    const application_q3_equipment_services *, application_q3_equipment **, qa_error *);
/* The actual CGAME module owner retains the image, qualified profile and
 * client callback contexts until equipment destruction succeeds. */
bool application_q3_equipment_create_module(const application_q3_equipment_module *,
    const application_q3_equipment_services *, application_q3_equipment **, qa_error *);
bool application_q3_equipment_destroy(application_q3_equipment *, qa_error *);
bool application_q3_equipment_idle(const application_q3_equipment *);
bool application_q3_equipment_executor(const application_q3_equipment *,
    const qa_session *, const qa_qvm *);
bool application_q3_equipment_draw_begin(application_q3_equipment *, qa_error *);
void application_q3_equipment_draw_end(application_q3_equipment *);
bool application_q3_equipment_hud(const application_q3_equipment *);
bool application_q3_equipment_view(const application_q3_equipment *);
bool application_q3_equipment_source_entity(void *, const qa_qvm_call *, int32_t,
    const qa_q3_ref_entity *, bool *suppress, qa_error *);
bool application_q3_equipment_source_entity_cancel(application_q3_equipment *,
    const qa_qvm_call *, qa_error *);
bool application_q3_equipment_source_poly(void *, const qa_qvm_call *, size_t vertices, qa_error *);
bool application_q3_equipment_source_light(void *, const qa_qvm_call *, qa_error *);
size_t application_q3_equipment_descriptor_count(const application_q3_equipment *);
bool application_q3_equipment_descriptors(const application_q3_equipment *,
    qa_qvm_saved_function *, size_t, qa_error *);
/* Called only after whole-executor qualification and no-fail ID reconstruction. */
void application_q3_equipment_adopt(application_q3_equipment *, const qa_qvm_binding *);

typedef struct application_q3_equipment_saved {
    application_q3_equipment_draw draw;
    bool hud_requested, view_requested;
} application_q3_equipment_saved;
bool application_q3_equipment_state_read(const application_q3_equipment *,
    application_q3_equipment_saved *, qa_error *);
bool application_q3_equipment_state_qualify(const application_q3_equipment *,
    const application_q3_equipment_saved *, qa_error *);
void application_q3_equipment_state_adopt(application_q3_equipment *,
    const application_q3_equipment_saved *);

#endif

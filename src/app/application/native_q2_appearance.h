#ifndef QA_APPLICATION_NATIVE_Q2_APPEARANCE_H
#define QA_APPLICATION_NATIVE_Q2_APPEARANCE_H

#include "qa/application_native_q2_presentation.h"
#include "qa/native_host_q2_wire.h"

typedef struct application_native_q2_appearance {
    qa_application_native_q2_presentation source;
    qa_native_host_q2_entity entity;
    uint64_t config_revision;
    uint32_t source_slot;
    char *models[4], *skin_path;
    uint32_t skin;
} application_native_q2_appearance;

/* Owns resolved paths; the completed physical Source and its configstring
 * revision are borrowed. No asset is opened or client state reconstructed. */
bool application_native_q2_appearance_read(qa_application *,
    const qa_application_native_q2_presentation *, uint32_t,
    application_native_q2_appearance *, qa_error *);
bool application_native_q2_appearance_current(qa_application *,
    const application_native_q2_appearance *);
void application_native_q2_appearance_dispose(application_native_q2_appearance *);

#endif

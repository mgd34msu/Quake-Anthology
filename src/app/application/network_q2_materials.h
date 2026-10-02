#ifndef APPLICATION_NETWORK_Q2_MATERIALS_H
#define APPLICATION_NETWORK_Q2_MATERIALS_H
#include "network_q2_private.h"
bool application_network_q2_materials_derive(qa_application_network_q2 *,
    application_q2_held_resource *, qa_error *);
bool application_network_q2_materials_validate(const qa_application_network_q2 *,
    const application_q2_held_resource *, qa_error *);
bool application_network_q2_materials_image_receipt_encode(const qa_application_network_q2 *,
    const application_q2_held_resource *, const char *, qa_buffer *, qa_error *);
bool application_network_q2_materials_model_scope_encode(const qa_application_network_q2 *,
    const application_q2_held_resource *, qa_buffer *, qa_error *);
bool application_network_q2_materials_model_scope_validate(const qa_application_network_q2 *,
    const application_q2_held_resource *, qa_error *);
#endif

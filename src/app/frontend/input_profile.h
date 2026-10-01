#ifndef QA_FRONTEND_INPUT_PROFILE_H
#define QA_FRONTEND_INPUT_PROFILE_H
#include "internal.h"
#include "qa/persistence_content.h"
#include "qa/settings.h"
bool frontend_input_profile_default_options(qa_frontend *,qa_error *);
bool frontend_input_profile_bind(qa_frontend *,qa_error *);
bool frontend_input_profile_bind_store(qa_frontend *,qa_catalog *,qa_product_id,qa_settings_store,qa_error *);
const qa_vfs *frontend_input_profile_files(const qa_frontend *);
qa_product_id frontend_input_profile_product(const qa_frontend *);
void frontend_input_profile_destroy(qa_frontend *);
bool frontend_input_profile_checkpoint(const qa_frontend *,const qa_application_content_graph *,qa_buffer *,qa_error *);
bool frontend_input_profile_restore(qa_frontend *,qa_application_content_graph *,qa_bytes,qa_error *);
#endif

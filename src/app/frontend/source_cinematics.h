#ifndef QA_FRONTEND_SOURCE_CINEMATICS_H
#define QA_FRONTEND_SOURCE_CINEMATICS_H
#include "internal.h"
#include "qa/q3_cinematic_handles.h"
bool frontend_source_cinematics_ensure(qa_frontend *,qa_scene_resources *,qa_error *);
bool frontend_source_cinematics_read(const qa_frontend *,qa_q3_cinematic_handles_options *,qa_error *);
bool frontend_source_cinematics_destroy(qa_frontend *,qa_error *);
bool frontend_source_cinematics_rebind_ready(const qa_frontend *,const qa_frontend *,qa_error *);
void frontend_source_cinematics_rebind(qa_frontend *);
#endif

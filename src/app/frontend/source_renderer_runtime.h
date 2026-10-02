#ifndef QA_FRONTEND_SOURCE_RENDERER_RUNTIME_H
#define QA_FRONTEND_SOURCE_RENDERER_RUNTIME_H
#include "internal.h"
bool frontend_source_renderer_runtime_bind(qa_frontend *,qa_error *);
bool frontend_source_renderer_policy(qa_frontend *,qa_error *);
bool frontend_source_renderer_end_registration(qa_frontend *,qa_error *);
bool frontend_source_renderer_image_grid(qa_frontend *,int32_t,qa_error *);
#endif

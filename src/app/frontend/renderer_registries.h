#ifndef QA_FRONTEND_RENDERER_REGISTRIES_H
#define QA_FRONTEND_RENDERER_REGISTRIES_H
#include "internal.h"
#include "qa/q3_assets_custody.h"
#include "qa/q3_assets_save.h"
typedef struct frontend_renderer_registries frontend_renderer_registries;
bool frontend_renderer_registries_refresh(qa_frontend *,qa_error *);
bool frontend_renderer_registries_include(qa_frontend *,qa_q3_presentation_assets *,qa_error *);
size_t frontend_renderer_registries_count(const qa_frontend *);
bool frontend_renderer_registries_at(const qa_frontend *,size_t,qa_q3_presentation_assets **,qa_error *);
bool frontend_renderer_registries_idle(const frontend_renderer_registries *);
bool frontend_renderer_registries_destroy(frontend_renderer_registries **,qa_error *);
#endif

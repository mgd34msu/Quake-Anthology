#ifndef QA_FRONTEND_ORIGINAL_FRONTEND_H
#define QA_FRONTEND_ORIGINAL_FRONTEND_H
#include "qa/frontend_save.h"
struct frontend_persistence_native;
bool frontend_graphics_create_detached(qa_frontend *, qa_frontend *,
    struct frontend_persistence_native *, qa_error *);
#endif

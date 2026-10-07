#ifndef QA_FRONTEND_VIEW_BINDINGS_H
#define QA_FRONTEND_VIEW_BINDINGS_H
#include "qa/frontend.h"
bool frontend_view_bindings_create(qa_frontend *,qa_error *);
bool frontend_view_bindings_apply_restored(qa_frontend *,qa_error *);
void frontend_view_bindings_restore_published(qa_frontend *);
bool frontend_view_bindings_finish_restore(qa_frontend *,qa_error *);
#endif

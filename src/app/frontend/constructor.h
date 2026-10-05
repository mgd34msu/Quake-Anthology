#ifndef QA_FRONTEND_CONSTRUCTOR_H
#define QA_FRONTEND_CONSTRUCTOR_H
#include "qa/frontend.h"
struct frontend_persistence_native;
bool frontend_outputs_create_detached(qa_frontend *,qa_frontend *,struct frontend_persistence_native *,qa_error *);
bool frontend_constructor_pending(const qa_frontend *);
bool frontend_constructor_advance(qa_frontend *,uint64_t elapsed_ns,bool *complete,qa_error *);
bool frontend_startup_advance(qa_frontend *,bool *complete,qa_error *);
#endif

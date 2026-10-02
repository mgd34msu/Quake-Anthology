#ifndef QA_FRONTEND_CONSTRUCTOR_H
#define QA_FRONTEND_CONSTRUCTOR_H
#include "qa/frontend.h"
bool frontend_constructor_pending(const qa_frontend *);
bool frontend_constructor_advance(qa_frontend *,uint64_t elapsed_ns,bool *complete,qa_error *);
#endif

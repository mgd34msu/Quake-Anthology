#ifndef QA_FRONTEND_CONSTRUCTOR_H
#define QA_FRONTEND_CONSTRUCTOR_H
#include "qa/frontend.h"
struct frontend_persistence_native;
bool frontend_outputs_create_detached(qa_frontend *,qa_frontend *,struct frontend_persistence_native *,qa_error *);
bool frontend_constructor_pending(const qa_frontend *);
bool frontend_constructor_advance(qa_frontend *,bool *complete,qa_error *);
/* Normal frontend construction through ENGINE bootstrap, without launching
 * the selected initial game. Original saves install their state afterward. */
bool frontend_create_for_import(const qa_frontend_options *,qa_native_runtime *,qa_frontend **,qa_error *);
bool frontend_startup_advance(qa_frontend *,bool *complete,qa_error *);
#endif

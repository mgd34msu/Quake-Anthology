#ifndef QA_FRONTEND_SHARED_UI_H
#define QA_FRONTEND_SHARED_UI_H
#include "internal.h"
#include "qa/console_cvars_prepare.h"
typedef struct frontend_shared_ui frontend_shared_ui;
/* Prepare after real source-release callbacks complete, before sealing the
 * actual ENGINE scalar ticket. Every font/catalog/caption child remains owned
 * until publication plus finish, or checked abort. A failure may retain *out. */
bool frontend_shared_ui_prepare(qa_frontend *,const qa_cvars_edit *,
    frontend_shared_ui **,qa_error *);
bool frontend_shared_ui_ready(const frontend_shared_ui *,qa_error *);
bool frontend_shared_ui_ready_is(const frontend_shared_ui *);
void frontend_shared_ui_publish(frontend_shared_ui *);
/* After final successful ready, publish and consume every UI/caption lease
 * before source retirement or Init. The held boundary must not change between
 * ready and this nofail, allocation-free handoff. Consumes *owner. */
void frontend_shared_ui_consume(frontend_shared_ui **);
bool frontend_shared_ui_finish(frontend_shared_ui **,qa_error *);
bool frontend_shared_ui_abort(frontend_shared_ui **,qa_error *);
#endif

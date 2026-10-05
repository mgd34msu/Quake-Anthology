#ifndef QA_FRONTEND_STARTUP_MENUS_H
#define QA_FRONTEND_STARTUP_MENUS_H
#include "internal.h"
#include "qa/application_startup_prepare.h"
#include "seats_resize.h"
bool frontend_startup_menus_create(frontend_seat *, qa_error *);
bool frontend_startup_menus_destroy(frontend_seat *, qa_error *);
bool frontend_startup_launch_drain(qa_frontend *, qa_error *);
void frontend_startup_launch_discard(qa_frontend *);
bool frontend_startup_end_stage(frontend_seat *, qa_error *);
frontend_seats_resize_state *frontend_startup_launch_resize_state(qa_frontend *, unsigned next);
bool frontend_startup_launch_settings(qa_frontend *, const qa_launch_snapshot *,
    const qa_application_startup_source *, qa_cvars *client, bool first_source, qa_error *);
bool frontend_startup_menus_pump(qa_frontend *, qa_error *);
bool frontend_startup_menus_bind(qa_frontend *, qa_error *);
#endif

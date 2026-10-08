#ifndef QA_FRONTEND_STARTUP_MENUS_H
#define QA_FRONTEND_STARTUP_MENUS_H
#include "internal.h"
#include "qa/application_startup_prepare.h"
#include "seats_resize.h"
#include "host_menu.h"
bool frontend_startup_menus_create(frontend_seat *, qa_error *);
bool frontend_startup_menus_destroy(frontend_seat *, qa_error *);
bool frontend_startup_launch_drain(qa_frontend *, qa_error *);
void frontend_startup_launch_discard(qa_frontend *);
bool frontend_startup_end_stage(frontend_seat *, qa_error *);
bool frontend_startup_local_players_read(frontend_seat *,bool *join,bool *drop,qa_error *);
bool frontend_startup_local_join_stage(frontend_seat *,qa_error *);
bool frontend_startup_local_drop_stage(frontend_seat *,qa_error *);
/* Dense physical slots project only real local human choices. */
bool frontend_local_seat_read(const qa_launch_choices *,unsigned physical,uint32_t *logical);
unsigned frontend_local_seat_count(const qa_launch_choices *);
bool frontend_local_seat_ordinal_read(const qa_launch_choices *,uint32_t logical,unsigned *physical);
/* Constructor-only observation of the queued genuine local-player draft. */
bool frontend_startup_launch_seat_read(const qa_frontend *,unsigned physical,uint32_t *logical);
bool frontend_startup_launch_complete(qa_frontend *,qa_error *);
frontend_seats_resize_state *frontend_startup_launch_resize_state(qa_frontend *, unsigned next);
bool frontend_startup_launch_settings(qa_frontend *, const qa_launch_snapshot *,
    const qa_application_startup_source *, qa_cvars *client, bool first_source, qa_error *);
bool frontend_startup_menus_pump(qa_frontend *, qa_error *);
bool frontend_startup_menus_bind(qa_frontend *, qa_error *);
bool frontend_startup_lobby_launch_stage(frontend_seat *, const qa_lobby_view *,
    const frontend_host_settings *, bool join, qa_error *);
bool frontend_startup_lobby_end_stage(qa_frontend *, uint32_t physical, qa_error *);
#endif

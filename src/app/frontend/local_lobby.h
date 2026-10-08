#ifndef QA_FRONTEND_LOCAL_LOBBY_H
#define QA_FRONTEND_LOCAL_LOBBY_H
#include "internal.h"
#include "qa/local_lobby.h"
#include "host_menu.h"

typedef struct frontend_local_lobby frontend_local_lobby;

/* The frontend owns the service and session across world and seat changes. */
bool frontend_local_lobby_create(qa_frontend *, qa_lobbies *, qa_local_account,
    frontend_local_lobby **, qa_error *);
bool frontend_local_lobby_menu_create(frontend_local_lobby *, frontend_seat *,
    qa_ui_id, qa_error *);
bool frontend_local_lobby_menu_destroy(frontend_local_lobby *, uint32_t physical, qa_error *);
bool frontend_local_lobby_pump(frontend_local_lobby *, qa_error *);
void frontend_local_lobby_report(frontend_local_lobby *, const char *message);
bool frontend_local_lobby_destroy(frontend_local_lobby **, qa_error *);
void frontend_local_lobby_hosting(frontend_local_lobby *, uint32_t physical,
    const frontend_host_settings *);
void frontend_local_lobby_launch_started(frontend_local_lobby *);
void frontend_local_lobby_launch_failed(frontend_local_lobby *, const qa_error *);
bool frontend_local_lobby_owns_match(const frontend_local_lobby *);
void frontend_local_lobby_match_replaced(frontend_local_lobby *);
void frontend_local_lobby_end_complete(frontend_local_lobby *);

/* Captures the selected draft and applied hosting choices after the UI callback.
 * Creating membership does not start a world or substitute an endpoint. */
bool frontend_startup_lobby_host_stage(frontend_seat *, qa_lobby_session *,
    const char *name, uint32_t capacity, uint32_t seats, qa_error *);
#endif

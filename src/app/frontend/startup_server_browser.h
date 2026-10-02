#ifndef QA_FRONTEND_STARTUP_SERVER_BROWSER_H
#define QA_FRONTEND_STARTUP_SERVER_BROWSER_H
#include "internal.h"
#include "network_menu.h"

typedef struct frontend_startup_server_browser frontend_startup_server_browser;
typedef struct frontend_startup_server_browser_menus {
    qa_ui_id browser, options, details;
} frontend_startup_server_browser_menus;

/* The physical seat and its UI/input/console owners outlive this child.
 * Failed registration retains a non-NULL child until checked destruction. */
bool frontend_startup_server_browser_create(frontend_seat *,
    const frontend_startup_server_browser_menus *, frontend_startup_server_browser **, qa_error *);
bool frontend_startup_server_browser_idle(const frontend_startup_server_browser *);
bool frontend_startup_server_browser_destroy(frontend_startup_server_browser **, qa_error *);
bool frontend_startup_server_browser_open(frontend_startup_server_browser *, qa_error *);
/* Private drafts do not serialize or replay the Network's retained responses,
 * favorite/direct records, pending queries, or connection attempts. */
bool frontend_startup_server_browser_checkpoint(const frontend_startup_server_browser *, qa_buffer *, qa_error *);
bool frontend_startup_server_browser_restore(frontend_startup_server_browser *, qa_bytes, qa_error *);
#endif

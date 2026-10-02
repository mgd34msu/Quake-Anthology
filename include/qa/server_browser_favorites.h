#ifndef QA_SERVER_BROWSER_FAVORITES_H
#define QA_SERVER_BROWSER_FAVORITES_H
#include "qa/server_browser.h"

/* Restore only favorite membership from a saved identity list. Unrelated
 * direct discovery records and their received status remain owned in place. */
bool qa_server_browser_restore_favorites(qa_server_browser *, qa_bytes, qa_error *);
#endif

#ifndef QA_SERVER_BROWSER_DETAILS_H
#define QA_SERVER_BROWSER_DETAILS_H
#include "qa/server_browser.h"

typedef struct qa_server_browser_player { int32_t score, ping; qa_buffer name; } qa_server_browser_player;
typedef struct qa_server_browser_rule { qa_buffer name, value; } qa_server_browser_rule;
typedef struct qa_server_browser_details {
    qa_server_entry entry;
    qa_buffer response;
    qa_server_browser_player *players;
    size_t player_count;
    qa_server_browser_rule *rules;
    size_t rule_count;
} qa_server_browser_details;
/* Owns copies of the actual retained response and its decoded rows. Counted
 * rule text preserves full retail values, including embedded NUL bytes. */
bool qa_server_browser_details_read(const qa_server_browser *, const qa_net_address *,
    qa_net_protocol_id, qa_server_browser_details *, qa_error *);
bool qa_server_browser_details_current(const qa_server_browser *, const qa_server_browser_details *);
void qa_server_browser_details_free(qa_server_browser_details *);
#endif

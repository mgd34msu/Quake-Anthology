#ifndef QA_FRONTEND_NETWORK_MENU_H
#define QA_FRONTEND_NETWORK_MENU_H
#include "qa/frontend.h"
#include "qa/server_browser.h"
#include "qa/server_browser_details.h"
#include "qa/server_admin.h"
#include "qa/downloads.h"
#include "qa/input.h"
#include "qa/console_seat.h"

typedef struct frontend_network_menu_view {
    const void *network;
    qa_application *application;
    const qa_server_browser *browser;
    const qa_server_admin *admin;
    const qa_downloads *downloads;
    const qa_fs_root *preferences;
    const qa_input_seat *input;
    const qa_seat_console *console;
    uint64_t configuration_generation;
    uint32_t physical_seat, authored_seat;
    bool has_authored_seat, restore_readonly;
} frontend_network_menu_view;

/* The menu borrows the actual published Network and physical input owner.
 * Rows are copied from retained responses; the menu owns filtering/sorting. */
bool frontend_network_menu_read(const qa_frontend *, uint32_t physical_seat,
    frontend_network_menu_view *, qa_error *);
bool frontend_network_menu_current(const qa_frontend *, const frontend_network_menu_view *);
bool frontend_network_menu_rows(const qa_frontend *, const frontend_network_menu_view *,
    qa_net_protocol_id, qa_server_entry *, size_t capacity, size_t *count, qa_error *);
bool frontend_network_menu_details_read(const qa_frontend *, const frontend_network_menu_view *,
    const qa_server_entry *, qa_server_browser_details *, qa_error *);
bool frontend_network_menu_details_current(const qa_frontend *, const frontend_network_menu_view *,
    const qa_server_browser_details *);
typedef struct frontend_network_menu_direct { qa_net_address address; char remote[256]; } frontend_network_menu_direct;
typedef struct frontend_network_menu_preferences {
    char master[2049];
    frontend_network_menu_direct direct[16];
    uint32_t direct_count;
} frontend_network_menu_preferences;
bool frontend_network_menu_preferences_read(const qa_frontend *, const frontend_network_menu_view *,
    qa_net_protocol_id, frontend_network_menu_preferences *, qa_error *);
bool frontend_network_menu_direct_read(const qa_frontend *, const frontend_network_menu_view *,
    const qa_server_entry *, char remote[256], bool *present, qa_error *);
bool frontend_network_menu_query(qa_frontend *, const frontend_network_menu_view *,
    qa_net_protocol_id, const char *remote, qa_error *);
bool frontend_network_menu_scan(qa_frontend *, const frontend_network_menu_view *,
    qa_net_protocol_id, qa_error *);
bool frontend_network_menu_master(qa_frontend *, const frontend_network_menu_view *,
    qa_net_protocol_id, const char *remote, qa_error *);
bool frontend_network_menu_favorite(qa_frontend *, const frontend_network_menu_view *,
    qa_net_protocol_id, const char *remote, bool *added, qa_error *);
typedef struct frontend_network_menu_connection {
    qa_net_protocol_id protocol;
    qa_net_address endpoint;
    char remote[256], reason[160];
    bool available;
} frontend_network_menu_connection;
bool frontend_network_menu_connection_read(const qa_frontend *, const frontend_network_menu_view *,
    qa_net_protocol_id, const char *remote, frontend_network_menu_connection *, qa_error *);
bool frontend_network_menu_connect(qa_frontend *, const frontend_network_menu_view *,
    const frontend_network_menu_connection *, qa_error *);
bool frontend_network_menu_download_read(const qa_frontend *, const frontend_network_menu_view *,
    qa_download_id, qa_download_view *, qa_error *);
bool frontend_network_menu_download_rows(const qa_frontend *, const frontend_network_menu_view *,
    qa_download_view *, size_t capacity, size_t *count, qa_error *);
bool frontend_network_menu_download_begin(qa_frontend *, const frontend_network_menu_view *,
    const qa_download_request *, const char *http_url, qa_download_id *, qa_error *);
bool frontend_network_menu_download_stop(qa_frontend *, const frontend_network_menu_view *,
    qa_download_id, bool suspend, qa_error *);
typedef struct frontend_network_menu_download_policy {
    const void *owner;
    qa_cvars *registry;
    size_t handle;
    uint64_t modification;
    int32_t integer;
    double number;
    bool numeric_permission, allowed;
} frontend_network_menu_download_policy;
/* Automatic-transfer permission comes from the actual physical CLIENT registry. */
bool frontend_network_menu_download_policy_read(const qa_frontend *, const frontend_network_menu_view *,
    frontend_network_menu_download_policy *, bool *present, qa_error *);
bool frontend_network_menu_download_policy_current(const qa_frontend *, const frontend_network_menu_view *,
    const frontend_network_menu_download_policy *);
bool frontend_network_menu_download_policy_set(qa_frontend *, const frontend_network_menu_view *,
    const frontend_network_menu_download_policy *, bool allowed, qa_error *);
#endif

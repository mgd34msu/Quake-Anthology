#ifndef QA_SERVER_ADMIN_H
#define QA_SERVER_ADMIN_H
#include "qa/network.h"
#include "qa/console.h"

typedef struct qa_server_admin qa_server_admin;
typedef enum qa_admin_result { QA_ADMIN_IGNORED, QA_ADMIN_THROTTLED, QA_ADMIN_DISABLED, QA_ADMIN_DENIED, QA_ADMIN_EXECUTED } qa_admin_result;
typedef bool (*qa_admin_write_fn)(void *, const char *, qa_error *);
typedef struct qa_admin_hooks {
    void *context;
    const char *(*password)(void *, bool limited);
    bool (*execute)(void *, const qa_net_address *, const char *command, bool limited,
                      qa_admin_write_fn, void *, qa_error *);
    bool (*send)(void *, const qa_net_address *, qa_bytes, qa_error *);
    bool (*travel)(void *, const char *map, qa_error *);
    bool (*players)(void *,uint32_t *,qa_error *);
    uint32_t (*random)(void *);
    void (*record)(void *, const qa_net_address *, qa_admin_result);
    qa_cvars *(*rate_registry)(void *);
    void (*print)(void *, const char *);
} qa_admin_hooks;
typedef struct qa_admin_options {
    qa_console_dialect dialect;
    uint32_t filters, rate_entries, burst;
    uint64_t rate_interval_ns, heartbeat_interval_ns;
    bool deny_matches, public_server;
    qa_admin_hooks hooks;
} qa_admin_options;
bool qa_server_admin_create(const qa_admin_options *, qa_server_admin **, qa_error *);
bool qa_server_admin_declarations(qa_cvars *, uint64_t owner, qa_error *);
bool qa_server_admin_policy(qa_server_admin *,qa_console_dialect,bool deny_matches,bool public_server,qa_error *);
bool qa_server_admin_adopt(qa_server_admin *,qa_server_admin **,qa_error *);
void qa_server_admin_destroy(qa_server_admin *);
bool qa_server_admin_filter(qa_server_admin *, const char *source_mask, bool remove, qa_error *);
bool qa_server_admin_rejects(const qa_server_admin *, const qa_net_address *);
bool qa_server_admin_limited_prefixes(qa_server_admin *, const char *const *, size_t, qa_error *);
bool qa_server_admin_limited_command(qa_server_admin *, const char *name,
    const char *raw_arguments, qa_admin_write_fn, void *, qa_error *);
bool qa_server_admin_filters_text(const qa_server_admin *, bool commands,
    qa_console_dialect, bool deny_matches, qa_buffer *, qa_error *);
bool qa_server_admin_receive(qa_server_admin *, const qa_net_datagram *, qa_admin_result *, qa_error *);
bool qa_server_admin_masters(qa_server_admin *, const qa_net_address *, size_t, qa_error *);
bool qa_server_admin_source_masters(qa_server_admin *,qa_console_dialect,const qa_net_address *,size_t,qa_error *);
bool qa_server_admin_request_heartbeat(qa_server_admin *,qa_error *);
bool qa_server_admin_refresh_masters(qa_server_admin *,qa_cvars *,qa_error *);
bool qa_server_admin_tick(qa_server_admin *, uint64_t now_ns, bool force, qa_error *);
bool qa_server_admin_shutdown(qa_server_admin *, qa_error *);
bool qa_server_admin_rotation(qa_server_admin *, const char *const *maps, size_t, bool shuffle, qa_error *);
bool qa_server_admin_next_map(qa_server_admin *, const char *current_map, bool *rotated, qa_error *);
bool qa_server_admin_save_filters(const qa_server_admin *, qa_buffer *, qa_error *);
bool qa_server_admin_restore_filters(qa_server_admin *, qa_bytes, qa_error *);
#endif

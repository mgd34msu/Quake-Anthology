#ifndef QA_FRONTEND_CLIENT_SOURCE_H
#define QA_FRONTEND_CLIENT_SOURCE_H
#include "qa/frontend.h"
#include "qa/launch_client.h"
#include "qa/launch_save.h"
#include "qa/application_client_save.h"
#include "qa/console_save.h"
#include "qa/persistence_content.h"

typedef struct frontend_client_source frontend_client_source;
typedef struct frontend_client_source_options {
    qa_launch_client_metadata metadata;
    qa_network_runtime *runtime;
    uint32_t physical_seat;
    qa_command_context input_origin;
    void *context;
    /* Registers the genuine selected CLIENT's declarations on its new heap.
     * Restoration imports that heap from QFCR instead of replaying this hook. */
    bool (*initialize)(void *, const qa_launch_instance *, qa_cvars *, const qa_command_context *, qa_error *);
    /* Advances the actual retained configuration programme. Completion is
     * supplied by that owner, never inferred from an empty console queue. */
    bool (*configure)(void *, const qa_application_client_source *, bool *complete, qa_error *);
    /* Installs actual native command handlers on this console. Cold import
     * calls it before rebinding saved registrations, without running config. */
    bool (*install)(void *, const qa_application_client_source *, bool restoring, qa_error *);
    void (*print)(void *, const qa_command_context *, const char *);
    bool (*connection_current)(void *, const qa_application_client_source *);
    bool (*entity_current)(void *, const qa_application_client_source *, uint32_t, uint64_t *);
    qa_command_fallback command, forward;
    bool (*allow_command)(void *, const qa_command_invocation *);
    qa_cvars *(*cvar_owner)(void *, const qa_command_context *, const char *);
    qa_cvars *(*visible_cvars)(void *, const qa_command_context *, size_t);
    bool (*cvar_edit)(void *, const qa_command_context *, qa_cvars *, struct qa_cvars_edit **, qa_error *);
    bool (*read_script)(void *, const qa_command_context *, const char *, qa_bytes *, void **, qa_error *);
    void (*release_script)(void *, void *);
    void (*script_complete)(void *, const qa_command_context *, const char *, bool);
} frontend_client_source_options;
typedef struct frontend_client_source_view {
    const frontend_client_source *owner;
    qa_application_client_source source;
    bool ready;
} frontend_client_source_view;
/* Partial construction is linked and returned before fallible acquisitions.
 * The caller keeps that owner reachable until checked destruction succeeds. */
bool frontend_client_source_create(qa_frontend *, const frontend_client_source_options *, frontend_client_source **, qa_error *);
bool frontend_client_source_advance(frontend_client_source *, bool *ready, qa_error *);
bool frontend_client_source_read(const frontend_client_source *, frontend_client_source_view *, qa_error *);
/* Installed physical topology only; never calls the transport or app current
 * facade. Cold codecs and child destructors retain readiness separately. */
bool frontend_client_source_metadata_read(const frontend_client_source *, frontend_client_source_view *, qa_error *);
bool frontend_client_source_current(const frontend_client_source_view *);
bool frontend_client_source_bind(frontend_client_source *, qa_net_client_id, qa_net_seat_id, uint64_t epoch, qa_error *);
bool frontend_client_source_drain(frontend_client_source *, size_t budget, size_t *, qa_error *);
bool frontend_client_source_idle(const frontend_client_source *);
bool frontend_client_source_retain(frontend_client_source *, qa_error *);
bool frontend_client_source_release(frontend_client_source *, qa_error *);
bool frontend_client_source_destroy(frontend_client_source **, qa_error *);
bool frontend_client_sources_idle(const qa_frontend *);
bool frontend_client_sources_destroy(qa_frontend *, qa_error *);
size_t frontend_client_source_count(const qa_frontend *);
frontend_client_source *frontend_client_source_at(const qa_frontend *, size_t);
bool frontend_client_sources_visit(const qa_frontend *, const qa_application_content_visitor *, qa_error *);

/* Physical prefix: the enclosing content graph and QFCR restore first. The
 * genuine runtime/transport callbacks and console resolvers are supplied by
 * the isolated Network owner; none are encoded as pointers. */
typedef struct frontend_client_source_state {
    qa_application_client_state application;
    qa_command_context command;
    qa_buffer console;
    uint32_t capabilities;
    bool ready;
} frontend_client_source_state;
typedef struct frontend_client_source_prefix {
    qa_launch_client_metadata recipe;
    qa_launch_restored_instance descriptor;
    frontend_client_source_state state;
    uint64_t content_id;
} frontend_client_source_prefix;
/* The graph-backed recipe and descriptor borrow the actual candidate graph
 * and session. State arrays/queue are owned; no view claim or callbacks occur. */
bool frontend_client_source_prefix_read(qa_frontend *, qa_application_content_graph *, qa_bytes,
    frontend_client_source_prefix *, qa_error *);
void frontend_client_source_prefix_free(frontend_client_source_prefix *);
bool frontend_client_source_capture(frontend_client_source *, frontend_client_source_state *, qa_error *);
void frontend_client_source_state_free(frontend_client_source_state *);
bool frontend_client_source_restore(qa_frontend *, const frontend_client_source_options *,
    const qa_launch_restored_instance *, const frontend_client_source_state *,
    const qa_console_save_resolvers *, frontend_client_source **, qa_error *);
bool frontend_client_source_checkpoint(frontend_client_source *, const qa_application_content_graph *, qa_buffer *, qa_error *);
bool frontend_client_source_restore_prefix(qa_frontend *, const frontend_client_source_options *,
    qa_application_content_graph *, const qa_console_save_resolvers *, qa_bytes,
    frontend_client_source **, qa_error *);
bool frontend_client_source_commands_capture(qa_frontend *, qa_application *,
    const qa_application_console_scope *, const qa_console *, qa_buffer *, qa_error *);
bool frontend_client_source_commands_restore(qa_frontend *, qa_application *,
    const qa_application_console_scope *, qa_console *, qa_bytes, qa_error *);
bool frontend_client_sources_finish_restore(qa_frontend *, qa_error *);
bool frontend_client_source_commands_owned(const qa_frontend *, const qa_application *,
    const qa_application_console_scope *, const qa_console *);
#endif

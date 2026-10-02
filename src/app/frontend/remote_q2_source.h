#ifndef QA_FRONTEND_REMOTE_Q2_SOURCE_H
#define QA_FRONTEND_REMOTE_Q2_SOURCE_H
#include "remote_q2_client.h"
#include "qa/launch_q2_client.h"
#include "qa/launch_save.h"
#include "qa/console_save.h"
#include "remote_q2_restore.h"
typedef struct frontend_remote_q2_source frontend_remote_q2_source;
typedef struct frontend_remote_q2_source_options {
    qa_launch_q2_client_metadata metadata;
    frontend_remote_q2_options client;
    /* Admit the real metadata-only app provider and return its actual pending
     * command namespace before constructing the physical registry/console. */
    bool (*prepare_namespace)(void *, const qa_launch_instance *, frontend_remote_q2_domain *, qa_error *);
    void (*print)(void *, const qa_command_context *, const char *);
    qa_command_fallback command;
    /* Actual source settings/startup owner applies its retained configuration
     * to this real CLIENT registry before the pending constructor is exposed. */
    bool (*configure)(void *, const qa_launch_instance *, qa_cvars *, qa_console *, qa_error *);
    /* The actual app row replaces only its retained CLIENT descriptor. Source
     * exposes the candidate physical tuple during this checked callback;
     * failure must leave the supplied previous app row unchanged. */
    bool (*admit_content)(void *, const frontend_remote_q2_domain *previous,
        const qa_launch_instance *candidate, const frontend_remote_q2_domain *candidate_domain, qa_error *);
} frontend_remote_q2_source_options;
typedef struct frontend_remote_q2_source_view {
    const frontend_remote_q2_source *owner;
    const qa_launch_instance *descriptor;
    frontend_remote_q2 *receiver;
    frontend_remote_q2_domain domain;
    bool bound;
} frontend_remote_q2_source_view;
/* The authenticated wire family selects its actual installed compiled CLIENT
 * profile. The caller owns the opened view until metadata preparation clones it. */
bool frontend_remote_q2_source_recipe(qa_catalog *, qa_net_protocol_id, const char *instance,
    uint32_t logical_seat, qa_launch_q2_client_metadata *, qa_vfs **prepared, qa_error *);
bool frontend_remote_q2_source_create(qa_frontend *, const frontend_remote_q2_source_options *,
    frontend_remote_q2_source **, qa_error *);
bool frontend_remote_q2_source_read(const frontend_remote_q2_source *, frontend_remote_q2_source_view *, qa_error *);
bool frontend_remote_q2_source_current(const frontend_remote_q2_source_view *);
bool frontend_remote_q2_source_bind(frontend_remote_q2_source *, const frontend_remote_q2_domain *, qa_error *);
bool frontend_remote_q2_source_pending_protocol(frontend_remote_q2_source *, qa_net_protocol_id, qa_error *);
bool frontend_remote_q2_source_drain(frontend_remote_q2_source *, size_t budget, size_t *, qa_error *);
bool frontend_remote_q2_source_destroy(frontend_remote_q2_source **, qa_error *);
/* Physical app CLIENT rows borrow this real heap owner. These qualifiers use
 * retained metadata/registry fields directly and never call app/current hooks.
 * The configure callback may use them before the receiver is constructed. */
bool frontend_remote_q2_source_owner_retain(void *, qa_error *);
bool frontend_remote_q2_source_owner_release(void *, qa_error *);
bool frontend_remote_q2_source_owner_idle(const frontend_remote_q2_source *);
/* Candidate import has returned its physical constructor, although receiver
 * readiness is still deferred to the real resource/lower-session joins. */
bool frontend_remote_q2_source_owner_import_idle(const frontend_remote_q2_source *);
bool frontend_remote_q2_source_owner_current(const frontend_remote_q2_source *,
    const qa_launch_instance *, const qa_console *, const qa_cvars *, const qa_command_context *, qa_error *);
typedef struct frontend_remote_q2_source_state {
    const qa_launch_instance *constructor, *selected;
    frontend_remote_q2_domain domain;
    qa_buffer console;
} frontend_remote_q2_source_state;
bool frontend_remote_q2_source_capture(const frontend_remote_q2_source *,
    frontend_remote_q2_source_state *, qa_error *);
void frontend_remote_q2_source_state_free(frontend_remote_q2_source_state *);
bool frontend_remote_q2_source_content_visit(const frontend_remote_q2_source *,
    const qa_application_content_visitor *, qa_error *);
typedef struct frontend_remote_q2_source_restore {
    qa_launch_q2_client_metadata constructor_request, selected_request;
    const qa_launch_restored_instance *constructor;
    /* NULL means the actual same descriptor owner as constructor. Distinct
     * descriptors carry distinct claimed views, never a second view claim. */
    const qa_launch_restored_instance *selected;
    frontend_remote_q2_domain domain;
    qa_bytes console, receiver;
    const qa_console_save_resolvers *console_resolvers;
    const frontend_remote_q2_restore_refs *receiver_refs;
    /* Restores the real app CLIENT row against this completed physical tuple.
     * Does not execute startup/configuration or require receiver readiness. */
    bool (*physical_admit)(void *, const qa_launch_instance *, qa_cvars *, qa_console *, qa_error *);
} frontend_remote_q2_source_restore;
/* QFCR is already staged. Imports its actual heap and console, then creates
 * the retained Q2RC receiver. Ordinary receiver current remains unavailable
 * until the real lower/session and resource dictionaries finish restoration.
 * Partial construction stays in out for checked candidate destruction. */
bool frontend_remote_q2_source_restore_prepare(qa_frontend *, const frontend_remote_q2_source_options *,
    const frontend_remote_q2_source_restore *, frontend_remote_q2_source **, qa_error *);
bool frontend_remote_q2_source_commands_capture(const frontend_remote_q2_source *, qa_application *,
    const qa_application_console_scope *, const qa_console *, qa_buffer *, qa_error *);
bool frontend_remote_q2_source_commands_restore(frontend_remote_q2_source *, qa_application *,
    const qa_application_console_scope *, qa_console *, qa_bytes, qa_error *);
bool frontend_remote_q2_source_finish_restore(frontend_remote_q2_source *, qa_error *);
#endif

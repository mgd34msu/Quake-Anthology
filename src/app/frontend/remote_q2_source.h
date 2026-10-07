#ifndef QA_FRONTEND_REMOTE_Q2_SOURCE_H
#define QA_FRONTEND_REMOTE_Q2_SOURCE_H
#include "remote_q2_client.h"
#include "qa/launch_q2_client.h"
#include "qa/launch_save.h"
#include "qa/console_save.h"
#include "remote_q2_restore.h"
#include "qa/application_client_prepare.h"
typedef struct frontend_remote_q2_source frontend_remote_q2_source;
typedef struct frontend_remote_q2_source_options {
    qa_launch_q2_client_metadata metadata;
    frontend_remote_q2_options client;
    /* Admit the real metadata-only app provider and return its actual pending
     * command namespace before constructing the CLIENT view and binding its callbacks. */
    bool (*prepare_namespace)(void *, const qa_launch_instance *, frontend_remote_q2_domain *, qa_error *);
    void (*print)(void *, const qa_command_context *, const char *);
    qa_command_fallback command;
    /* Actual source settings/startup owner applies its retained configuration
     * to this real CLIENT registry before the pending constructor is exposed. */
    bool (*configure)(void *, const qa_launch_instance *, qa_cvars *, qa_console *, qa_error *);
    bool (*initialize)(void *, const qa_launch_instance *, qa_cvars *, const qa_command_context *, qa_error *);
    bool (*install)(void *, bool restoring, qa_error *);
    bool (*configure_step)(void *, bool *complete, qa_error *);
    bool (*configuration_advance)(void *, qa_application_client_preparation *, bool *complete, qa_error *);
    bool (*retire)(void *, qa_error *);
    /* Actual retained disconnect receipt; never a live connection grant. */
    bool (*retirement_current)(void *, const qa_application_client_source *);
    void (*released)(void *);
    bool (*read_script)(void *, const qa_command_context *, const char *, qa_bytes *, void **, qa_error *);
    void (*release_script)(void *, void *);
    void (*script_complete)(void *, const qa_command_context *, const char *, bool);
    bool (*allow_command)(void *, const qa_command_invocation *);
    qa_command_fallback template_forward;
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
bool frontend_remote_q2_source_advance(frontend_remote_q2_source *, bool *ready, qa_error *);
bool frontend_remote_q2_source_configuration_advance(frontend_remote_q2_source *,
    qa_application_client_preparation *, bool *, qa_error *);
bool frontend_remote_q2_source_configuration_continue(frontend_remote_q2_source *,
    qa_application_client_preparation *, bool *, qa_error *);
bool frontend_remote_q2_source_configuration_completed(const frontend_remote_q2_source *);
bool frontend_remote_q2_source_retire(frontend_remote_q2_source *, qa_error *);
bool frontend_remote_q2_source_retirement_current(const frontend_remote_q2_source *,
    const qa_application_client_source *, const qa_console *, const qa_command_context *, qa_error *);
bool frontend_remote_q2_source_read(const frontend_remote_q2_source *, frontend_remote_q2_source_view *, qa_error *);
bool frontend_remote_q2_source_current(const frontend_remote_q2_source_view *);
/* Returned physical constructor tuple, including a genuinely pending
 * configuration programme with no receiver. No connection/readiness callback. */
bool frontend_remote_q2_source_physical_read(const frontend_remote_q2_source *, frontend_remote_q2_source_view *, qa_error *);
bool frontend_remote_q2_source_physical_current(const frontend_remote_q2_source_view *);
bool frontend_remote_q2_source_retirement_custody_read(const frontend_remote_q2_source *,
    frontend_remote_q2_source_view *, qa_error *);
/* Exact returned Source/receiver association retained at actual disconnect
 * admission. No callback or live-connection readiness is observed. */
bool frontend_remote_q2_source_retirement_metadata_current(const frontend_remote_q2 *, qa_error *);
bool frontend_remote_q2_source_bind(frontend_remote_q2_source *, const frontend_remote_q2_domain *, qa_error *);
bool frontend_remote_q2_source_pending_protocol(frontend_remote_q2_source *, qa_net_protocol_id, qa_error *);
bool frontend_remote_q2_source_pending_capabilities(frontend_remote_q2_source *, const qa_q2_connect_request *, bool *, qa_error *);
bool frontend_remote_q2_source_material_scripts(const qa_q2_connect_request *, bool *, qa_error *);
bool frontend_remote_q2_source_drain(frontend_remote_q2_source *, size_t budget, size_t *, qa_error *);
bool frontend_remote_q2_source_destroy(frontend_remote_q2_source **, qa_error *);
bool frontend_remote_q2_source_rebind_ready(const frontend_remote_q2_source *, qa_frontend *, qa_error *);
void frontend_remote_q2_source_rebind(frontend_remote_q2_source *, qa_frontend *);
/* Physical app CLIENT rows borrow this real view owner. These qualifiers use
 * retained metadata/registry fields directly and never call app/current hooks.
 * The configure callback may use them before the receiver is constructed. */
bool frontend_remote_q2_source_owner_retain(void *, qa_error *);
bool frontend_remote_q2_source_owner_release(void *, qa_error *);
bool frontend_remote_q2_source_owner_idle(const frontend_remote_q2_source *);
/* Candidate import has returned its physical constructor, although receiver
 * readiness is still deferred to the real resource/lower-session joins. */
bool frontend_remote_q2_source_owner_import_idle(const frontend_remote_q2_source *);
/* Checked cleanup of an isolated, returned partial import constructor. */
bool frontend_remote_q2_source_owner_retirement_idle(const frontend_remote_q2_source *);
bool frontend_remote_q2_source_constructor_read(const frontend_remote_q2_source *, const qa_launch_instance **, qa_error *);
bool frontend_remote_q2_source_owner_current(const frontend_remote_q2_source *,
    const qa_launch_instance *, const qa_console *, const qa_cvars *, const qa_command_context *, qa_error *);
bool frontend_remote_q2_source_content_visit(const frontend_remote_q2_source *,
    const qa_application_content_visitor *, qa_error *);
bool frontend_remote_q2_source_restore_abort_ready(const frontend_remote_q2_source *,
    const qa_application_client_source *,qa_error *);
#endif

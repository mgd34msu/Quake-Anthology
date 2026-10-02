#ifndef QA_FRONTEND_NETWORK_PRESENTATION_H
#define QA_FRONTEND_NETWORK_PRESENTATION_H
#include "network_prediction.h"
#include "network_config.h"
#include "qa/application_q3_factory.h"
#include "qa/network_q3_runtime.h"
#include "qa/q3_host.h"

typedef struct frontend_q3_content frontend_q3_content;
struct q3n_remote_publication;
struct q3n_remote_command;

/* The connecting source UI has its real physical CLIENT configuration before
 * any received gamestate, map, or CGAME Init counter exists. */
typedef struct frontend_network_client_attempt {
    qa_application_q3_remote_source source;
    frontend_remote_config_view configuration;
    qa_net_client_id connection;
    qa_net_address endpoint;
    uint64_t epoch, restart_generation;
    qa_q3_admission_phase phase;
    bool attached;
} frontend_network_client_attempt;
bool frontend_network_client_attempt_read(const qa_frontend *,
    frontend_network_client_attempt *, bool *present, qa_error *);
bool frontend_network_client_attempt_current(const qa_frontend *,
    const frontend_network_client_attempt *);
/* The real per-role frontend lease retains this binding through checked
 * release. Admission may progress at the same attempt; decoded replacement
 * requires its separate DATA facade and media owner. */
typedef struct frontend_network_initial_services_binding {
    qa_frontend *frontend;
    qa_application *application;
    const void *network;
    frontend_network_client_attempt attempt;
    void *context;
    /* The genuine per-role lease proves Factory's exact entered host and
     * namespace during Shutdown; this never grants a new epoch's data. */
    bool (*entered)(void *, const frontend_network_client_attempt *, qa_error *);
    bool restore_candidate;
} frontend_network_initial_services_binding;
bool frontend_network_client_attempt_services(qa_frontend *,
    const frontend_network_client_attempt *, frontend_network_initial_services_binding *,
    void *lease_context,
    bool (*entered)(void *, const frontend_network_client_attempt *, qa_error *),
    qa_q3_host_client_services *, qa_error *);

/* Borrowed only during the real idle private CLIENT initialization entry.
 * Geometry belongs to the frontend owner constructed from this actual map. */
typedef struct frontend_network_construction_source {
    qa_application_q3_remote_source source;
    qa_net_client_id connection;
    uint64_t epoch, restart_generation;
    frontend_q3_content *content_owner;
    qa_vfs *content, *prepared_mounts;
    const qa_resource *map;
    qa_product_id product;
    const qa_q3_gamestate *gamestate;
    qa_network_q3_client_init init;
} frontend_network_construction_source;

bool frontend_network_construction_source_read(const qa_frontend *,
    frontend_network_construction_source *, qa_error *);
bool frontend_network_construction_source_current(const qa_frontend *,
    const frontend_network_construction_source *);

/* This domain survives reliable-command execution after Init. The counters
 * identify the recorded Init entry, rather than the current wire cursors. */
typedef struct frontend_network_client_domain {
    qa_application_q3_remote_source source;
    qa_net_client_id connection;
    uint64_t epoch, restart_generation;
    frontend_q3_content *content_owner;
    qa_vfs *content, *prepared_mounts;
    const qa_resource *map;
    qa_product_id product;
    const qa_q3_gamestate *gamestate;
    qa_network_q3_client_init initial;
} frontend_network_client_domain;
bool frontend_network_client_domain_read(const qa_frontend *,
    frontend_network_client_domain *, qa_error *);
bool frontend_network_client_domain_current(const qa_frontend *,
    const frontend_network_client_domain *);
/* Final resource sealing reads owned metadata under the actual inventory
 * fence, without invoking connection, media, or source callbacks. */
bool frontend_network_client_domain_metadata_read(const qa_frontend *,
    frontend_network_client_domain *, qa_error *);
bool frontend_network_client_domain_metadata_current(const qa_frontend *,
    const frontend_network_client_domain *);
/* Native CG_Init reads the actual transport publication before a retail
 * collision snapshot exists. Its reached-command receipt has the same native
 * execute-result owner as the later snapshot facade. */
bool frontend_network_native_publication_read(const qa_frontend *,
    struct q3n_remote_publication *, qa_error *);
bool frontend_network_native_publication_current(const qa_frontend *,
    const struct q3n_remote_publication *);
bool frontend_network_native_command_read(qa_frontend *, int32_t,
    struct q3n_remote_command *, qa_error *);
bool frontend_network_native_command_current(const qa_frontend *,
    const struct q3n_remote_command *);
/* Sends one literal reliable command from the actual physical CLIENT origin.
 * The caller retains its frontend service lease; no original VM lease is used. */
bool frontend_network_client_reliable(qa_frontend *, const qa_command_context *,
    const char *, qa_error *);
/* Applies the console forward policy to the original qualified invocation,
 * at its actual execution boundary, before any reliable append. */
bool frontend_network_client_forward(qa_frontend *, const qa_command_invocation *, qa_error *);

typedef struct frontend_network_presentation_source {
    frontend_network_prediction_source prediction;
    const qa_q3_gamestate *gamestate;
    int32_t initial_message, initial_command, latest_message, latest_time;
    int32_t presentation_time;
    int32_t server_message, received_command, executed_command;
} frontend_network_presentation_source;
typedef struct frontend_network_presentation_command {
    frontend_network_presentation_source source;
    int32_t sequence;
    const qa_q3_tokens *tokens;
    bool present;
} frontend_network_presentation_command;

/* The caller retains its actual receiver lease; these are borrowed native
 * transport data services, without constructing a source host. */
bool frontend_network_presentation_services(qa_frontend *,
    const qa_application_q3_client_context *, qa_q3_host_client_services *, qa_error *);
bool frontend_network_presentation_source_read(const qa_frontend *,
    frontend_network_presentation_source *, bool *, qa_error *);
bool frontend_network_presentation_source_current(const qa_frontend *,
    const frontend_network_presentation_source *);
bool frontend_network_presentation_snapshot(const qa_frontend *,
    const frontend_network_presentation_source *, int32_t,
    const qa_q3_snapshot **, int32_t *, qa_error *);
bool frontend_network_presentation_snapshot_current(const qa_frontend *,
    const frontend_network_presentation_source *, const qa_q3_snapshot *);
/* Executes the actual native reliable producer, then returns its reached
 * argument owner and a fresh source cut after any map_restart effect. */
bool frontend_network_presentation_execute(qa_frontend *,
    const frontend_network_presentation_source *, int32_t,
    frontend_network_presentation_command *, qa_error *);
/* A reached receipt can be absent for bcs chunks or a cycled demo command.
 * The actual execute result owns its sequence independently of visible argv
 * and the native acknowledgement, which a cycled demo does not advance. */
bool frontend_network_presentation_command_current(const qa_frontend *,
    const frontend_network_presentation_command *);
#endif

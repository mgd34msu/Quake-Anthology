#ifndef QA_APPLICATION_Q3_FACTORY_H
#define QA_APPLICATION_Q3_FACTORY_H
#include "qa/application_startup_prepare.h"

#include "qa/application_q3_client.h"
#include "qa/persistence_content.h"

typedef struct qa_application_q3_remote_source {
    const qa_launch_instance *descriptor;
    qa_application_q3_client_context receiver;
    uint64_t configuration_generation, connection_epoch;
} qa_application_q3_remote_source;

typedef struct qa_application_q3_remote_replacement {
    qa_application_q3_remote_source previous;
    qa_catalog *catalog;
    qa_product_id product;
    qa_vfs *prepared_mounts;
    const char *cgame_path, *ui_path;
    qa_program_kind cgame_runtime;
    uint64_t connection_epoch;
} qa_application_q3_remote_replacement;

typedef struct qa_application_q3_role_receipt {
    qa_qvm_role role;
    qa_actor_owner receiver;
    uint32_t seat;
    uint64_t service_owner, configuration_generation, connection_epoch;
    const qa_launch_instance *descriptor;
    const qa_resource *artifact;
    const qa_vfs_acquisition *acquisition;
    qa_vfs *artifact_view;
} qa_application_q3_role_receipt;
typedef struct qa_application_q3_role_artifact {
    qa_application_q3_role_receipt source;
    const char *path;
} qa_application_q3_role_artifact;
/* Actual retained opening, available before Init. Path comes from the real
 * constructed role, including authored companion overrides. */
bool qa_application_q3_role_artifact_read(qa_application *, qa_actor_owner,
    qa_qvm_role, uint32_t seat, qa_application_q3_role_artifact *, qa_error *);
typedef struct qa_application_q3_remote_recipe {
    qa_application_q3_role_artifact cgame, ui;
    const char *cgame_path, *ui_path;
    qa_program_kind cgame_runtime;
    qa_actor_owner menu_receiver;
    /* Replaces this receiver's source UI helper, independently of MENU. */
    bool replace_ui;
} qa_application_q3_remote_recipe;
/* decoded is the transport's current held gamestate at its idle decode
 * boundary. SDK pure/demo policy can author bytecode replacement paths while
 * cgame/ui retain the exact previous physical openings for qualification. */
bool qa_application_q3_remote_recipe_read(qa_application *,
    const qa_application_q3_remote_source *, const qa_q3_gamestate *decoded,
    qa_application_q3_remote_recipe *, qa_error *);

/* These borrow genuine retained engine metadata, including its owned VFS.
 * Initial descriptors and later client-only descriptors share this contract. */
bool qa_application_q3_remote_source_read(qa_application *, qa_actor_owner,
    uint32_t, uint64_t connection_epoch, qa_application_q3_remote_source *, qa_error *);
bool qa_application_q3_remote_source_current(qa_application *,
    const qa_application_q3_remote_source *);
/* Available only inside the real q3_services construction callback. The
 * imports are the actual pending request; the frontend prepares its final
 * private registry before retaining keys, AUTH, collision and host imports.
 * source_client remains unbound until the real GAME binding follows. */
bool qa_application_q3_preconstruction_source_read(qa_application *, qa_actor_owner,
    qa_qvm_role, uint32_t seat, qa_application_q3_client_preparation *, qa_error *);
/* Actual CLIENT configuration views exist before role hosts and imports.
 * GAME, CGAME and UI use the common console and canonical cvar store. */
bool qa_application_q3_client_configuration_read(qa_application *, qa_actor_owner,
    qa_qvm_role role, uint32_t authored_seat, qa_application_startup_source *, qa_error *);
/* The exact retained native CLIENT has returned its service, transport,
 * acquired modules and synchronous callbacks. Its configuration stays owned. */
bool qa_application_q3_client_configuration_unborrowed(qa_application *,
    const qa_application_startup_source *);
/* Exact held child identity, only inside checked hosted CLIENT retirement. */
bool qa_application_q3_client_configuration_retiring(const qa_application *,
    const qa_application_startup_source *);
/* Original CGAME/UI host callbacks retain this physical namespace only while
 * their real source entry, Shutdown or native destructor is entered. */
bool qa_application_q3_client_configuration_entered(const qa_application *,
    const qa_application_startup_source *);
/* Named cvar callbacks qualify their exact retained host pointer as well as
 * the original GAME or CLIENT physical configuration. */
bool qa_application_q3_configuration_host_entered(const qa_application *,
    const qa_application_startup_source *, const qa_q3_host *);
/* Reads the real late GAME declaration only during that exact host's source
 * entry. Early configuration preparation may precede role construction. */
bool qa_application_q3_game_configuration_entered_read(const qa_application *,
    const qa_q3_host *, qa_application_startup_source *, qa_error *);
/* Consume source Shutdown once at the idle decoder boundary. Actual hosts,
 * registry and metadata remain retained, ready for content replacement. */
bool qa_application_q3_remote_clear(qa_application *,
    const qa_application_q3_remote_source *, qa_application_q3_remote_source *, qa_error *);
typedef struct qa_application_q3_remote_binding {
    qa_application_q3_remote_source previous;
    uint64_t new_epoch;
    void *connection;
    /* Pure proof that the retained old transport is absent and the fresh
     * generation-bearing transport owns this actual receiver and seat. */
    bool (*current)(void *, uint64_t old_epoch, uint64_t new_epoch, qa_error *);
} qa_application_q3_remote_binding;
/* Rebind only already cleared roles at the idle new-admission boundary.
 * Their actual hosts, registry, descriptor and content generation survive. */
bool qa_application_q3_remote_rebind(qa_application *,
    const qa_application_q3_remote_binding *, qa_application_q3_remote_source *, qa_error *);
/* Builds CGAME/UI hosts from independently cloned prepared content. No GAME,
 * actor, session clock or world configuration is constructed or changed.
 * Source Init remains a separate operation after decoded gamestate is ready. */
bool qa_application_q3_remote_replace(qa_application *,
    const qa_application_q3_remote_replacement *, qa_application_q3_remote_source *, qa_error *);
typedef struct qa_application_q3_remote_init {
    qa_application_q3_remote_source source;
    int32_t server_message, last_executed_server_command, client_number;
    void *connection;
    /* Pure observation of the real decoded connection and retained epoch. */
    bool (*current)(void *, uint64_t connection_epoch, int32_t server_message,
        int32_t last_executed_server_command, int32_t client_number, qa_error *);
} qa_application_q3_remote_init;
bool qa_application_q3_remote_initialize(qa_application *,
    const qa_application_q3_remote_init *, qa_error *);
/* The actual CGAME Init update-screen trap draws its already initialized
 * source UI helper. Other entries report drawn=false without a guest call. */
bool qa_application_q3_source_loading_screen(qa_application *, qa_actor_owner,
    uint32_t seat, bool *drawn, qa_error *);
/* Successful Init is required, independently of an entered failing Init. */
bool qa_application_q3_role_receipt_read(qa_application *, qa_actor_owner,
    qa_qvm_role, uint32_t, qa_application_q3_role_receipt *, qa_error *);
bool qa_application_q3_role_receipt_current(qa_application *,
    const qa_application_q3_role_receipt *);
/* Native/mixed providers without a cleared external guest role report false. */
bool qa_application_q3_role_loading(qa_application *, qa_actor_owner,
    qa_qvm_role, uint32_t seat, bool *, qa_error *);
/* Visits actual retained descriptor, decoder and artifact content owners,
 * including private prior generations still retained by immutable caches. */
bool qa_application_q3_content_visit(const qa_application *,
    const qa_application_content_visitor *, qa_error *);

#endif

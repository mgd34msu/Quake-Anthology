#ifndef QA_APPLICATION_Q3_FACTORY_H
#define QA_APPLICATION_Q3_FACTORY_H

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

/* These borrow genuine retained engine metadata, including its owned VFS.
 * Initial descriptors and later client-only descriptors share this contract. */
bool qa_application_q3_remote_source_read(qa_application *, qa_actor_owner,
    uint32_t, uint64_t connection_epoch, qa_application_q3_remote_source *, qa_error *);
bool qa_application_q3_remote_source_current(qa_application *,
    const qa_application_q3_remote_source *);
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
/* Successful Init is required, independently of an entered failing Init. */
bool qa_application_q3_role_receipt_read(qa_application *, qa_actor_owner,
    qa_qvm_role, uint32_t, qa_application_q3_role_receipt *, qa_error *);
bool qa_application_q3_role_receipt_current(qa_application *,
    const qa_application_q3_role_receipt *);
/* Visits actual retained descriptor, decoder and artifact content owners,
 * including private prior generations still retained by immutable caches. */
bool qa_application_q3_content_visit(const qa_application *,
    const qa_application_content_visitor *, qa_error *);

#endif

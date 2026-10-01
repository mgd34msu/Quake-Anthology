#ifndef QA_FRONTEND_NETWORK_CONTENT_Q3_H
#define QA_FRONTEND_NETWORK_CONTENT_Q3_H

#include "qa/application_q3_client.h"
#include "qa/network_q3.h"

typedef struct frontend_q3_content frontend_q3_content;
typedef enum frontend_q3_content_phase {
    FRONTEND_Q3_CONTENT_CATALOG, FRONTEND_Q3_CONTENT_PREPARED,
    FRONTEND_Q3_CONTENT_PUBLISHED, FRONTEND_Q3_CONTENT_MEDIA_READY
} frontend_q3_content_phase;

/* The transport owner qualifies the decoded connection epoch before every
 * operation. No GAME actor or GAME source is used to construct remote content. */
typedef struct frontend_q3_content_request {
    qa_application *application;
    const qa_launch_instance *descriptor;
    qa_catalog *catalog;
    qa_application_q3_client_context receiver;
    uint64_t configuration_generation, connection_epoch;
    const qa_q3_gamestate *gamestate;
    void *connection;
    bool (*connection_current)(void *, uint64_t connection_epoch, qa_error *);
} frontend_q3_content_request;

typedef struct frontend_q3_content_view {
    qa_catalog *catalog;
    qa_product_id selected, base;
    qa_vfs *mounts;
    const qa_q3_gamestate *gamestate;
    const qa_resource *map;
    const char *map_path, *selected_directory, *base_directory;
    const char *selected_write_path, *base_write_path;
    qa_fs_root *selected_write_root, *base_write_root;
    const qa_q3_package *referenced;
    size_t referenced_count;
    const uint32_t *loaded_checksums;
    size_t loaded_count;
    uint32_t checksum_feed;
    int32_t server_id;
    bool pure;
} frontend_q3_content_view;

/* Publication is produced by the actual application private client factory.
 * It replaces the receiver descriptor and service lifetime without replacing
 * the channel, connection epoch, foreign providers or shared world. Its real
 * descriptor owns an independent clone of the prepared mounts. The content
 * owner keeps its candidate view until its own destruction. */
typedef struct frontend_q3_content_publication {
    const qa_launch_instance *descriptor;
    qa_application_q3_client_context receiver;
    uint64_t configuration_generation, connection_epoch;
} frontend_q3_content_publication;

/* A role factory produces this only from the qualified loaded artifact and
 * successful source Init. Media views are the actual private presentation
 * views used by that role, rather than probes or temporary canonical reads. */
typedef struct frontend_q3_content_role_receipt {
    qa_qvm_role role;
    qa_actor_owner receiver;
    uint32_t seat;
    uint64_t service_owner, configuration_generation, connection_epoch;
    const qa_launch_instance *descriptor;
    const qa_resource *artifact;
    const qa_vfs_acquisition *acquisition;
    qa_vfs *artifact_view;
    qa_vfs *const *media_views;
    size_t media_view_count;
    /* Read-only producer predicate over the real retained role/artifact and
     * completed Init. A stale or merely opened artifact cannot satisfy it. */
    void *producer;
    bool (*current)(void *, const struct frontend_q3_content_role_receipt *, qa_error *);
} frontend_q3_content_role_receipt;

bool frontend_q3_content_create(const frontend_q3_content_request *,
    frontend_q3_content **empty, qa_error *);
void frontend_q3_content_destroy(frontend_q3_content *);
frontend_q3_content_phase frontend_q3_content_state(const frontend_q3_content *);
bool frontend_q3_content_read(const frontend_q3_content *, frontend_q3_content_view *, qa_error *);
/* Downloads precede this operation. A new gamestate gets a fresh owner even
 * when map, fs_game and checksum feed compare equal to the previous state. */
bool frontend_q3_content_prepare(frontend_q3_content *, qa_error *);
bool frontend_q3_content_publish(frontend_q3_content *,
    const frontend_q3_content_publication *, qa_error *);
/* Both genuine initialized roles must still be current when called. The
 * caller obtains receipts from the application artifact/Init producer. */
bool frontend_q3_content_media_ready(frontend_q3_content *,
    const frontend_q3_content_role_receipt *cgame,
    const frontend_q3_content_role_receipt *ui, qa_error *);
bool frontend_q3_content_pure_command(frontend_q3_content *,
    char *, size_t capacity, qa_error *);
bool frontend_q3_content_download_reference(const frontend_q3_content *,
    const char *remote, uint32_t checksum, qa_error *);
/* Returns an owned path relative to the actual family download root. */
bool frontend_q3_content_download_destination(const frontend_q3_content *,
    const char *remote, char **empty, qa_error *);

#endif

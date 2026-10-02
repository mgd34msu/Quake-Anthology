#ifndef QA_LAUNCH_SAVE_H
#define QA_LAUNCH_SAVE_H
#include "qa/launch.h"

typedef struct qa_configuration_checkpoint {
    uint64_t generation;
    bool has_current;
} qa_configuration_checkpoint;
bool qa_configuration_checkpoint_capture(const qa_configuration *, qa_configuration_checkpoint *, qa_error *);
typedef struct qa_launch_restored_instance {
    qa_catalog *catalog;
    qa_launch_provider selection;
    qa_vfs *content;
    const qa_resource *artifact, *declaration;
    const qa_vfs_acquisition *artifact_acquisition;
    const qa_launch_resource *interfaces;
    size_t interface_count;
    const qa_catalog_weapon_behavior *const *behaviors;
    size_t behavior_count;
    qa_sha256_digest identity;
} qa_launch_restored_instance;
typedef struct qa_launch_restore_content {
    void *context;
    /* Transfer the actual owning view, also on partial failure. All other
     * returned metadata is borrowed until preparation finishes. */
    bool (*mounts)(void *, qa_vfs **empty, qa_error *);
    bool (*instance)(void *, const qa_launch_provider *, qa_launch_restored_instance *, qa_error *);
    size_t resource_count;
    bool (*resource)(void *, size_t, qa_launch_resource *, qa_error *);
    /* Transfers content's actual view reference; the other fields borrow. */
    bool (*resource_origin)(void *, size_t, qa_launch_resource_origin *, qa_error *);
} qa_launch_restore_content;
/* Fresh isolated manager only. Preparation retains real content and constructs
 * actual provider states. Ordinary publication callbacks never run on this
 * path: the application must restore routing, world and source owners itself
 * before publishing the complete isolated application. */
bool qa_configuration_prepare_restored(qa_configuration *, const qa_launch_draft *,
    const qa_configuration_checkpoint *, const qa_launch_restore_content *,
    qa_configuration_transaction **, qa_error *);
bool qa_configuration_commit_restored(qa_configuration_transaction *, qa_error *);

/* Private draft continuation, including incomplete/invalid composition and
 * original private string order. The caller qualifies the actual catalog
 * snapshot; valid product IDs map by stable identity, unknown draft IDs remain
 * unknown in that qualified catalog. Actor references preserve provenance.
 * Restore constructs an empty private draft without defaults or validation. */
bool qa_launch_draft_checkpoint(const qa_launch_draft *, const qa_actor_registry *, qa_buffer *, qa_error *);
bool qa_launch_draft_restore(qa_catalog *, const qa_actor_registry *, qa_bytes, qa_launch_draft **, qa_error *);
#endif

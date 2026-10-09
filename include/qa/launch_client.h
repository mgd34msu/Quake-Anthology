#ifndef QA_LAUNCH_CLIENT_H
#define QA_LAUNCH_CLIENT_H
#include "qa/launch.h"
typedef struct qa_launch_client_metadata {
    qa_catalog *catalog;
    qa_product_id profile, selected;
    const qa_vfs *prepared;
    const char *instance;
    uint32_t seat;
    /* Actual selected CLIENT dialect, independently of the remote protocol. */
    qa_ruleset_id clock;
} qa_launch_client_metadata;
/* Retains only compiled CLIENT metadata and the actual prepared content.
 * Received content can carry an external GAME without executing that GAME. */
bool qa_launch_instance_prepare_client_profile(const qa_launch_client_metadata *,
    qa_launch_instance_lease **, qa_error *);
bool qa_launch_instance_restore_client_profile(const qa_launch_client_metadata *,
    const struct qa_launch_restored_instance *, qa_launch_instance_lease **, qa_error *);
#endif

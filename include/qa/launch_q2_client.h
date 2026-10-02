#ifndef QA_LAUNCH_Q2_CLIENT_H
#define QA_LAUNCH_Q2_CLIENT_H
#include "qa/launch.h"

typedef struct qa_launch_q2_client_metadata {
    qa_catalog *catalog;
    qa_product_id profile, selected;
    const qa_vfs *prepared;
    const char *instance;
    uint32_t seat;
} qa_launch_q2_client_metadata;
/* Owns a real compiled CLIENT descriptor from the catalog's Q2 execution
 * profile and prepared content. No GAME preparation or actor is performed. */
bool qa_launch_instance_prepare_q2_client_metadata(const qa_launch_q2_client_metadata *,
    qa_launch_instance_lease **, qa_error *);
/* Takes the actual claimed saved private view after argument admission,
 * including on failure. Restores metadata without discovery, acquire or Init. */
bool qa_launch_instance_restore_q2_client_metadata(const qa_launch_q2_client_metadata *,
    const struct qa_launch_restored_instance *, qa_launch_instance_lease **, qa_error *);
#endif

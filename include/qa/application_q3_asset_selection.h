#ifndef QA_APPLICATION_Q3_ASSET_SELECTION_H
#define QA_APPLICATION_Q3_ASSET_SELECTION_H

#include "qa/application.h"

/* A pure content observation of a genuine full actor's selected role. This
 * does not read weapon status, project a visual, register or load a resource. */
typedef struct qa_application_q3_asset_selection {
    qa_actor_id actor;
    qa_launch_role role;
    qa_actor_owner provider;
    const qa_launch_instance *launch;
    qa_vfs *content;
    qa_product_id product;
    qa_game_family family;
    uint64_t publication_generation, map_revision;
} qa_application_q3_asset_selection;

bool qa_application_q3_asset_selection_read(qa_application *, qa_actor_id,
    qa_launch_role, qa_application_q3_asset_selection *, bool *found, qa_error *);
bool qa_application_q3_asset_selection_current(qa_application *,
    const qa_application_q3_asset_selection *);

#endif

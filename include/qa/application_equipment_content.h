#ifndef QA_APPLICATION_EQUIPMENT_CONTENT_H
#define QA_APPLICATION_EQUIPMENT_CONTENT_H

#include "qa/application.h"

typedef struct qa_application_equipment_content {
    qa_actor_owner owner, selected_owner;
    qa_string_id service_owner;
    const qa_launch_instance *descriptor;
    const qa_resource *artifact;
    const qa_vfs_acquisition *acquisition;
    const qa_vfs *files;
} qa_application_equipment_content;

/* The separate gear namespace borrows its genuine selected content authority.
 * This performs no source call or file admission and remains usable during
 * candidate import. References expire when that retained runtime retires.
 * Output is unchanged on failure; ordinary provider owners are not gear. */
bool qa_application_equipment_content_read(const qa_application *, qa_actor_owner,
    qa_application_equipment_content *, qa_error *);
bool qa_application_equipment_content_current(const qa_application *,
    const qa_application_equipment_content *);

#endif

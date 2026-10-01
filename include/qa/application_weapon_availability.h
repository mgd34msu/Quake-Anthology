#ifndef QA_APPLICATION_WEAPON_AVAILABILITY_H
#define QA_APPLICATION_WEAPON_AVAILABILITY_H

#include "qa/application.h"
#include "qa/inventory.h"

/* A copied canonical definition must belong to the actual selected arsenal.
 * Unknown external availability remains absent, without a native substitute. */
bool qa_application_weapon_availability_read(qa_application *, qa_actor_id,
    const qa_item_definition *, bool *available, bool *found, qa_error *);

#endif

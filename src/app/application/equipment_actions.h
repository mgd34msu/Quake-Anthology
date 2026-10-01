#ifndef QA_APPLICATION_EQUIPMENT_ACTIONS_H
#define QA_APPLICATION_EQUIPMENT_ACTIONS_H

#include "internal.h"

/* The context is the real native GAME inventory owner. Unhandled weapon
 * requests continue in that owner's original inventory callback. */
bool application_equipment_q3_weapon_request(void *, qa_actor_id, qa_item_id,
    bool *handled, qa_error *);

#endif

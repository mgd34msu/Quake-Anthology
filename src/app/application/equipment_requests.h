#ifndef QA_APPLICATION_EQUIPMENT_REQUESTS_H
#define QA_APPLICATION_EQUIPMENT_REQUESTS_H
#include "internal.h"
#include "qa/application_equipment.h"
/* Match the shared catalog without executing an item action. Labels and
 * ambiguity text borrow the caller's command arena. A handled ambiguity has
 * selection.item == 0; an unknown name is unhandled. */
qa_command_result application_equipment_item_resolve(qa_application *,qa_actor_id,
    const qa_command_invocation *,qa_arena *,qa_item_definition *,
    qa_application_item_selection *,qa_error *);
bool application_equipment_primary_accepts(void *,qa_actor_id,qa_actor_owner,qa_item_id,bool *,qa_error *);
bool application_equipment_primary_select(void *,qa_actor_id,qa_actor_owner,qa_item_id,bool *,qa_error *);
#endif

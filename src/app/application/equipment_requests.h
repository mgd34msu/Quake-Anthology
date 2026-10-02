#ifndef QA_APPLICATION_EQUIPMENT_REQUESTS_H
#define QA_APPLICATION_EQUIPMENT_REQUESTS_H
#include "internal.h"
bool application_equipment_primary_accepts(void *,qa_actor_id,qa_actor_owner,qa_item_id,bool *,qa_error *);
bool application_equipment_primary_select(void *,qa_actor_id,qa_actor_owner,qa_item_id,bool *,qa_error *);
bool application_equipment_request_weapon(qa_application *,qa_actor_id,qa_actor_owner,qa_item_id,bool *,qa_error *);
#endif

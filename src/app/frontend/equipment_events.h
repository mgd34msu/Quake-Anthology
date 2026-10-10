#ifndef QA_FRONTEND_EQUIPMENT_EVENTS_H
#define QA_FRONTEND_EQUIPMENT_EVENTS_H

#include "internal.h"
#include "qa/application_equipment_events.h"

typedef struct frontend_equipment_events frontend_equipment_events;

bool frontend_equipment_events_create(qa_frontend *, frontend_equipment_events **, qa_error *);
bool frontend_equipment_events_idle(const frontend_equipment_events *);
bool frontend_equipment_events_destroy(frontend_equipment_events *, qa_error *);
/* Publication moves this continuation with its actual application and HUD /
 * console owners. Rebind itself performs no output or allocation. */
void frontend_equipment_events_rebind(frontend_equipment_events *, qa_frontend *);
bool frontend_equipment_events_drain(frontend_equipment_events *, qa_error *);
#endif

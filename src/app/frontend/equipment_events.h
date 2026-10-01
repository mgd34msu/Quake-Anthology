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
/* Only CONFIGSTRING events reached by drain are visible here. These are
 * private gear namespaces, separate from primary GAME/CGAME rows. */
bool frontend_equipment_events_configstring(const frontend_equipment_events *, qa_actor_owner,
    uint32_t, const char **, bool *present, qa_error *);
bool frontend_equipment_events_checkpoint(const frontend_equipment_events *, qa_buffer *, qa_error *);
bool frontend_equipment_events_restore(frontend_equipment_events *, qa_bytes, qa_error *);

#endif

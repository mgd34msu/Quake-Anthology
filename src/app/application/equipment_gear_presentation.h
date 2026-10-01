#ifndef QA_APPLICATION_EQUIPMENT_GEAR_PRESENTATION_H
#define QA_APPLICATION_EQUIPMENT_GEAR_PRESENTATION_H

#include "equipment_runtime.h"

typedef struct application_equipment_gear_presentation {
    qa_actor_id actor;
    qa_actor_owner primary;
    uint64_t publication_generation, map_revision;
    application_equipment_runtime_source source;
    application_q3_gear_view gear;
} application_equipment_gear_presentation;

/* Only the controller's actual active QVM grapple weapon slot is selected.
 * The immutable profile supplies weapon/model/anchor declarations; the copied
 * player remains the real source PS, including its original weapon word.
 * Pointers borrow the retained runtime until mutation/retirement. */
bool application_equipment_gear_presentation_read(qa_application *, qa_actor_id,
    application_equipment_gear_presentation *, bool *selected, qa_error *);
/* Pure lifetime/selection qualification, also usable during capture. */
bool application_equipment_gear_presentation_current(qa_application *,
    const application_equipment_gear_presentation *);

#endif

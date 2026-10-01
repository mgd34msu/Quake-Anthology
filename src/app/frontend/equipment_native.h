#ifndef QA_FRONTEND_EQUIPMENT_NATIVE_H
#define QA_FRONTEND_EQUIPMENT_NATIVE_H

#include "native_q3_client.h"
#include "equipment_source.h"

/* The actual native row invokes this factory after its media/backend exist.
 * Restoration uses the same empty owner after real dictionary adoption. */
bool frontend_equipment_native_compose(void *frontend, frontend_native_q3 *,
    frontend_native_q3_composition *, qa_error *);
bool frontend_equipment_native_weapon(const void *composition_context,
    qa_application_equipment_view *, bool *requested, qa_error *);

#endif

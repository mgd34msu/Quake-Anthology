#ifndef QA_APPLICATION_GUEST_Q3_EQUIPMENT_FUNCTIONS_H
#define QA_APPLICATION_GUEST_Q3_EQUIPMENT_FUNCTIONS_H

#include "guest_q3_equipment.h"

/* Complete CGAME-only callback inventory. The module owns no GAME input hooks.
 * Restore qualifies the untouched constructor against the full saved executor
 * before adopting IDs/state; the caller then imports executor RAM and host. */
bool application_q3_equipment_functions_checkpoint(qa_session *, qa_qvm *,
    application_q3_equipment *, qa_buffer *, qa_error *);
bool application_q3_equipment_functions_restore(qa_session *, qa_qvm *,
    application_q3_equipment *, qa_bytes continuation, qa_bytes executor, qa_error *);

#endif

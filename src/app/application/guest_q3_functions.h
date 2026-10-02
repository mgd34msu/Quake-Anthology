#ifndef QA_APPLICATION_GUEST_Q3_FUNCTIONS_H
#define QA_APPLICATION_GUEST_Q3_FUNCTIONS_H

#include "guest_q3_private.h"

/* Owns the complete actual input/weapons/combat/pickups/equipment/body callback inventory. Restore
 * qualifies the full saved executor, reconstructs all IDs once and adopts
 * every owner before the caller imports the actual executor RAM/host state. */
bool application_guest_q3_functions_checkpoint(q3g_role *, qa_buffer *, qa_error *);
bool application_guest_q3_functions_restore(q3g_role *, qa_bytes continuation,
    qa_bytes executor, qa_error *);

#endif

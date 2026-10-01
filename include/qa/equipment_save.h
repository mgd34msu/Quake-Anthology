#ifndef QA_EQUIPMENT_SAVE_H
#define QA_EQUIPMENT_SAVE_H

#include "qa/equipment.h"

bool qa_equipment_capture(qa_equipment *, qa_buffer *, qa_error *);
/* Empty isolated service after native source continuation is restored. Native
 * grapple and grenade state, combat and inventory retain their existing owners. */
bool qa_equipment_restore_bytes(qa_equipment *, qa_bytes, qa_error *);
/* Qualify the genuine selected native item groups after shared inventory
 * restore without publishing or replacing their source-owned groups. */
bool qa_equipment_reconnect(qa_equipment *, qa_error *);

#endif

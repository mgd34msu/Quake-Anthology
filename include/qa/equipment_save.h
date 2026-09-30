#ifndef QA_EQUIPMENT_SAVE_H
#define QA_EQUIPMENT_SAVE_H

#include "qa/equipment.h"

bool qa_equipment_capture(qa_equipment *, qa_buffer *, qa_error *);
/* Empty isolated service after native source continuation is restored. Native
 * grapple and grenade state, combat and inventory retain their existing owners. */
bool qa_equipment_restore_bytes(qa_equipment *, qa_bytes, qa_error *);
/* Validate imported catalogs after shared inventory restore without publishing
 * catalogs that were absent from the saved private continuation. */
bool qa_equipment_reconnect(qa_equipment *, qa_error *);
bool qa_equipment_inventory_owner(qa_equipment *, qa_actor_id, uint64_t saved_serial);
bool qa_equipment_inventory_group(qa_equipment *, qa_actor_id, uint64_t saved_serial,
    const qa_inventory_source_group *, qa_inventory_items *, qa_error *);

#endif

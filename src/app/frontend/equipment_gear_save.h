#ifndef QA_FRONTEND_EQUIPMENT_GEAR_SAVE_H
#define QA_FRONTEND_EQUIPMENT_GEAR_SAVE_H

#include "equipment_gear.h"

/* The genuine gear runtime roster and visual heaps precede topology import.
 * Real assets import/capture precedes each row's pure private continuation. */
bool frontend_equipment_gear_topology_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
bool frontend_equipment_gear_prepare_restored(qa_frontend *, qa_bytes, qa_error *);
bool frontend_equipment_gear_checkpoint(const qa_frontend *, size_t,
    const q3n_selected_media_refs *, qa_buffer *, qa_error *);
bool frontend_equipment_gear_restore(qa_frontend *, size_t,
    const q3n_selected_media_refs *, qa_bytes, qa_error *);
bool frontend_equipment_gear_topology_ready(const qa_frontend *, qa_error *);
bool frontend_equipment_gear_rebind_ready(const qa_frontend *, const qa_frontend *, qa_error *);
void frontend_equipment_gear_rebind(qa_frontend *, qa_frontend *);

#endif

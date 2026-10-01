#ifndef QA_FRONTEND_EQUIPMENT_Q3_SAVE_H
#define QA_FRONTEND_EQUIPMENT_Q3_SAVE_H
#include "equipment_q3.h"

/* Empty genuine registries are prepared over the imported visual heaps before
 * aggregate Q3 registry decoding. No registration, acquisition or parsing runs. */
bool frontend_equipment_q3_topology_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
bool frontend_equipment_q3_prepare_restored(qa_frontend *, qa_bytes, qa_error *);
/* The actual registry capture/import lease and shared content dictionaries
 * precede each physical row's private presenter/media continuation. */
bool frontend_equipment_q3_checkpoint(const qa_frontend *, size_t, qa_buffer *, qa_error *);
bool frontend_equipment_q3_restore(qa_frontend *, size_t, qa_bytes, qa_error *);
bool frontend_equipment_q3_topology_ready(const qa_frontend *, qa_error *);
bool frontend_equipment_q3_rebind_ready(const qa_frontend *, const qa_frontend *, qa_error *);
void frontend_equipment_q3_rebind(qa_frontend *, qa_frontend *);

#endif

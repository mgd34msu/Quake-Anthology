#ifndef QA_FRONTEND_SELECTED_EFFECTS_SAVE_H
#define QA_FRONTEND_SELECTED_EFFECTS_SAVE_H
#include "selected_effects.h"

/* Empty registries/banks/backends over genuine imported visual heaps and
 * tagged parent rows precede dictionary import. The dictionary attaches the
 * parent's actual map/frame/entity aliases before private continuation. */
bool frontend_selected_effects_topology_checkpoint(const qa_frontend *, qa_buffer *, qa_error *);
bool frontend_selected_effects_prepare_restored(qa_frontend *, qa_bytes, qa_error *);
bool frontend_selected_effects_checkpoint(const qa_frontend *, size_t, qa_buffer *, qa_error *);
bool frontend_selected_effects_restore(qa_frontend *, size_t, qa_bytes, qa_error *);
bool frontend_selected_effects_topology_ready(const qa_frontend *, qa_error *);
bool frontend_selected_effects_rebind_ready(const qa_frontend *, const qa_frontend *, qa_error *);
void frontend_selected_effects_rebind(qa_frontend *, qa_frontend *);
#endif

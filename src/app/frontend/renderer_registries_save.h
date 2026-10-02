#ifndef QA_FRONTEND_RENDERER_REGISTRIES_SAVE_H
#define QA_FRONTEND_RENDERER_REGISTRIES_SAVE_H
#include "q3_inventory.h"
/* Prefix creates or retains actual nominal registry allocations before raw
 * renderer cells resolve them. QFQ3 owns each numeric holder payload once. */
bool frontend_renderer_registries_checkpoint(qa_frontend *,frontend_q3_inventory *,
    frontend_world_inventory *,qa_buffer *,qa_error *);
bool frontend_renderer_registries_prepare_restored(qa_frontend *,frontend_q3_inventory *,
    frontend_world_inventory *,qa_bytes,qa_error *);
#endif

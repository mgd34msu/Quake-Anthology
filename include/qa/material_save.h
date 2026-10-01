#ifndef QA_MATERIAL_SAVE_H
#define QA_MATERIAL_SAVE_H
#include "qa/material.h"

typedef struct qa_material_checkpoint_refs {
    void *context;
    bool (*material_encode)(void *, const qa_material *, uint64_t *, qa_error *);
    bool (*material_decode)(void *, uint64_t, qa_material **, qa_error *);
} qa_material_checkpoint_refs;
/* The aggregate retains all contributing material libraries while this
 * renderer order is held. Nested read leases protect the complete shared
 * order across resolvers; admission, reordering and destruction reject them. */
bool qa_material_order_capture_begin(const qa_material_order *, qa_error *);
void qa_material_order_capture_end(const qa_material_order *);
bool qa_material_order_idle(const qa_material_order *);
/* Reference keys identify actual records in the enclosing library inventory.
 * Restore requires detached records whose order_entry is NULL. It installs
 * their entries only after the complete renderer order has been validated.
 * The aggregate holds the actual detached library owners during import. */
bool qa_material_order_checkpoint(const qa_material_order *, const qa_material_checkpoint_refs *,
                                   qa_buffer *, qa_error *);
bool qa_material_order_restore(qa_bytes, const qa_material_checkpoint_refs *,
                                qa_material_order **, qa_error *);
#endif

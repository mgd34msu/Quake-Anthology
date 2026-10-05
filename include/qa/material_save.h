#ifndef QA_MATERIAL_SAVE_H
#define QA_MATERIAL_SAVE_H
#include "qa/material.h"

/* The aggregate retains all contributing material libraries while this
 * renderer order is held. Nested read leases protect the complete shared
 * order across resolvers; admission, reordering and destruction reject them. */
bool qa_material_order_capture_begin(const qa_material_order *, qa_error *);
void qa_material_order_capture_end(const qa_material_order *);
bool qa_material_order_idle(const qa_material_order *);
#endif

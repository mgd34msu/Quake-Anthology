#ifndef QA_FRONTEND_EQUIPMENT_HELD_STOCK_H
#define QA_FRONTEND_EQUIPMENT_HELD_STOCK_H

#include "equipment_held.h"
#include "qa/builtin.h"

/* Original source declarations only. Unknown models leave found=false;
 * the caller then requires the actual authored .held.json declaration. */
bool frontend_held_stock(qa_game_family, const char *view_model, const char *item,
    frontend_held_declaration *, bool *found, qa_error *);
/* Stock Q2 grips require the canonical path, byte length and parsed MD2 layout. */
bool frontend_held_stock_q2_grip(const char *source_path, const qa_resource *, const qa_model *,
    qa_model_transform *, bool *found, qa_error *);

#endif

#ifndef QA_FRONTEND_EQUIPMENT_HELD_STOCK_H
#define QA_FRONTEND_EQUIPMENT_HELD_STOCK_H

#include "equipment_held.h"
#include "qa/builtin.h"

/* Original source declarations only. Unknown models leave found=false;
 * the caller then requires the actual authored .held.json declaration. */
bool frontend_held_stock(qa_game_family, const char *view_model, const char *item,
    frontend_held_declaration *, bool *found, qa_error *);
/* Only stock Q2 metadata uses this exact admitted mesh-digest override. */
bool frontend_held_stock_q2_grip(const qa_resource *, qa_model_transform *, bool *found, qa_error *);

#endif

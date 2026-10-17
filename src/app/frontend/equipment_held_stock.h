#ifndef QA_FRONTEND_EQUIPMENT_HELD_STOCK_H
#define QA_FRONTEND_EQUIPMENT_HELD_STOCK_H

#include "equipment_held.h"
#include "qa/builtin.h"

/* Original source declarations only. Unknown models leave found=false;
 * the caller then requires the actual authored .held.json declaration. */
bool frontend_held_stock(qa_game_family, qa_ruleset_id, const char *view_model, const char *item,
    frontend_held_declaration *, bool *found, qa_error *);
/* Canonical carrier names supply the reference grip; animation uses the parsed geometry. */
bool frontend_held_stock_q2_grip(const char *source_path, qa_ruleset_id, const qa_resource *, const qa_model *,
    qa_model_transform *, bool *found, qa_error *);
bool frontend_held_stock_q2_attachment(const char *path,qa_ruleset_id,qa_scene_model *,qa_error *);

#endif

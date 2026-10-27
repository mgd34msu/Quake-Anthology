/* Port of src/content/q2/base/monsters/floater.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_BASE_MONSTERS_FLOATER_H
#define QA_CONTENT_Q2_BASE_MONSTERS_FLOATER_H
#include "qa/content/q2/foundation/monsters/types.h"
/* TS floaterDefinition (moves are floatMoves minus floater_move_activate,
 * filtered at first use). */
const qa_q2m_definition *qa_q2bm_get_floater_definition(void);
const qa_q2m_move *qa_q2bm_floater_active_moves(size_t *out_count);
#endif

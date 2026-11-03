/* Port of src/content/composition/q2-q3-items.ts: Quake III items offered to
 * a Quake II primary. GPL-2.0-or-later. */
#ifndef QA_CONTENT_COMPOSITION_Q2_Q3_ITEMS_H
#define QA_CONTENT_COMPOSITION_Q2_Q3_ITEMS_H
#include "qa/content/q2/foundation/items.h"

/* TS q3ForeignItemDefinitions: Q3-authored items as Q2 definitions, for
 * qa_q2_item_hooks.foreign_items (ctx unused). Grants land in Q2 destinations
 * through the shared Q3-to-Q2 supply profile; the Q2 module keeps its native
 * definition where a classname collides. The pickups keep `services`, which
 * the item module owns. */
qa_q2_foreign_item_list qa_composition_q3_foreign_item_definitions(void *ctx, const qa_q2_foreign_item_services *services);
#endif

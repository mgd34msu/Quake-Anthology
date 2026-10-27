/* Port of src/content/q1/addons/items/index.ts (registerMg3Items +
 * player-extension closures). GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_ITEMS_INDEX_H
#define QA_CONTENT_Q1_ADDONS_ITEMS_INDEX_H
#include "qa/content/q1/addons/context.h"
void qa_q1ad_register_mg3_items(qa_q1ad_context *context);
void qa_q1ad_mg3_items_attach(qa_q1ad_context *context, qa_q1ad_player *player);
void qa_q1ad_mg3_items_frame(qa_q1ad_context *context, qa_q1ad_player *player);
#endif

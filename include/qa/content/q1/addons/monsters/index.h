/* Port of src/content/q1/addons/monsters/index.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_INDEX_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_INDEX_H

#include "qa/content/q1/addons/context.h"

/* Native maps and selected foreign rosters install the same named source controllers. */
void qa_q1am_register_addon_monsters(qa_q1ad_context *context);
/* C ownership (TS GC): frees every per-game registration this module made
 * for the game (controllers, startup names, navigation). Call after the
 * last use of the game, before its actor registry is destroyed. */
void qa_q1am_release_addon_monsters(qa_q1_services *game);

#endif

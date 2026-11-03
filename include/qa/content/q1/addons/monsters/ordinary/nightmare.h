/* Port of src/content/q1/addons/monsters/ordinary/nightmare.ts.
 * quakec_mg3 monsters/{shambler,ogre,shalrath}.qc skill and Bloody Nightmare behavior.
 * GPL-2.0-or-later. mg3NightmareFrames is composed into the generated ordinary source
 * tables (ordinary/source_frames.h). */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_NIGHTMARE_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_NIGHTMARE_H

#include "qa/content/q1/addons/context.h"

/* TS mg3NightmareActions. */
extern const qa_q1_monster_action_entry qa_q1am_mg3_nightmare_actions[];
extern const size_t qa_q1am_mg3_nightmare_action_count;
void qa_q1am_register_mg3_nightmare(qa_q1ad_context *context);

#endif

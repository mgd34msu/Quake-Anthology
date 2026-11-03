/* Port of src/content/q1/addons/monsters/ordinary/rocket-ogre.ts.
 * quakec_mg3/monsters/ogre.qc rocket variant. GPL-2.0-or-later. rocketOgreFrames is
 * composed into the generated ordinary source tables (ordinary/source_frames.h). */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_ROCKET_OGRE_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_ROCKET_OGRE_H

#include "qa/content/q1/addons/context.h"

void qa_q1am_rocket_ogre_frame(qa_q1_base_monster *monster, const char *name);
void qa_q1am_register_rocket_ogre(qa_q1ad_context *context);

#endif

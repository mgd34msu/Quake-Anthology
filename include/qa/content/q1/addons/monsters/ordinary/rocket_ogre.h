/* Port of src/content/q1/addons/monsters/ordinary/rocket-ogre.ts.
 * rocketOgreFrames is built lazily from the mirrored base frame
 * lookup (game->base_frame) on first use. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_ROCKET_OGRE_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_ORDINARY_ROCKET_OGRE_H

#include "qa/content/q1/addons/monsters/ai/controller.h"

const qa_q1am_frame_table *qa_q1am_rocket_ogre_frames(qa_q1am_game *game);
void qa_q1am_rocket_ogre_frame(qa_q1am_monster *monster, const char *name);
void qa_q1am_register_rocket_ogre(qa_q1am_context *context);

#endif

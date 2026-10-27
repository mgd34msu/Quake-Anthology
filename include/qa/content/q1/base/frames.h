/* Port of src/content/q1/base/frames.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_BASE_FRAMES_H
#define QA_CONTENT_Q1_BASE_FRAMES_H

#include <stdbool.h>

#include "qa/content/q1/base/animation.h"

/* TS monsterFrames (ReadonlyMap). */
extern const qa_q1_monster_frame_table QA_Q1_MONSTER_FRAMES;
const qa_q1_monster_frame *qa_q1_monster_frames_get(const char *name);
bool qa_q1_monster_frames_has(const char *name);

#endif

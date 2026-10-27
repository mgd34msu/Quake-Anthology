/* Port of src/content/q1/addons/monsters/heavy/index.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_INDEX_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_HEAVY_INDEX_H

#include "qa/content/q1/addons/monsters/heavy/runtime.h"

/* Runtime storage is caller-owned; the lava-man definition storage
 * lives in heavy/index.c static storage. */
qa_q1am_mg3_heavy *qa_q1am_register_mg3_heavy(qa_q1am_context *context, qa_q1am_mg3_heavy *runtime);

#endif

/* Port of src/content/q1/addons/monsters/index.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_MONSTERS_INDEX_H
#define QA_CONTENT_Q1_ADDONS_MONSTERS_INDEX_H

#include "qa/content/q1/addons/monsters/ai/controller.h"
#include "qa/content/q1/addons/monsters/heavy/runtime.h"

/* Heavy runtime storage is caller-owned (only used for "mg3"). */
void qa_q1am_register_addon_monsters(qa_q1am_context *context, qa_q1am_mg3_heavy *heavy_runtime);

#endif

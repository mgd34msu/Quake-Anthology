/* Port of src/bots/behavior/rerelease/data/knowledge-q1.ts. GPL-2.0-or-later. */
#ifndef QA_BOTS_BEHAVIOR_RERELEASE_DATA_KNOWLEDGE_Q1_H
#define QA_BOTS_BEHAVIOR_RERELEASE_DATA_KNOWLEDGE_Q1_H
#include <stdbool.h>
#include "qa/bots/behavior/assets.h"
#include "qa/bots/behavior/rerelease/data/knowledge.h"
/* False (out untouched) = TS null when weapons.txt missing; missing settings aborts. */
bool qa_load_quake1_knowledge(qa_bot_knowledge *out, qa_bot_asset_files *files);
#endif

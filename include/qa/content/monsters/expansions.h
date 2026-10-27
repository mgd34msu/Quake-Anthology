/* Port of src/content/monsters/expansions.ts (const data).
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_MONSTERS_EXPANSIONS_H
#define QA_CONTENT_MONSTERS_EXPANSIONS_H

#include <stddef.h>

#include "qa/content/monsters/definitions.h"

/* TS q1ExpansionMonsterSources / q1AddonMonsterSources. */
extern const qa_monster_source_definition qa_q1_expansion_monster_sources[];
extern const size_t qa_q1_expansion_monster_source_count;
extern const qa_monster_source_definition qa_q1_addon_monster_sources[];
extern const size_t qa_q1_addon_monster_source_count;

#endif

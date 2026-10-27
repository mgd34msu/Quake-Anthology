/* Port of src/content/monsters/q2-expansions.ts (const data).
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_MONSTERS_Q2_EXPANSIONS_H
#define QA_CONTENT_MONSTERS_Q2_EXPANSIONS_H

#include <stddef.h>

#include "qa/content/monsters/definitions.h"

/* TS q2ExpandedClassicCreatures / q2ExpandedBaseCreatures. */
extern const qa_monster_creature_entry qa_q2_expanded_classic_creatures[];
extern const size_t qa_q2_expanded_classic_creature_count;
extern const qa_monster_creature_entry qa_q2_expanded_base_creatures[];
extern const size_t qa_q2_expanded_base_creature_count;

/* TS q2ExpansionSources. */
extern const qa_monster_source_definition qa_q2_expansion_sources[];
extern const size_t qa_q2_expansion_source_count;

#endif

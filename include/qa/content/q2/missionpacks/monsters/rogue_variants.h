/* Port of src/content/q2/missionpacks/monsters/rogue-variants.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_VARIANTS_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_ROGUE_VARIANTS_H

#include <stddef.h>

#include "qa/content/q2/base/monsters/makron.h"
#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/types.h"
#include "qa/content/q2/missionpacks/monsters/state.h"

/* TS owners: src/content/q2/base/monsters/makron.ts
 * makronDefinition/withMakronSpawnCallbacks and jorg.ts
 * createJorgDefinition, via the landed base ports. The with-makron
 * helper copies *definition to *out with think spawn callbacks
 * appended; think storage needs room for think_count + 2. */
/* The makron mirror is the landed base definition object itself. */
#define qa_q2mpm_base_makron_definition qa_q2bm_makron_definition
qa_q2m_definition qa_q2mpm_base_create_jorg_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters);
void qa_q2mpm_with_makron_spawn_callbacks(qa_q2m_definition *out, const qa_q2m_definition *definition,
                                          qa_q2m_monsters *monsters, qa_q2_named_think *think_storage,
                                          qa_q2_named_use *use_storage);

/* Writes 11 definitions (3 soldiers, infantry, brain, floater,
 * gladiator, boss2, supertank, jorg, makron) to out; returns 11. */
size_t qa_q2mpm_create_rogue_base_variants(qa_q2mpm_arena *arena, qa_q2m_definition *out, qa_q2m_monsters *monsters,
                                           qa_q2mpm_monster_state *source);

#endif

/* Port of src/content/q2/rerelease/monsters/base-variants/medic.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_BASE_VARIANTS_MEDIC_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_BASE_VARIANTS_MEDIC_H
#include <stddef.h>
#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/content/q2/missionpacks/monsters/types.h"

/* TS rereleaseMedicReinforcements entry. classname is malloc'd. */
typedef struct qa_q2rm_medic_reinforcement_choice {
  char *classname;
  double strength;
} qa_q2rm_medic_reinforcement_choice;
/* Malloc'd array (NULL when empty); free with
 * qa_q2rm_medic_reinforcements_free. */
size_t qa_q2rm_rerelease_medic_reinforcements(const qa_q2f_spawn_fields *fields,
                                              qa_q2rm_medic_reinforcement_choice **out);
void qa_q2rm_medic_reinforcements_free(qa_q2rm_medic_reinforcement_choice *entries, size_t count);
/* TS createRereleaseMedicDefinitions: writes 2 definitions. */
size_t qa_q2rm_create_rerelease_medic_definitions(qa_q2m_monsters *monsters, qa_q2mpm_weapons weapons,
                                                  qa_q2mpm_monster_state *source, qa_q2m_definition out[2]);
#endif

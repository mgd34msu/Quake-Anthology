/* Port of src/content/q2/rerelease/monsters/beam.ts.
 * GPL-2.0-or-later. The TS "beam" presentation event rides the
 * landed qa_q2mpm beam sink (missionpacks/monsters/types.h). */
#ifndef QA_CONTENT_Q2_RERELEASE_MONSTERS_BEAM_H
#define QA_CONTENT_Q2_RERELEASE_MONSTERS_BEAM_H
#include "qa/content/q2/foundation/monsters/types.h"

void qa_q2rm_update_monster_beam(qa_q2f_entity *beam, qa_q2f_game *game, bool damage);
/* TS freeMonsterBeam (Q2Think). */
void qa_q2rm_free_monster_beam_fn(void *ctx, qa_q2f_entity *beam, qa_q2f_game *game);
qa_q2f_think qa_q2rm_free_monster_beam(void);
void qa_q2rm_fire_monster_beam(qa_q2m_context *context, double damage, bool secondary, qa_q2f_think update);
#endif

/* Port of src/content/q2/missionpacks/monsters/stalker.ts.
 * GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_STALKER_H
#define QA_CONTENT_Q2_MISSIONPACKS_MONSTERS_STALKER_H

#include "qa/content/q2/foundation/monsters/index.h"
#include "qa/content/q2/missionpacks/monsters/state.h"
#include "qa/content/q2/missionpacks/monsters/types.h"

bool qa_q2mpm_stalker_transition_ok(qa_q2m_context *context, bool spawned_by_widow);
/* TS readonly [number, number] written to out[0..1]. */
void qa_q2mpm_stalker_jump_angles(qa_vec3 start, qa_vec3 end, double velocity, double out[2]);
/* gravity is the TS () => number closure as fn+ctx. */
typedef struct qa_q2mpm_gravity_fn {
  void *ctx;
  double (*fn)(void *ctx);
} qa_q2mpm_gravity_fn;
qa_q2m_definition qa_q2mpm_create_stalker_definition(qa_q2mpm_arena *arena, qa_q2m_monsters *monsters, qa_q2mpm_weapons weapons,
                                                     qa_q2mpm_monster_state *source, qa_q2mpm_gravity_fn gravity);

#endif

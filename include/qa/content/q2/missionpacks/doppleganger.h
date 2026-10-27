/* Port of src/content/q2/missionpacks/doppleganger.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_DOPPLEGANGER_H
#define QA_CONTENT_Q2_MISSIONPACKS_DOPPLEGANGER_H
#include "qa/content/q2/foundation/weapons/player.h"
#include "qa/content/q2/missionpacks/spheres.h"

/* TS Q2MissionPackDoppleganger. cb_think leads with the landed
 * rogueSpawnCallbacks think entries (monsters/spawn.ts). */
typedef struct qa_q2mp_doppleganger {
  qa_q2mp_spheres *spheres;
  qa_q2w_weapons *weapons;
  qa_q2f_named_think cb_think[8];
  size_t cb_think_count;
  qa_q2f_named_pain cb_pain[1];
  qa_q2f_named_die cb_die[1];
  qa_q2f_think timeout, body_think;
  qa_q2f_pain pain;
  qa_q2f_die die;
} qa_q2mp_doppleganger;

void qa_q2mp_doppleganger_init(qa_q2mp_doppleganger *doppleganger, qa_q2mp_spheres *spheres, qa_q2w_weapons *weapons);
qa_q2f_callback_definitions qa_q2mp_doppleganger_callbacks(qa_q2mp_doppleganger *doppleganger);
bool qa_q2mp_doppleganger_use(qa_q2mp_doppleganger *d, qa_q2f_entity *owner, qa_q2f_game *game);
qa_q2f_entity *qa_q2mp_doppleganger_fire(qa_q2mp_doppleganger *d, qa_q2f_entity *owner, qa_q2f_game *game,
                                         qa_vec3 start, qa_vec3 direction);
#endif

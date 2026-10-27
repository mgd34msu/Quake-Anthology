/* Port of src/content/q2/missionpacks/entities/movers.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_MOVERS_H
#define QA_CONTENT_Q2_MISSIONPACKS_ENTITIES_MOVERS_H
#include "qa/content/q2/missionpacks/entities/types.h"

/* TS platformState phase: "top"|"bottom"|"up"|"down". */
typedef enum qa_q2mp_platform_phase {
  QA_Q2MP_PLATFORM_TOP, QA_Q2MP_PLATFORM_BOTTOM, QA_Q2MP_PLATFORM_UP, QA_Q2MP_PLATFORM_DOWN
} qa_q2mp_platform_phase;
const char *qa_q2mp_platform_phase_name(qa_q2mp_platform_phase phase);
typedef struct qa_q2mp_platform_state { qa_vec3 top, bottom; qa_q2mp_platform_phase phase; } qa_q2mp_platform_state;

/* TS Q2RogueMovers. */
typedef struct qa_q2mp_rogue_movers {
  qa_q2mp_entity_hooks hooks;
  qa_q2f_named_think cb_think[12];
  qa_q2f_named_use cb_use[4];
  qa_q2f_named_touch cb_touch[2];
  qa_q2f_named_die cb_die[1];
  qa_q2f_named_blocked cb_blocked[2];
  qa_q2f_think up, down, top, bottom, secret1, secret2, secret3, secret4, secret5, secret6, secret_done, force_think;
  qa_q2f_use plat_use, activate, secret_use, force_use;
  qa_q2f_touch plat_touch, secret_touch;
  qa_q2f_die secret_die;
  qa_q2f_blocked plat_blocked, secret_blocked;
} qa_q2mp_rogue_movers;

void qa_q2mp_rogue_movers_init(qa_q2mp_rogue_movers *movers, qa_q2mp_entity_hooks hooks);
qa_q2f_callback_definitions qa_q2mp_rogue_movers_callbacks(qa_q2mp_rogue_movers *movers);
/* False return is TS null. */
bool qa_q2mp_rogue_movers_platform_state(qa_q2mp_rogue_movers *m, qa_q2f_entity *entity, qa_q2mp_platform_state *out);
bool qa_q2mp_rogue_movers_spawn(qa_q2mp_rogue_movers *movers, qa_q2f_entity *entity, qa_q2f_game *game);
#endif

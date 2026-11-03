/* Port of src/content/q1/missionpacks/travel.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_MISSIONPACKS_TRAVEL_H
#define QA_CONTENT_Q1_MISSIONPACKS_TRAVEL_H
#include "qa/content/q1/base/travel.h"
#include "qa/content/q1/missionpacks/types.h"

/* Every returned state owns its lists (qa_q1_travel_state_destroy); where
 * TS returns the input state itself, C returns an owned copy of it. */
qa_q1_travel_state qa_q1mp_new_mission_pack_travel(qa_q1_services *game, qa_q1mp_pack pack);
qa_q1_travel_state qa_q1mp_capture_mission_pack_travel(qa_q1_services *game, qa_owned_actor actor, qa_q1mp_pack pack);
qa_q1_travel_state qa_q1mp_decode_mission_pack_travel(qa_q1_services *game, const qa_q1_travel_state *state,
                                                      double server_flags, qa_q1mp_pack pack);
void qa_q1mp_admit_mission_pack_travel(qa_q1_services *game, qa_owned_actor actor, const qa_q1_travel_state *state,
                                       qa_q1mp_pack pack);

#endif

/* Port of src/content/q1/addons/travel.ts (quakec_mg1/client.qc and
 * quakec_mg3/client.qc source level parameters). GPL-2.0-or-later.
 * Every returned qa_q1_travel_state is owned by the caller
 * (qa_q1_travel_state_destroy): where TS returns the input state itself or
 * shares its extensions array, C returns a deep copy. */
#ifndef QA_CONTENT_Q1_ADDONS_TRAVEL_H
#define QA_CONTENT_Q1_ADDONS_TRAVEL_H
#include "qa/content/q1/addons/context.h"
#include "qa/content/q1/base/travel.h"
qa_q1_travel_state qa_q1ad_new_travel(qa_q1ad_context *context);
qa_q1_travel_state qa_q1ad_capture_travel(qa_q1ad_context *context, qa_owned_actor actor);
qa_q1_travel_state qa_q1ad_decode_travel(qa_q1ad_context *context, const qa_q1_travel_state *state);
/** The existing admission applies shared inventory, then source travel extensions restore addon words. */
void qa_q1ad_admit_travel(qa_q1ad_context *context, qa_owned_actor actor, const qa_q1_travel_state *state);
#endif

/* Port of src/content/q1/addons/ctf/travel.ts. Every returned travel state
 * is caller-owned (qa_q1_travel_state_destroy); where TS returns the input
 * or shares its extensions, C deep-copies. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_TRAVEL_H
#define QA_CONTENT_Q1_ADDONS_CTF_TRAVEL_H
#include "qa/content/q1/addons/ctf/state.h"
#include "qa/content/q1/base/travel.h"
qa_q1_travel_state qa_q1ad_new_q1_ctf_travel(qa_q1ad_ctf_state *state);
qa_q1_travel_state qa_q1ad_capture_q1_ctf_travel(qa_q1ad_ctf_state *state, qa_owned_actor actor);
qa_q1_travel_state qa_q1ad_decode_q1_ctf_travel(qa_q1ad_ctf_state *state, const qa_q1_travel_state *travel);
/* TS captureTravel: checkpoint-value bytes (malloc'd; NULL on OOM). */
uint8_t *qa_q1ad_ctf_capture_travel(qa_q1ad_ctf_state *state, qa_actor_id actor, size_t *out_length);
void qa_q1ad_ctf_restore_travel(qa_q1ad_ctf_state *state, qa_actor_id actor, const uint8_t *bytes, size_t length);
#endif

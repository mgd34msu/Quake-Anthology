/* Port of src/content/q1/addons/ctf/travel.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_TRAVEL_H
#define QA_CONTENT_Q1_ADDONS_CTF_TRAVEL_H
#include "qa/content/q1/addons/ctf/state.h"
#include "qa/content/q1/addons/travel.h"
qa_q1ad_travel qa_q1ad_new_ctf_travel(const qa_q1ad_base_travel *base, qa_q1ad_ctf_state *state);
qa_q1ad_travel qa_q1ad_capture_q1_ctf_travel(const qa_q1ad_base_travel *base, qa_q1ad_ctf_state *state, qa_owned_actor actor);
qa_q1ad_travel qa_q1ad_decode_ctf_travel(const qa_q1ad_base_travel *base, qa_q1ad_ctf_state *state, const qa_q1ad_travel *travel);
uint8_t *qa_q1ad_ctf_capture_travel(qa_q1ad_ctf_state *state, qa_actor_id actor, size_t *out_length);
void qa_q1ad_ctf_restore_travel(qa_q1ad_ctf_state *state, qa_actor_id actor, const uint8_t *bytes, size_t length);
#endif

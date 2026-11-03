/* Port of src/content/q1/addons/ctf/maps.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_MAPS_H
#define QA_CONTENT_Q1_ADDONS_CTF_MAPS_H
#include "qa/content/q1/addons/ctf/state.h"
void qa_q1ad_ctf_vote_touch(qa_q1ad_ctf_state *state, qa_q1_actor *trigger, qa_actor_id actor);
void qa_q1ad_ctf_map_frame(qa_q1ad_ctf_state *state);
void qa_q1ad_ctf_register_maps(qa_q1ad_ctf_state *state);
#endif

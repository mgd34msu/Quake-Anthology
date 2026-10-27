/* Port of src/content/q1/addons/ctf/observer.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_OBSERVER_H
#define QA_CONTENT_Q1_ADDONS_CTF_OBSERVER_H
#include "qa/content/q1/addons/ctf/state.h"
void qa_q1ad_ctf_become_observer(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_observer_frame(qa_q1ad_ctf_state *state, qa_actor_id actor);
#endif

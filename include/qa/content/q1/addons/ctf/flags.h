/* Port of src/content/q1/addons/ctf/flags.ts. GPL-2.0-or-later. */
#ifndef QA_CONTENT_Q1_ADDONS_CTF_FLAGS_H
#define QA_CONTENT_Q1_ADDONS_CTF_FLAGS_H
#include "qa/content/q1/addons/ctf/state.h"
/* TS returnFlag(state, flag, announce = true). */
void qa_q1ad_ctf_return_flag(qa_q1ad_ctf_state *state, qa_q1_actor *flag, bool announce);
void qa_q1ad_ctf_drop_flag(qa_q1ad_ctf_state *state, qa_actor_id actor);
void qa_q1ad_ctf_touch_flag(qa_q1ad_ctf_state *state, qa_q1_actor *flag, qa_actor_id actor);
void qa_q1ad_ctf_register_flags(qa_q1ad_ctf_state *state);
#endif

#ifndef QA_Q2_HINTS_INTERNAL_H
#define QA_Q2_HINTS_INTERNAL_H

#include "../internal.h"

struct q2m_context;
bool q2_hint_spawn(qa_q2_game *, q2_actor *, bool *, qa_error *);
bool q2_hints_init(qa_q2_game *, qa_error *);
bool q2_hint_touch(qa_q2_game *, q2_actor *, qa_actor_id, qa_error *);
bool q2_hint_run(struct q2m_context *, float, bool *, qa_error *);
bool q2_hint_check_lost(struct q2m_context *, bool *, qa_error *);
bool q2_hint_stop(struct q2m_context *, qa_error *);

#endif

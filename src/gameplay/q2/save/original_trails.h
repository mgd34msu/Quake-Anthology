#ifndef QA_Q2_ORIGINAL_TRAILS_H
#define QA_Q2_ORIGINAL_TRAILS_H
#include "original_internal.h"

bool q2_original_trail_record(qa_q2_game *, q2_original_record_io *, q2_actor *,
                              bool *handled);
bool q2_original_trail_client(qa_q2_game *, q2_original_record_io *, qa_actor_id);

#endif

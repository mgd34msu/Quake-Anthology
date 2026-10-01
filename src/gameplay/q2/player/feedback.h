#ifndef QA_Q2_PLAYER_FEEDBACK_H
#define QA_Q2_PLAYER_FEEDBACK_H

#include "../internal.h"

void q2_player_feedback_begin(qa_q2_game *);
bool q2_player_frame_begin(qa_q2_game *, qa_error *);
bool q2_player_end_server_frames(qa_q2_game *, qa_error *);

#endif

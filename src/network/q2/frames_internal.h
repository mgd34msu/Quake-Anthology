#ifndef QA_Q2_FRAMES_INTERNAL_H
#define QA_Q2_FRAMES_INTERNAL_H
#include "qa/network_q2_messages.h"
struct qa_q2_frame_history {
    qa_q2_wire_frame *frames;
    bool *present;
    size_t capacity, next;
};
const qa_q2_wire_frame *qa_q2_frame_history_store_owned(qa_q2_frame_history *, qa_q2_wire_frame *);
#endif

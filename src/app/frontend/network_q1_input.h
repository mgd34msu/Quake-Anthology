#ifndef QA_FRONTEND_NETWORK_Q1_INPUT_H
#define QA_FRONTEND_NETWORK_Q1_INPUT_H
#include "qa/frontend.h"
#include "qa/input.h"
bool frontend_network_q1_input(qa_frontend *,uint32_t physical_seat,
    const qa_seat_input_sample *,uint64_t sequence,double source_frame_ms,bool *handled,qa_error *);
bool frontend_network_q1_input_owned(const qa_frontend *,uint32_t physical_seat);
#endif

#ifndef QA_FRONTEND_NETWORK_Q2_INPUT_H
#define QA_FRONTEND_NETWORK_Q2_INPUT_H
#include "qa/frontend.h"
#include "qa/input.h"
/* Ownership is distinct from command readiness while the real remote CLIENT
 * is connecting or loading. A handled sample never enters local GAME input. */
bool frontend_network_q2_input(qa_frontend *, uint32_t physical_seat,
    const qa_seat_input_sample *, uint64_t sequence, bool *handled, qa_error *);
bool frontend_network_q2_input_owned(const qa_frontend *,uint32_t physical_seat);
#endif

#ifndef QA_NETWORK_Q3_ROUND_H
#define QA_NETWORK_Q3_ROUND_H
#include "qa/network_q3.h"

/* An immutable observation of one actual retained transport client. The cut
 * owns userinfo; its lifetime spans source actor retirement and readmission.
 * previous_actor identifies the old source admission, not a replacement actor.
 * No source callbacks, pointers or this operation scope are save records. */
typedef struct qa_network_q3_round_client {
    qa_net_client_id client;
    qa_net_seat_id seat;
    uint32_t source_slot;
    qa_actor_id previous_actor;
    const char *userinfo;
    qa_q3_usercmd last_command;
} qa_network_q3_round_client;
typedef struct qa_network_q3_round qa_network_q3_round;

#endif

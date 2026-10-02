#ifndef QA_APPLICATION_Q3_ARSENAL_CLIENT_H
#define QA_APPLICATION_Q3_ARSENAL_CLIENT_H

#include "qa/application_q3_client.h"

typedef struct qa_application_q3_arsenal_client {
    qa_actor_id actor;
    qa_application_q3_client_host client;
} qa_application_q3_arsenal_client;

/* Borrows the selected original GAME's admitted CG companion for this actual
 * player and launch seat. The selected HUD CG is excluded because its normal
 * presentation already draws it. No clocks or source calls occur here. */
bool qa_application_q3_arsenal_client_read(qa_application *, qa_actor_id,
    uint32_t seat, qa_application_q3_arsenal_client *, bool *present, qa_error *);
bool qa_application_q3_arsenal_client_current(qa_application *,
    const qa_application_q3_arsenal_client *);

/* Enters the genuine CG Draw at a returned presentation boundary using its
 * actual GAME clock. The frontend must hold the receipt's exact source lease
 * and route the companion's scene output to its private completed packet. */
bool qa_application_q3_arsenal_client_draw(qa_application *,
    const qa_application_q3_arsenal_client *, qa_error *);

#endif

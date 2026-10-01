#ifndef QA_APPLICATION_NATIVE_Q3_CONTROL_H
#define QA_APPLICATION_NATIVE_Q3_CONTROL_H

#include "internal.h"
#include "qa/game_q3_wire.h"

/* ClientSpawn, asynchronous intake and G_RunClient share this source turn.
 * The command is already retained by its genuine source receiver. */
bool application_control_q3_client_think(application_provider *, qa_actor_id,
    const qa_q3_usercmd *, qa_error *);

#endif

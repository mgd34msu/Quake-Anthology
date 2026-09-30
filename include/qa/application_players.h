#ifndef QA_APPLICATION_PLAYERS_H
#define QA_APPLICATION_PLAYERS_H

#include "qa/application.h"
#include "qa/network.h"

typedef struct qa_application_remote_player_request {
    qa_net_client_id client;
    qa_net_seat_id seat;
    uint32_t application_seat, source_slot; /* UINT32_MAX requests source admission. */
    const char *userinfo, *name, *team, *skin;
    bool spectator, bot, defer_source_begin;
} qa_application_remote_player_request;
/* Admission uses the same roster, role bindings, native/guest player initialization
 * and shared controls as local seats. A callback failure after mutation faults
 * the application; it is never presented as an allocating rollback. */
bool qa_application_remote_player_attach(qa_application *, const qa_application_remote_player_request *,
                                          qa_actor_id *, qa_error *);
bool qa_application_remote_player_actor(const qa_application *, qa_net_client_id, qa_net_seat_id,
                                         qa_actor_id *);
bool qa_application_remote_player_begin(qa_application *, qa_net_client_id, qa_net_seat_id, qa_error *);
bool qa_application_remote_player_detach(qa_application *, qa_net_client_id, qa_net_seat_id, qa_error *);
bool qa_application_player_seat(const qa_application *, qa_actor_id, uint32_t *);

#endif

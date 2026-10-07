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
/* The local seat must already exist in the current launch metadata. Admission
 * and retirement share the same real Source slot, role and actor lifecycle as
 * remote players, without a network peer or a map replacement. */
bool qa_application_local_player_available(qa_application *, uint32_t logical_seat, bool *, qa_error *);
bool qa_application_local_player_attach(qa_application *, uint32_t logical_seat,
    const char *userinfo, const char *skin, qa_actor_id *, qa_error *);
bool qa_application_local_player_detach(qa_application *, uint32_t logical_seat, qa_error *);
/* After the removed physical services are destroyed and launch metadata is
 * published, release only the original CLIENT rows for omitted seats. */
bool qa_application_local_player_clients_retire(qa_application *, qa_error *);
bool qa_application_player_seat(const qa_application *, qa_actor_id, uint32_t *);
/* Borrow the actual selected CHARACTER player identity, including its current skin. */
bool qa_application_player_info_read(qa_application *, qa_actor_id, qa_builtin_player_info *);

#endif

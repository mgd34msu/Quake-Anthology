#ifndef QA_NETWORK_LOCAL_H
#define QA_NETWORK_LOCAL_H
#include "qa/network_runtime.h"
typedef struct qa_network_local_player {
    qa_actor_id actor;
    qa_actor_owner source_owner;
    uint32_t source_slot;
} qa_network_local_player;
typedef struct qa_network_local_hooks {
    void *context;
    bool (*player)(void *,qa_net_seat_id,qa_network_local_player *,qa_error *);
} qa_network_local_hooks;
/* One human local Source seat is one canonical connection. The existing
 * local input path keeps applying commands directly to that same player. */
bool qa_network_attach_local(qa_network_runtime *,const qa_net_connect *,
    const qa_network_local_hooks *,uint64_t,qa_net_client_id *,qa_error *);
bool qa_network_local_player_read(const qa_network_runtime *,qa_net_client_id,
    qa_network_local_player *,qa_error *);
#endif

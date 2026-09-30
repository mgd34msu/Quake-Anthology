#ifndef QA_APPLICATION_NETWORK_H
#define QA_APPLICATION_NETWORK_H
#include "qa/application.h"
#include "qa/application_players.h"
#include "qa/network_runtime.h"

#define QA_NETWORK_COMMAND_OWNER UINT64_C(0x71616e6574770001)
bool qa_application_network_command_owner_bound(const qa_application *);

/* These adapters borrow the application. Seat authority is always the
 * canonical roster, and travel resolves its freshly published actor IDs. */
bool qa_application_network_controlled(qa_application *, qa_net_client_id,
    qa_net_seat_id, qa_actor_id, qa_movement_kind, qa_bytes arsenal, qa_error *);
bool qa_application_network_command(qa_application *, const qa_network_command *, qa_error *);
bool qa_application_network_resolve(qa_application *, const qa_net_client *,
    uint32_t actor_slot, uint32_t actor_generation, qa_net_seat_id *,
    qa_unified_controlled_actor *, qa_error *);
bool qa_application_network_detach(qa_application *, const qa_net_client *, qa_error *);

/* A caller owns this observation storage; snapshot.entities points into it.
 * The producer reads complete original records from a qualified Q3 game host.
 * Builtin/native mixed projections require their own complete wire producer
 * and are explicitly rejected here. It never publishes another world. */
typedef struct qa_application_network_q3_frame {
    qa_q3_snapshot snapshot;
    qa_q3_visible_entities visible;
} qa_application_network_q3_frame;
const qa_q3_gamestate *qa_application_network_q3_gamestate(qa_application *, qa_actor_id);
bool qa_application_network_q3_source(qa_application *, qa_actor_id,
    uint32_t *source_slot, qa_q3_product *, qa_error *);
bool qa_application_network_q3_slots(qa_application *, qa_actor_id,
    bool occupied[64], qa_error *);
qa_cvars *qa_application_network_q3_cvars(qa_application *, qa_actor_id);
typedef struct qa_application_network_q3_status_player {
    uint32_t slot;
    int32_t score, ping;
    const char *userinfo;
} qa_application_network_q3_status_player;
/* Userinfo borrows the source owner until its next mutation. */
bool qa_application_network_q3_status(qa_application *, qa_actor_id,
    qa_application_network_q3_status_player players[64], size_t *count, qa_error *);
/* Complete original signon records and authoritative clock from the same
 * qualified game. Pure hosting requires the package-reference producer;
 * absent that contract it is rejected before publishing systeminfo. */
bool qa_application_network_q3_signon(qa_application *, qa_actor_id,
    int32_t server_id, int32_t checksum_feed, qa_q3_gamestate *, qa_q3_server_world *, qa_error *);
bool qa_application_network_q3_world(qa_application *, qa_actor_id,
    int32_t server_id, int32_t restarted_server_id, int32_t checksum_feed,
    qa_q3_server_world *, qa_error *);
bool qa_application_network_q3_userinfo(qa_application *, qa_actor_id, const char *, qa_error *);
bool qa_application_network_q3_snapshot(qa_application *, qa_actor_id,
    int32_t message_number, int32_t server_command_number, uint8_t flags,
    qa_application_network_q3_frame *, qa_error *);
/* Qualified external cgame consumes native client snapshots and owns source
 * prediction. Admission preserves the selected gameplay composition. */
bool qa_application_network_q3_client_source(qa_application *, qa_actor_id,
    qa_actor_owner *, qa_q3_product *, qa_error *);
bool qa_application_network_q3_client_command(qa_application *, qa_actor_owner,
    uint32_t seat, const qa_q3_tokens *, qa_error *);
bool qa_application_network_q3_client_clear(qa_application *, qa_actor_owner,
    uint32_t seat, qa_error *);
/* Connection-owned projection, initialized to zero. Source numbers are kept
 * here rather than in the local GAME source-slot namespace. Bodies contain
 * authoritative snapshot state, remain unlinked, and grant no input authority.
 * Keep current and next presentation snapshots together until the source clock
 * transitions; release before closing the borrowed application. */
typedef struct qa_application_network_q3_projection {
    qa_actor_owner owner;
    qa_actor_definition definition;
    qa_actor_id actors[QA_Q3_ENTITY_WORLD];
} qa_application_network_q3_projection;
bool qa_application_network_q3_client_project(qa_application *, qa_actor_owner,
    qa_application_network_q3_projection *, const qa_q3_snapshot *current,
    const qa_q3_snapshot *next, qa_error *);
bool qa_application_network_q3_client_unproject(qa_application *,
    qa_application_network_q3_projection *, qa_error *);
bool qa_application_network_q3_client_actor(qa_application *,
    const qa_application_network_q3_projection *, uint32_t source,
    qa_actor_id *, bool *present, qa_error *);
#endif

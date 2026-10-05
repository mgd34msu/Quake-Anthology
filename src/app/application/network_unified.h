#ifndef QA_APPLICATION_NETWORK_UNIFIED_H
#define QA_APPLICATION_NETWORK_UNIFIED_H

#include "qa/application_players.h"
#include "qa/network_unified_session.h"
#include "qa/executable_recipe.h"

struct qa_unified_world_frame;

/* Borrows the installed Source, not an actor's selected CHARACTER. The receipt
 * expires at the next application publication or Source frame. */
typedef struct application_unified_source {
    const qa_launch_snapshot *launch;
    qa_session *session;
    qa_world *world;
    qa_actor_owner owner;
    qa_game_family family;
    qa_source_frame frame;
    uint64_t publication, map_revision, frame_revision;
    uint32_t max_clients;
} application_unified_source;

bool application_unified_source_read(qa_application *, application_unified_source *, qa_error *);
bool application_unified_source_current(const qa_application *, const application_unified_source *);
bool application_unified_source_checkpoint_read(qa_application *, application_unified_source *, qa_error *);
bool application_unified_source_checkpoint_current(const qa_application *, const application_unified_source *);
bool application_unified_source_slot_occupied(qa_application *, uint32_t client_slot,
    bool *, qa_error *);
bool application_unified_player_read(qa_application *, qa_net_client_id, qa_net_seat_id,
    qa_unified_session_player *, qa_error *);
bool application_unified_player_current(qa_application *, qa_net_client_id,
    const qa_unified_session_player *);
bool application_unified_player_checkpoint_read(qa_application *, qa_net_client_id, qa_net_seat_id,
    qa_unified_session_player *, qa_error *);
bool application_unified_player_checkpoint_current(qa_application *, qa_net_client_id,
    const qa_unified_session_player *);
bool application_unified_player_admit(qa_application *, qa_network_runtime *, const qa_application_remote_player_request *,
    qa_unified_session_player *, qa_error *);
bool application_unified_player_userinfo(qa_application *, qa_net_client_id, qa_net_seat_id,
    const char *, qa_error *);
bool application_unified_player_command(qa_application *, qa_net_client_id, qa_net_seat_id,
    const char *name, const char *const *arguments, size_t count, qa_error *);
bool application_unified_component_command(qa_application *, qa_net_client_id, qa_net_seat_id,
    const qa_unified_document *owner, uint64_t generation, const char *const *arguments,
    size_t count, qa_error *);
bool application_unified_source_command(qa_application *,qa_net_client_id,qa_net_seat_id,
    const qa_unified_source_command *,qa_error *);
bool application_unified_player_input(qa_application *, qa_net_client_id, qa_net_seat_id,
    qa_actor_id, const qa_unified_input *, qa_error *);
bool application_unified_player_disconnect(qa_application *, qa_net_client_id, qa_net_seat_id,
    qa_error *);

typedef struct application_unified_inputs application_unified_inputs;
/* The actual peer owns this retained, unentered Source programme. Queueing
 * authenticates every command; flush runs once before the next Source frame. */
bool application_unified_inputs_create(qa_application *, qa_network_runtime *, qa_net_client_id,
    qa_net_seat_id, uint32_t epoch, application_unified_inputs **, qa_error *);
bool application_unified_inputs_queue(application_unified_inputs *, const qa_unified_input_batch *,
    qa_error *);
bool application_unified_inputs_flush(application_unified_inputs *, qa_error *);
int64_t application_unified_inputs_submitted(const application_unified_inputs *);
bool application_unified_inputs_destroy(application_unified_inputs *, qa_error *);

typedef struct application_unified_server application_unified_server;
struct application_unified_output_external;
bool application_unified_server_create(qa_application *, qa_network_runtime *, qa_net_seat_id,
    uint32_t application_seat, uint32_t epoch, const qa_recipe_sidecar *, size_t,
    application_unified_server **, qa_unified_document **owned_offer, qa_error *);
const qa_sha256_digest *application_unified_server_composition(const application_unified_server *);
qa_unified_session_hooks application_unified_server_hooks(application_unified_server *);
bool application_unified_server_bind(application_unified_server *, qa_net_client_id,
    qa_unified_session *, qa_error *);
bool application_unified_server_offer(application_unified_server *, uint32_t epoch,
    const qa_recipe_sidecar *, size_t, qa_unified_document **owned_offer, qa_error *);
bool application_unified_server_pre_frame(application_unified_server *, qa_error *);
bool application_unified_server_publish(application_unified_server *,
    struct qa_unified_world_frame *borrowed_world,
    const struct application_unified_output_external *, qa_error *);
bool application_unified_server_publication_complete(const application_unified_server *);
bool application_unified_server_source_drop(application_unified_server *,qa_actor_owner,
    uint32_t source_slot,const char *reason,bool *matched,qa_error *);
bool application_unified_source_drop_recipient(qa_application *,qa_actor_owner,uint32_t,
    const char *,qa_actor_id *,qa_net_client_id *,qa_net_seat_id *,bool *,qa_error *);
bool application_unified_server_source_drop_finish(application_unified_server *,qa_error *);
bool application_unified_server_source_drop_current(const application_unified_server *,qa_error *);
bool application_unified_server_destroy(application_unified_server *, qa_error *);
bool application_unified_server_transport_retired(application_unified_server *,
    const qa_unified_session *, qa_error *);

#endif

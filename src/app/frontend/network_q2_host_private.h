#ifndef QA_FRONTEND_NETWORK_Q2_HOST_PRIVATE_H
#define QA_FRONTEND_NETWORK_Q2_HOST_PRIVATE_H
#include "network_q2_host.h"
#include "qa/network_q2_unicast.h"
#include "../../network/event_receipts.h"

typedef struct q2_host_peer {
    frontend_network_q2_host *host;
    qa_application_network_q2 *source;
    qa_q2_server_admission admission;
    qa_network_q2_server_hooks source_hooks;
    qa_net_seat_binding bindings[QA_Q2_MAX_SEATS];
    uint32_t slots[QA_Q2_MAX_SEATS];
    qa_net_client_id client;
    char userinfo[8193],reason[1024];
    char **configs;
    size_t config_count;
    char **signon_configs;
    size_t signon_config_count;
    uint64_t event_generation;
    uint64_t event_cursor;
    uint64_t player_event_cursor;
    qa_event_receipts event_receipts;
    uint64_t event_pending_first, event_pending_after;
    uint64_t event_transport_first, event_transport_after, event_transport_before;
    qa_bytes event_packet;
    uint8_t *event_storage;
    size_t event_capacity;
    bool event_pending,event_reliable,event_player;
    bool reserved,committed,retiring;
    bool material_scripts;
    qa_application_network_q2 *travel_source;
    qa_application_network_q2 *import_source;
    qa_actor_id import_actors[QA_Q2_MAX_SEATS];
    uint64_t import_epoch;
    bool import_bound;
    bool travel_installed;
} q2_host_peer;
struct frontend_network_q2_host {
    frontend_network_q2_host_options options;
    qa_network_q2_bootstrap *bootstrap;
    qa_application_network_q2 *discovery;
    qa_application_network_q2_host source;
    qa_q2_unicast_cache *unicast;
    qa_q2_messages *event_decoders[2];
    q2_host_peer *peers;
    size_t capacity;
    unsigned calls;
    qa_application_network_q2 *travel_discovery;
    qa_application_network_q2 *import_discovery;
    qa_application_network_q2_host travel_target;
    size_t travel_cursor;
    int32_t server_count;
    bool traveling,travel_discovery_installed;
    bool importing,import_ready,import_published;
    uint64_t import_network_owner;
    uint64_t import_map_revision;
    qa_network_runtime *import_runtime;
    qa_buffer import_bootstrap,import_unicast;
    frontend_demo_sink demo_sink;
    qa_actor_id demo_actor;
    qa_q2_codec demo_codec;
    uint8_t demo_bytes[65535];
    char **demo_configs;
    size_t demo_config_count;
    uint64_t demo_event_cursor, demo_player_event_cursor;
    uint64_t demo_event_generation, demo_map_revision, demo_frame;
    bool demo_held, demo_frame_set;
};

bool frontend_network_q2_host_bind_publisher(frontend_network_q2_host *,qa_application_network_q2 *,
    qa_network_runtime *,qa_network_q2_server_hooks *,qa_error *);
bool frontend_network_q2_host_bind_peer(q2_host_peer *,qa_application_network_q2 *,
    qa_network_runtime *,qa_network_q2_server_hooks *,qa_error *);
qa_q2_server_bootstrap_options frontend_network_q2_host_bootstrap_options(frontend_network_q2_host *);

#endif

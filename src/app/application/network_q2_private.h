#ifndef APPLICATION_NETWORK_Q2_PRIVATE_H
#define APPLICATION_NETWORK_Q2_PRIVATE_H

#include "internal.h"
#include "guest_native_q2_private.h"
#include "native_q2_console.h"
#include "map_players_private.h"
#include "qa/application_network_q2.h"
#include "qa/game_q2_wire.h"
#include "qa/native_host_q2_wire.h"
#include "qa/hash.h"
#include "qa/game_q2_items.h"
#include "qa/game_q2_bots.h"

typedef struct application_q2_resource_table {
    uint32_t base, maximum, count;
    char **paths;
} application_q2_resource_table;
struct qa_application_network_q2 {
    qa_application *app;
    qa_application_network_q2_host host;
    qa_sha256_digest identity;
    int32_t server_count;
    uint32_t config_count, item_base, skin_base, light_base;
    uint32_t checksum_index, clients_index, air_index, n64_index;
    char **configs;
    application_q2_resource_table resources[3];
    qa_q2_config_entry *entries;
    qa_q2_entity *entities, *baselines;
    qa_q2_source_entity_motion *motion_rows;
    qa_q2_source_motion motion;
    size_t entity_count, baseline_count, entity_capacity;
    uint8_t area_bits[QA_Q2_MAX_SEATS][QA_Q2_MAX_AREABITS];
    qa_q2_status_player *status_players;
    char (*status_names)[32];
    qa_buffer status_info;
    qa_actor_id *event_actors;
    uint32_t *events;
    uint64_t event_frame;
    bool initialized, restored;
    qa_application_network_q2_bindings bindings;
    struct application_native_q2 *recipient_engine;
};

bool application_network_q2_current(qa_application_network_q2 *, qa_error *);
bool application_network_q2_config(qa_application_network_q2 *, uint32_t,
    const char *, qa_error *);
bool application_network_q2_layout(qa_application_network_q2 *, qa_error *);
void application_network_q2_free_tables(qa_application_network_q2 *);
application_provider *application_network_q2_provider(qa_application_network_q2 *);
bool application_network_q2_resource(qa_application_network_q2 *, unsigned,
    const char *, uint32_t *, qa_error *);
bool application_network_q2_observe(qa_application_network_q2 *, qa_error *);
bool application_network_q2_entities(qa_application_network_q2 *, qa_error *);
bool application_network_q2_player_state(qa_application_network_q2 *, qa_actor_id,
    qa_q2_player *, qa_error *);
char *application_network_q2_copy(const char *, qa_error *);
void application_network_q2_unbind(qa_application_network_q2 *);

#endif

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
#include "native_q2_wire_engine.h"
#include "qa/material.h"
#include "qa/material_library_save.h"

typedef enum application_q2_held_kind {
    APPLICATION_Q2_HELD_MODEL,
    APPLICATION_Q2_HELD_DEPENDENCY,
    APPLICATION_Q2_HELD_MATERIAL
} application_q2_held_kind;
typedef struct application_q2_held_resource {
    qa_actor_owner provider;
    char *instance, *path, *wire_path;
    qa_sha256_digest identity;
    qa_vfs *view;
    qa_resource *resource;
    qa_vfs_acquisition opening;
    application_q2_held_kind kind;
    bool missing;
    qa_sha256_digest authority;
    qa_buffer wire_bytes;
    size_t *dependencies, dependency_count;
    char *script_name, *sky_base;
    size_t source_offset, script_size, name_offset, name_size;
    qa_buffer catalog_bytes;
    qa_sha256_digest sky_group;
    uint8_t sky_face;
} application_q2_held_resource;

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
    bool materials_bound, materials_capability;
    qa_application_network_q2_bindings bindings;
    struct application_native_q2 *recipient_engine;
    application_q2_held_resource *held_resources;
    size_t held_resource_count, held_resource_capacity;
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
bool application_network_q2_visual_resource(qa_application_network_q2 *,
    const qa_application_visual_view *, unsigned, uint32_t *, qa_error *);
bool application_network_q2_download_resource(void *, const char *, const qa_vfs **,
    qa_resource **, qa_vfs_acquisition *, qa_buffer *, qa_buffer *, qa_q2_download_resource_status *, qa_error *);
bool application_network_q2_materials_required(const qa_application_network_q2 *);
bool application_network_q2_dependency(qa_application_network_q2 *,
    const application_q2_held_resource *, const char *, size_t *, qa_error *);
bool application_network_q2_dependency_receipt(qa_application_network_q2 *,
    const application_q2_held_resource *, const char *, const qa_resource *,
    const qa_vfs_acquisition *, size_t *, qa_error *);
bool application_network_q2_dependency_of(const application_q2_held_resource *,
    const application_q2_held_resource *);
bool application_network_q2_material_resource(qa_application_network_q2 *,
    const application_q2_held_resource *, const qa_material_script_view *, qa_bytes,
    const size_t *, size_t, size_t *, qa_error *);
bool application_network_q2_sky_dependencies(qa_application_network_q2 *,
    const application_q2_held_resource *, const char *, const char *const [6],
    const qa_resource *const [6], const qa_vfs_acquisition *const [6],
    size_t [6], char [64], qa_error *);
bool application_network_q2_sky_group_valid(const application_q2_held_resource *,
    const application_q2_held_resource *const [6]);
void application_network_q2_resources_free(qa_application_network_q2 *);
bool application_network_q2_resources_capture(qa_application_network_q2 *, qa_buffer *, qa_error *);
bool application_network_q2_resources_restore(qa_application_network_q2 *, qa_bytes, qa_error *);

#endif

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
#include "qa/hud_q2.h"

enum { APPLICATION_Q2_LAYOUT_BYTES = 65536 };

typedef struct application_q2_layout_receipt {
    qa_actor_id actor;
    char *text;
    qa_hud_q2_stat_references references;
    qa_native_profile profile;
} application_q2_layout_receipt;

typedef enum application_q2_held_kind {
    APPLICATION_Q2_HELD_MODEL,
    APPLICATION_Q2_HELD_DEPENDENCY,
    APPLICATION_Q2_HELD_MATERIAL,
    APPLICATION_Q2_HELD_EVENT,
    APPLICATION_Q2_HELD_IMAGE,
    APPLICATION_Q2_HELD_ALIAS,
    APPLICATION_Q2_HELD_IMAGE_RECEIPT
} application_q2_held_kind;
typedef struct application_q2_image_receipt {
    const char *request, *path;
    const qa_resource *source;
    const qa_vfs_acquisition *opening;
    const char *logical_path;
    const qa_resource *logical_source;
    const qa_vfs_acquisition *logical_opening;
    const qa_scene_image_options *options;
    qa_bytes palette_rgb;
    const qa_scene_palette_source *palette_source;
    bool palette_attempted;
    qa_status rejection, palette_error;
} application_q2_image_receipt;
typedef struct application_q2_held_resource {
    qa_actor_owner provider;
    char *instance, *path, *wire_path;
    uint64_t identity, serial;
    qa_vfs *view;
    qa_resource *resource;
    qa_vfs_acquisition opening;
    application_q2_held_kind kind;
    bool missing;
    bool model_scope;
    char *model_scope_path;
    qa_buffer model_scope_bytes;
    uint64_t authority;
    qa_buffer wire_bytes;
    size_t *dependencies, dependency_count;
    char *script_name, *sky_base;
    size_t source_offset, script_size, name_offset, name_size;
    qa_buffer catalog_bytes;
    uint64_t sky_group;
    uint8_t sky_face;
    qa_native_host_resource_kind event_kind;
    char event_key[QA_APPLICATION_RESOURCE_KEY_CAPACITY];
    uint64_t event_custody;
    qa_scene_image_options image_options;
    qa_buffer image_palette, image_translation;
    size_t image_palette_dependency;
    char *image_request, *image_logical_path;
    size_t image_logical_dependency;
    bool image_palette_attempted;
    qa_status image_rejection, image_palette_error;
    uint64_t receipt_source;
} application_q2_held_resource;

typedef struct application_q2_resource_table {
    uint32_t base, maximum, count;
    char **paths;
} application_q2_resource_table;
struct qa_application_network_q2 {
    qa_application *app;
    qa_application_network_q2_host host;
    char *source_instance, *source_map;
    int32_t server_count;
    uint32_t config_count, item_base, skin_base, light_base;
    uint32_t checksum_index, clients_index, air_index, n64_index;
    char **configs;
    application_q2_resource_table resources[3];
    qa_q2_config_entry *entries;
    application_q2_layout_receipt *layouts;
    qa_q2_entity *entities, *source_entities, *baselines;
    qa_q2_source_entity_motion *motion_rows;
    qa_q2_source_motion motion;
    size_t entity_count, baseline_count, entity_capacity;
    size_t source_entity_count;
    uint64_t source_frame, source_application_frame, source_mutation, source_actors_revision;
    uint64_t source_resource_revision;
    bool source_entities_ready;
    uint8_t area_bits[QA_Q2_MAX_SEATS][QA_Q2_MAX_AREABITS];
    qa_q2_status_player *status_players;
    char (*status_names)[32];
    qa_buffer status_info;
    qa_actor_id *event_actors;
    uint32_t *events;
    uint64_t event_frame;
    bool initialized, restored, archival;
    bool materials_bound, materials_capability;
    qa_application_network_q2_bindings bindings;
    struct application_q2_recipient_binding *recipient_binding;
    application_q2_held_resource *held_resources;
    size_t held_resource_count, held_resource_capacity;
    uint64_t held_resource_serial;
};

bool application_network_q2_current(qa_application_network_q2 *, qa_error *);
bool application_network_q2_config(qa_application_network_q2 *, uint32_t,
    const char *, qa_error *);
bool application_network_q2_layout(qa_application_network_q2 *, qa_error *);
void application_network_q2_free_tables(qa_application_network_q2 *);
application_provider *application_network_q2_provider(qa_application_network_q2 *);
bool application_network_q2_resource(qa_application_network_q2 *, unsigned,
    const char *, uint32_t *, qa_error *);
bool application_network_q2_source_resource(qa_application_network_q2 *, unsigned,
    uint32_t, uint32_t *, qa_error *);
bool application_network_q2_source_config(qa_application_network_q2 *, uint32_t,
    uint32_t *, qa_error *);
bool application_network_q2_observe(qa_application_network_q2 *, qa_error *);
bool application_network_q2_source_resources(qa_application_network_q2 *, qa_error *);
bool application_network_q2_entities(qa_application_network_q2 *, qa_error *);
void application_network_q2_capture_dispose(application_provider *);
bool application_network_q2_player_state(qa_application_network_q2 *, qa_actor_id,
    qa_q2_player *, qa_error *);
char *application_network_q2_copy(const char *, qa_error *);
void application_network_q2_unbind(qa_application_network_q2 *);
void application_network_q2_retire_bindings(struct application_native_q2 *);
void application_network_q2_retire_source_bindings(application_provider *);
bool application_network_q2_print_recipients(application_provider *, qa_actor_id,
    const qa_application_network_q2_recipient_view **, size_t *, qa_error *);
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
bool application_network_q2_dependency_image(qa_application_network_q2 *,
    const application_q2_held_resource *, const char *, const qa_resource *, const qa_vfs_acquisition *,
    const qa_scene_image_options *, qa_bytes palette_rgb, const char *palette_path,
    const qa_resource *palette_resource, const qa_vfs_acquisition *palette_opening,
    qa_bytes derived_png, size_t *, qa_error *);
bool application_network_q2_dependency_alias(qa_application_network_q2 *,
    const application_q2_held_resource *, const application_q2_image_receipt *, size_t *, qa_error *);
bool application_network_q2_dependency_image_receipt(qa_application_network_q2 *,
    const application_q2_held_resource *, size_t alias_index, size_t *, qa_error *);
bool application_network_q2_dependency_of(const application_q2_held_resource *,
    const application_q2_held_resource *);
bool application_network_q2_material_resource(qa_application_network_q2 *,
    const application_q2_held_resource *, const qa_material_script_view *, qa_bytes,
    qa_scene_family, qa_bytes palette_rgb, const qa_scene_palette_source *,
    const size_t *, size_t, size_t *, qa_error *);
bool application_network_q2_sky_dependencies(qa_application_network_q2 *,
    const application_q2_held_resource *, const char *, const char *const [6],
    const qa_resource *const [6], const qa_vfs_acquisition *const [6],
    size_t [6], char [64], qa_error *);
bool application_network_q2_sky_aliases(qa_application_network_q2 *,
    const application_q2_held_resource *, const char *, const application_q2_image_receipt [6],
    size_t [6], char [64], qa_error *);
void application_network_q2_resources_free(qa_application_network_q2 *);
bool application_network_q2_resources_capture(qa_application_network_q2 *, qa_buffer *, qa_error *);
bool application_network_q2_resources_capture_retained(qa_application_network_q2 *, qa_buffer *, qa_error *);
bool application_network_q2_resources_restore(qa_application_network_q2 *, qa_bytes, qa_error *);

#endif
